#include "diag.h"

#ifdef CONFIG_ZMK_JAVELIN_DIAG

#include <stdarg.h>
#include <string.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#if IS_ENABLED(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif
#if __has_include("diag_build_id.h")
#include "diag_build_id.h"
#endif
#ifndef JAVELIN_BUILD_ID
#define JAVELIN_BUILD_ID "unknown"
#endif

#define RING_BYTES 1024U
#define RING_MAGIC 0x4a44494cU
#define LINE_BYTES 112U
#define COUNTER_SETTING "lapwing/diag"
/* ZMK has loaded settings long before this. Nothing is saved earlier, so a
 * counter is never written over a record that was not read yet.
 */
#define SAVE_AFTER_MS 5000U
#define SAVE_SPACING_MS 2000U

/* Kept in RAM that a soft reset does not clear. After a power loss the magic
 * is wrong and the ring starts empty.
 */
struct ring {
    uint32_t magic;
    uint32_t boot;
    uint32_t head;
    uint32_t used;
    char data[RING_BYTES];
};
static struct ring ring __noinit;
static struct k_spinlock ring_lock;

static uint32_t session[DIAG_EVENT_COUNT];
static uint32_t unsaved[DIAG_PERSISTED_COUNT];
static bool dirty;
static uint32_t last_save_ms;
static uint32_t reset_cause;
static bool reset_cause_known;

static bool wake_pending;
static bool wake_from_idle;
static uint32_t wake_requested_ms;
static uint32_t wake_count;
static uint32_t wake_last_ms;
static uint32_t wake_max_ms;
static uint32_t wake_total_ms;
static uint32_t wake_last_check_ms;
static bool wake_last_quick;
static bool wake_last_idle;

static const char *const event_names[DIAG_PERSISTED_COUNT] = {
    "boots", "start-up retries", "rail power cycles", "quick checks that fell back to full",
    "bring-ups with no dictionaries", "serial erases", "serial writes",
};

static int init_ring(void) {
    if (ring.magic != RING_MAGIC || ring.head >= RING_BYTES || ring.used > RING_BYTES) {
        memset(&ring, 0, sizeof(ring));
        ring.magic = RING_MAGIC;
    }
    ring.boot++;
    reset_cause_known = hwinfo_get_reset_cause(&reset_cause) == 0;
    return 0;
}
SYS_INIT(init_ring, POST_KERNEL, 0);

static void ring_put(const char *text, size_t length) {
    k_spinlock_key_t key = k_spin_lock(&ring_lock);

    for (size_t i = 0; i < length; i++) {
        ring.data[ring.head] = text[i];
        ring.head = (ring.head + 1U) % RING_BYTES;
        if (ring.used < RING_BYTES) {
            ring.used++;
        }
    }
    k_spin_unlock(&ring_lock, key);
}

void diag_log(const char *fmt, ...) {
    char line[LINE_BYTES];
    const uint32_t now = k_uptime_get_32();
    int length = snprintk(line, sizeof(line), "[b%u %u.%03u] ", ring.boot, now / 1000U,
                          now % 1000U);
    if (length < 0 || (size_t)length >= sizeof(line)) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    const int body = vsnprintk(line + length, sizeof(line) - (size_t)length, fmt, args);
    va_end(args);
    if (body < 0) {
        return;
    }
    length = (int)strlen(line);
    ring_put(line, (size_t)length);
    printk("%s", line);
}

/* Output for the serial commands: poll out with CRLF line ends. */
static void write_text(const struct device *console, const char *text, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\n') {
            uart_poll_out(console, '\r');
        }
        uart_poll_out(console, (unsigned char)text[i]);
    }
}

void diag_printf(const struct device *console, const char *fmt, ...) {
    char line[LINE_BYTES];
    va_list args;

    va_start(args, fmt);
    const int length = vsnprintk(line, sizeof(line) - 1U, fmt, args);
    va_end(args);
    if (length < 0) {
        return;
    }
    size_t used = MIN((size_t)length, sizeof(line) - 2U);
    line[used++] = '\n';
    write_text(console, line, used);
}

#if IS_ENABLED(CONFIG_SETTINGS)
static int read_counters(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
                         void *param) {
    ARG_UNUSED(key);
    if (len == sizeof(uint32_t) * DIAG_PERSISTED_COUNT) {
        (void)read_cb(cb_arg, param, len);
    }
    return 0;
}

static bool load_counters(uint32_t out[DIAG_PERSISTED_COUNT]) {
    memset(out, 0, sizeof(uint32_t) * DIAG_PERSISTED_COUNT);
    return settings_load_subtree_direct(COUNTER_SETTING, read_counters, out) == 0;
}

static void flush_counters(void) {
    uint32_t total[DIAG_PERSISTED_COUNT];

    /* Read what is saved, add what happened since, and write it back. */
    if (!load_counters(total)) {
        return;
    }
    for (size_t i = 0; i < DIAG_PERSISTED_COUNT; i++) {
        total[i] += unsaved[i];
    }
    if (settings_save_one(COUNTER_SETTING, total, sizeof(total)) == 0) {
        memset(unsaved, 0, sizeof(unsaved));
        dirty = false;
    } else {
        diag_log("Could not save the diagnostic counters\n");
    }
}
#else
static bool load_counters(uint32_t out[DIAG_PERSISTED_COUNT]) {
    memset(out, 0, sizeof(uint32_t) * DIAG_PERSISTED_COUNT);
    return false;
}
static void flush_counters(void) {
    dirty = false;
}
#endif

void diag_note(enum diag_event event) {
    if (event >= DIAG_EVENT_COUNT) {
        return;
    }
    session[event]++;
    if (event < DIAG_PERSISTED_COUNT) {
        unsaved[event]++;
        dirty = true;
    }
}

void diag_tick(void) {
    const uint32_t now = k_uptime_get_32();

    if (dirty && now >= SAVE_AFTER_MS && (last_save_ms == 0 || now - last_save_ms >= SAVE_SPACING_MS)) {
        last_save_ms = now ? now : 1U;
        flush_counters();
    }
}

void diag_wake_requested(bool from_idle) {
    if (!wake_pending) {
        wake_pending = true;
        wake_from_idle = from_idle;
        wake_requested_ms = k_uptime_get_32();
    }
}

void diag_wake_ready(bool mapped, bool quick, uint32_t check_ms) {
    if (!wake_pending) {
        return;
    }
    const uint32_t elapsed = k_uptime_get_32() - wake_requested_ms;

    wake_pending = false;
    if (!mapped) {
        diag_log("Wake from %s: flash not ready after %u ms\n",
                 wake_from_idle ? "idle" : "resume", elapsed);
        return;
    }
    wake_count++;
    wake_last_ms = elapsed;
    wake_total_ms += elapsed;
    if (elapsed > wake_max_ms) {
        wake_max_ms = elapsed;
    }
    wake_last_check_ms = check_ms;
    wake_last_quick = quick;
    wake_last_idle = wake_from_idle;
    diag_log("Wake from %s: ready %u ms after the request (%s check %u ms)\n",
             wake_from_idle ? "idle" : "resume", elapsed, quick ? "quick" : "full", check_ms);
}

static void print_reset_cause(const struct device *console) {
    if (!reset_cause_known) {
        diag_printf(console, "RESET unknown");
        return;
    }
    diag_printf(console, "RESET 0x%08x%s%s%s%s%s%s", reset_cause,
                (reset_cause & RESET_PIN) ? " pin" : "",
                (reset_cause & RESET_SOFTWARE) ? " software" : "",
                (reset_cause & RESET_BROWNOUT) ? " brownout" : "",
                (reset_cause & RESET_POR) ? " power-on" : "",
                (reset_cause & RESET_WATCHDOG) ? " watchdog" : "",
                (reset_cause & RESET_DEBUG) ? " debug" : "");
}

void diag_print_status(const struct device *console) {
    uint32_t stored[DIAG_PERSISTED_COUNT];
    const bool have_stored = load_counters(stored);

    diag_printf(console, "BUILD %s %s %s", JAVELIN_BUILD_ID, __DATE__, __TIME__);
    diag_printf(console, "UPTIME %u ms, boot %u since power loss", k_uptime_get_32(), ring.boot);
    print_reset_cause(console);
    diag_printf(console, "COUNTERS (saved%s + unsaved), this boot in brackets",
                have_stored ? "" : ", none read");
    for (size_t i = 0; i < DIAG_PERSISTED_COUNT; i++) {
        diag_printf(console, "  %s: %u [%u]", event_names[i], stored[i] + unsaved[i],
                    session[i]);
    }
    diag_printf(console, "CHECKS this boot: %u quick, %u full; idle power-downs %u",
                session[DIAG_QUICK_CHECK], session[DIAG_FULL_CHECK], session[DIAG_IDLE_DOWN]);
    if (wake_count) {
        diag_printf(console, "WAKES %u: last %u ms (%s %s check %u ms), max %u ms, mean %u ms",
                    wake_count, wake_last_ms, wake_last_idle ? "idle" : "resume",
                    wake_last_quick ? "quick" : "full", wake_last_check_ms, wake_max_ms,
                    wake_total_ms / wake_count);
    } else {
        diag_printf(console, "WAKES none since boot");
    }
}

void diag_dump_log(const struct device *console) {
    static char copy[RING_BYTES]; /* Static: the worker stack is small. */
    size_t length;

    k_spinlock_key_t key = k_spin_lock(&ring_lock);
    length = ring.used;
    const size_t start = (ring.head + RING_BYTES - length) % RING_BYTES;
    for (size_t i = 0; i < length; i++) {
        copy[i] = ring.data[(start + i) % RING_BYTES];
    }
    k_spin_unlock(&ring_lock, key);

    diag_printf(console, "LOG BEGIN %u bytes", (uint32_t)length);
    write_text(console, copy, length);
    diag_printf(console, "LOG END");
}

void diag_clear(const struct device *console) {
    k_spinlock_key_t key = k_spin_lock(&ring_lock);
    ring.head = 0;
    ring.used = 0;
    k_spin_unlock(&ring_lock, key);
    memset(session, 0, sizeof(session));
    memset(unsaved, 0, sizeof(unsaved));
    dirty = false;
    wake_count = wake_last_ms = wake_max_ms = wake_total_ms = 0;
#if IS_ENABLED(CONFIG_SETTINGS)
    (void)settings_delete(COUNTER_SETTING);
#endif
    diag_printf(console, "CLEARED log and counters");
}

#endif /* CONFIG_ZMK_JAVELIN_DIAG */
