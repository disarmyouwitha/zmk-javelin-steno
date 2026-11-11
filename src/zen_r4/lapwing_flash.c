#include "lapwing_flash.h"
#include "quad.h"
#ifdef CONFIG_ZMK_JAVELIN_BATTERY_LOG
#include "battery_log.h"
#endif

#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#ifdef CONFIG_ZMK_SLEEP
#include <zephyr/pm/device.h>
#endif
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>

#define QSPI_NODE DT_NODELABEL(qspi)
#define XIP_BASE DT_REG_ADDR_BY_NAME(QSPI_NODE, qspi_mm)
#define FLASH_BYTES (16U * 1024U * 1024U)
#define SECTOR_BYTES 4096U
#define PAGE_BYTES 256U
#define CHUNK_BYTES (2U * PAGE_BYTES)
#define JSC4_MAGIC 0x3443534aU

BUILD_ASSERT(LAPWING_BYTES % sizeof(uint32_t) == 0);
BUILD_ASSERT(LAPWING_BYTES <= BASE_DICTIONARY_LIMIT);
#if PLOVER_BYTES > 0
BUILD_ASSERT(LAPWING_BYTES <= PLOVER_FLASH_OFFSET);
BUILD_ASSERT(PLOVER_BYTES % sizeof(uint32_t) == 0);
BUILD_ASSERT(PLOVER_FLASH_OFFSET + PLOVER_BYTES <= BASE_DICTIONARY_LIMIT);
#endif

struct base_image {
    const char *name;
    uint32_t offset;
    uint32_t bytes;
    uint32_t crc;
    uint8_t command;
    uint8_t query;
    bool verified;
};
static struct base_image images[] = {
    {"LAPWING", 0, LAPWING_BYTES, LAPWING_CRC, 'L', '?'},
#if PLOVER_BYTES > 0
    {"PLOVER", PLOVER_FLASH_OFFSET, PLOVER_BYTES, PLOVER_CRC, 'P', 'p'},
#endif
};

static bool all_verified(void) {
    for (size_t i = 0; i < ARRAY_SIZE(images); i++) {
        if (!images[i].verified) {
            return false;
        }
    }
    return true;
}

PINCTRL_DT_DEFINE(QSPI_NODE);

static const struct gpio_dt_spec power =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), flash_power_gpios);
#define SERIAL_PROVISIONING IS_ENABLED(CONFIG_ZMK_JAVELIN_STENO_SERIAL_PROVISIONING)
#if SERIAL_PROVISIONING
static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static uint8_t incoming[CHUNK_BYTES] __aligned(4);
static bool last_check_quick;
static uint32_t last_check_ms;
#endif
#define IDLE_OFF IS_ENABLED(CONFIG_ZMK_JAVELIN_STENO_FLASH_IDLE_OFF)
static atomic_t mapped_ready;
/* Set once a bring-up attempt has finished, whatever its result. */
static atomic_t checked;

/* Called whenever the dictionaries become usable or a bring-up gives up.
 * Runs on the flash worker with the flash lock held: only submit work.
 */
__weak void lapwing_flash_ready_changed(void) {
}
static uint8_t readback[CHUNK_BYTES] __aligned(4);
/* Both bases passed a full CRC since this controller last lost power. The
 * flash cannot change while its rail is off, so later bring-ups may be quick.
 */
static bool trusted __maybe_unused;

#ifdef CONFIG_ZMK_JAVELIN_BATTERY_LOG
static atomic_t flash_powered;

bool lapwing_flash_is_powered(void) {
    return atomic_get(&flash_powered) != 0;
}
#endif

#ifdef CONFIG_ZMK_SLEEP
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_EXT_POWER), "The QSPI driver owns the flash power pin");
K_MUTEX_DEFINE(flash_lock);
/* True until the first bring-up, and again whenever the flash is powered down. */
static bool flash_suspended = true;
static atomic_t restart_requested = ATOMIC_INIT(1);
static K_SEM_DEFINE(restart_wake, 0, 1);
#if IDLE_OFF
#define IDLE_OFF_MS (CONFIG_ZMK_JAVELIN_STENO_FLASH_IDLE_SECONDS * 1000U)
/* Set while the idle timer, and not ZMK's sleep, has the flash powered down. */
static atomic_t idle_off;
static atomic_t last_use;

static void note_use(void) {
    atomic_set(&last_use, (atomic_val_t)k_uptime_get_32());
}
#endif

/* GPREGRET2 survives System OFF and resets, and clears when power is lost.
 * Deep sleep leaves a marker tied to the expected images, so the wake that
 * follows can skip the two full CRC passes. Anything else verifies in full.
 */
static uint8_t trust_marker(void) {
    uint32_t value = LAPWING_CRC ^ LAPWING_BYTES ^ (PLOVER_CRC * 31U) ^ PLOVER_BYTES;

    value ^= value >> 16;
    value ^= value >> 8;
    value &= 0xff;
    return value ? (uint8_t)value : 0xa5;
}

static bool take_wake_trust(void) {
    const bool wake = (NRF_POWER->GPREGRET2 & 0xff) == trust_marker();

    NRF_POWER->GPREGRET2 = 0;
    return wake || trusted;
}

static void request_restart(void) {
    atomic_set(&restart_requested, 1);
    k_sem_give(&restart_wake);
}

/* Ask the worker to power an idle flash back up, and wait for the result. */
static bool wake_idle_flash(void) {
#if IDLE_OFF
    if (!atomic_get(&idle_off)) {
        return false;
    }
    request_restart();
    for (int waited = 0; atomic_get(&idle_off) && waited < 1000; waited += 2) {
        k_msleep(2);
    }
    return atomic_get(&mapped_ready) != 0;
#else
    return false;
#endif
}

bool lapwing_flash_acquire(void) {
    /* The second pass covers an idle power-down that won the lock first. */
    for (int attempt = 0; attempt < 2; attempt++) {
#if IDLE_OFF
        note_use();
#endif
        if (!atomic_get(&mapped_ready) && !wake_idle_flash()) {
            return false;
        }
        k_mutex_lock(&flash_lock, K_FOREVER);
        if (atomic_get(&mapped_ready)) {
            return true;
        }
        k_mutex_unlock(&flash_lock);
    }
    return false;
}

void lapwing_flash_release(void) {
    k_mutex_unlock(&flash_lock);
}

/* Hold flash_lock. False while a flash operation is still in progress. */
static bool flash_is_idle(bool initialized) {
    return !initialized ||
           (reactivate_after_xip() && nrfx_qspi_mem_busy_check() == NRFX_SUCCESS);
}

/* Hold flash_lock. Stops QSPI, floats its pins and switches the rail off. */
static int power_down_flash(bool initialized) {
    atomic_clear(&mapped_ready);
    flash_suspended = true;
    if (initialized) {
        nrfx_qspi_uninit();
    }
    int rc = pinctrl_apply_state(PINCTRL_DT_DEV_CONFIG_GET(QSPI_NODE), PINCTRL_STATE_SLEEP);
    if (rc == 0) {
        rc = gpio_pin_configure_dt(&power, GPIO_OUTPUT_INACTIVE);
    }
#ifdef CONFIG_ZMK_JAVELIN_BATTERY_LOG
    if (rc == 0) {
        atomic_clear(&flash_powered);
    }
#endif
    return rc;
}

static int flash_pm_action(const struct device *dev, enum pm_device_action action) {
    ARG_UNUSED(dev);
    if (action == PM_DEVICE_ACTION_RESUME) {
        // ZMK aborted power-off and rolled back device suspension. Let the
        // worker restore the same mapping before allowing new flash access.
        NRF_POWER->GPREGRET2 = 0;
        request_restart();
        return 0;
    }
    if (action != PM_DEVICE_ACTION_SUSPEND) {
        return -ENOTSUP;
    }
    if (k_mutex_lock(&flash_lock, K_NO_WAIT) != 0) {
        // ZMK retries on its next idle check. Let any read or save finish.
        return -EBUSY;
    }
    int rc = 0;
    if (flash_suspended) {
#if IDLE_OFF
        /* The idle timer already powered the flash down. Sleep takes over. */
        if (!atomic_cas(&idle_off, 1, 0)) {
            k_mutex_unlock(&flash_lock);
            return -EBUSY;
        }
#else
        k_mutex_unlock(&flash_lock);
        return -EBUSY;
#endif
    } else {
        const bool initialized = nrfx_qspi_init_check();
        if (!flash_is_idle(initialized)) {
            k_mutex_unlock(&flash_lock);
            return -EBUSY;
        }
        rc = power_down_flash(initialized);
    }
    atomic_clear(&restart_requested);
    if (rc != 0) {
        // A failing suspend is not on ZMK's resume list.
        request_restart();
    } else if (trusted) {
        NRF_POWER->GPREGRET2 = trust_marker();
    }
    k_mutex_unlock(&flash_lock);
#ifdef CONFIG_ZMK_JAVELIN_BATTERY_LOG
    if (rc == 0) {
        zen_battery_log_sleep();
    }
#endif
    return rc;
}

PM_DEVICE_DEFINE(zen_r4_flash, flash_pm_action);
DEVICE_DEFINE(zen_r4_flash, "ZEN_R4_QSPI", NULL, PM_DEVICE_GET(zen_r4_flash), NULL, NULL,
              POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);
#else
static bool take_wake_trust(void) {
    return false;
}
#endif

uintptr_t lapwing_mapped_address(void) {
    return atomic_get(&mapped_ready) ? XIP_BASE : 0;
}

enum lapwing_flash_state lapwing_flash_status(void) {
#if IDLE_OFF
    /* An idle flash comes back on first use, so it still counts as ready. */
    if (atomic_get(&idle_off)) {
        return LAPWING_FLASH_READY;
    }
#endif
    if (atomic_get(&mapped_ready)) {
        return LAPWING_FLASH_READY;
    }
    return atomic_get(&checked) ? LAPWING_FLASH_UNAVAILABLE : LAPWING_FLASH_LOADING;
}

void lapwing_flash_prepare(void) {
#if IDLE_OFF
    /* Restart the idle timer, and start an idle flash without waiting for it. */
    note_use();
    if (!atomic_get(&mapped_ready) && atomic_get(&idle_off)) {
        request_restart();
    }
#endif
}

static bool start_qspi(nrfx_qspi_config_t *config) {
    if (!gpio_is_ready_dt(&power) || gpio_pin_configure_dt(&power, GPIO_OUTPUT_ACTIVE) != 0) {
        return false;
    }
#ifdef CONFIG_ZMK_JAVELIN_BATTERY_LOG
    atomic_set(&flash_powered, 1);
#endif
    k_msleep(50);
    if (pinctrl_apply_state(PINCTRL_DT_DEV_CONFIG_GET(QSPI_NODE), PINCTRL_STATE_DEFAULT) != 0 ||
        nrfx_qspi_init(config, NULL, NULL) != NRFX_SUCCESS ||
        nrfx_qspi_activate(true) != NRFX_SUCCESS) {
        return false;
    }

    const nrf_qspi_cinstr_conf_t read_id = {
        .opcode = 0x9f, .length = NRF_QSPI_CINSTR_LEN_4B, .io2_level = true, .io3_level = true,
    };
    uint8_t id[3];
    if (nrfx_qspi_cinstr_xfer(&read_id, NULL, id) != NRFX_SUCCESS) {
        return false;
    }
    printk("Lapwing JEDEC ID: %02x %02x %02x\n", id[0], id[1], id[2]);
    return id[0] == 0xef && id[1] == 0x40 && id[2] == 0x18;
}

static bool read_image_crc(const struct base_image *image) {
    uint32_t crc = 0;

    for (uint32_t offset = 0; offset < image->bytes; offset += CHUNK_BYTES) {
        const size_t size = MIN(CHUNK_BYTES, image->bytes - offset);

        if (nrfx_qspi_read(readback, size, image->offset + offset) != NRFX_SUCCESS) {
            return false;
        }
        crc = crc32_ieee_update(crc, readback, size);
        k_yield();
    }
    return crc == image->crc;
}

/* Four data lines for every read that follows, raw or mapped. */
static bool enable_quad_reads(nrfx_qspi_config_t *config) {
    if (w25q128_enable_volatile_quad() != NRFX_SUCCESS) {
        return false;
    }
    config->prot_if.readoc = NRF_QSPI_READOC_READ4O;
    return nrfx_qspi_reconfigure(config) == NRFX_SUCCESS &&
           nrfx_qspi_activate(true) == NRFX_SUCCESS;
}

static bool enable_verified_mapping(nrfx_qspi_config_t *config, bool quick) {
    if (!enable_quad_reads(config)) {
        return false;
    }

    for (size_t i = 0; !quick && i < ARRAY_SIZE(images); i++) {
        const struct base_image *image = &images[i];
        uint32_t crc = 0;
        const volatile uint32_t *mapped = (const volatile uint32_t *)(XIP_BASE + image->offset);
        for (size_t offset = 0; offset < image->bytes; offset += CHUNK_BYTES) {
            const size_t size = MIN(CHUNK_BYTES, image->bytes - offset);
            for (size_t index = 0; index < size; index += sizeof(uint32_t)) {
                const uint32_t word = mapped[(offset + index) / sizeof(uint32_t)];
                __DSB();
                memcpy(readback + index, &word, sizeof(word));
            }
            crc = crc32_ieee_update(crc, readback, size);
            k_yield();
        }
        if (crc != image->crc) {
            printk("%s mapped CRC mismatch: %08x\n", image->name, crc);
            return false;
        }
    }
    if (!lapwing_verify_native_dictionary(XIP_BASE)) {
        return false;
    }
#if PLOVER_BYTES > 0
    if (!plover_verify_native_dictionary(XIP_BASE + PLOVER_FLASH_OFFSET)) {
        return false;
    }
#endif

    // The personal-dictionary store serializes its QSPI operations with mapped lookups.
    trusted = true;
    atomic_set(&mapped_ready, 1);
    return true;
}

#if SERIAL_PROVISIONING
static void reply(const struct base_image *image, const char *message, uint32_t value) {
    char line[64];

    snprintk(line, sizeof(line), "%s %s %u\r\n", image->name, message, value);
    for (const char *character = line; *character; character++) {
        uart_poll_out(console, *character);
    }
}

static bool receive_chunk(size_t size) {
    const int64_t deadline = k_uptime_get() + 5000;
    size_t received = 0;

    while (received < size && k_uptime_get() < deadline) {
        if (uart_poll_in(console, incoming + received) == 0) {
            received++;
        } else {
            k_msleep(1);
        }
    }
    return received == size;
}

static bool wait_until_ready(void) {
    const int64_t deadline = k_uptime_get() + 2000;
    nrfx_err_t rc;

    do {
        rc = nrfx_qspi_mem_busy_check();
        if (rc != NRFX_ERROR_BUSY) {
            return rc == NRFX_SUCCESS;
        }
        k_msleep(1);
    } while (k_uptime_get() < deadline);
    return false;
}

static bool transfer_image(const struct base_image *image, bool program) {
    uint32_t crc = 0;

    reply(image, program ? "WRITE" : "PREFLIGHT", image->bytes);
    for (uint32_t offset = 0; offset < image->bytes; offset += CHUNK_BYTES) {
        const size_t size = MIN(CHUNK_BYTES, image->bytes - offset);

        if (!receive_chunk(size) || nrfx_qspi_read(readback, size, image->offset + offset) != NRFX_SUCCESS) {
            reply(image, "TRANSFER_ERROR", offset);
            return false;
        }
        crc = crc32_ieee_update(crc, incoming, size);
        // NOR writes may clear bits. A conflicting image needs a deliberate erase.
        for (size_t index = 0; index < size; index++) {
            if ((readback[index] & incoming[index]) != incoming[index]) {
                reply(image, "FLASH_CONFLICT", offset + index);
                return false;
            }
        }
        if (program) {
            for (size_t index = 0; index < size; index += PAGE_BYTES) {
                const size_t page = MIN(PAGE_BYTES, size - index);

                if (memcmp(readback + index, incoming + index, page) != 0 &&
                    (nrfx_qspi_write(incoming + index, page, image->offset + offset + index) != NRFX_SUCCESS ||
                     !wait_until_ready())) {
                    reply(image, "PROGRAM_ERROR", offset + index);
                    return false;
                }
            }
            if (nrfx_qspi_read(readback, size, image->offset + offset) != NRFX_SUCCESS ||
                memcmp(readback, incoming, size) != 0) {
                reply(image, "READBACK_ERROR", offset);
                return false;
            }
        }
        reply(image, "ACK", offset + size);
    }
    if (crc != image->crc) {
        reply(image, "TRANSFER_CRC_ERROR", crc);
        return false;
    }
    return true;
}
#endif /* SERIAL_PROVISIONING */

static bool prepare_flash(nrfx_qspi_config_t *config, bool *mapping_failed, bool quick) {
    *mapping_failed = false;
    config->prot_if.readoc = NRF_QSPI_READOC_FASTREAD;
    if (!start_qspi(config)) {
        return false;
    }
    /* The raw CRC pass reads every byte of both bases. On one data line it
     * was most of the cold-boot wait, so switch to four lines first.
     */
    if (!enable_quad_reads(config)) {
        for (size_t i = 0; i < ARRAY_SIZE(images); i++) {
            images[i].verified = false;
        }
        *mapping_failed = true;
        return true;
    }
    for (size_t i = 0; i < ARRAY_SIZE(images); i++) {
        struct base_image *image = &images[i];
        uint32_t magic = 0;
        image->verified =
            nrfx_qspi_read(&magic, sizeof(magic), image->offset) == NRFX_SUCCESS &&
            magic == JSC4_MAGIC && (quick || read_image_crc(image));
    }
    if (all_verified()) {
        *mapping_failed = !enable_verified_mapping(config, quick);
    }
    return true;
}

/* A wake from our own deep sleep checks the chip ID, image headers and native
 * lookups. A cold boot, or a quick check that fails, runs both CRC passes.
 */
static bool bring_up_flash(nrfx_qspi_config_t *config, bool *mapping_failed) {
    const int64_t started = k_uptime_get();
    bool quick = take_wake_trust();

    atomic_clear(&checked);
    bool qspi_ok = prepare_flash(config, mapping_failed, quick);

    if (quick && (!qspi_ok || *mapping_failed || !atomic_get(&mapped_ready))) {
        printk("Quick flash check failed; verifying in full\n");
        quick = false;
        trusted = false;
        if (nrfx_qspi_init_check()) {
            (void)reactivate_after_xip();
            nrfx_qspi_uninit();
        }
        qspi_ok = prepare_flash(config, mapping_failed, false);
    }
    printk("External flash %s after %s check in %d ms\n",
           atomic_get(&mapped_ready) ? "mapped" : "unavailable", quick ? "quick" : "full",
           (int)(k_uptime_get() - started));
    atomic_set(&checked, 1);
    lapwing_flash_ready_changed();
#if SERIAL_PROVISIONING
    last_check_quick = quick;
    last_check_ms = (uint32_t)(k_uptime_get() - started);
#endif
    return qspi_ok;
}

#if SERIAL_PROVISIONING

static void handle_command(uint8_t command, nrfx_qspi_config_t *config, bool qspi_ok,
                           bool *mapping_failed) {
    for (size_t i = 0; i < ARRAY_SIZE(images); i++) {
        struct base_image *image = &images[i];
        if (command != image->command && command != image->query) {
            continue;
        }
        if (!qspi_ok || *mapping_failed) {
            reply(image, "QSPI_ERROR", 0);
        } else if (lapwing_mapped_address()) {
            reply(image, "MAPPED", image->bytes);
        } else if (image->verified) {
            reply(image, "VERIFIED", image->bytes);
        } else if (command == image->query) {
            reply(image, "UNPROVISIONED", image->bytes);
        } else if (transfer_image(image, false) && transfer_image(image, true)) {
            image->verified = read_image_crc(image);
            if (!image->verified) {
                reply(image, "READBACK_ERROR", 0);
            } else if (all_verified()) {
                *mapping_failed = !enable_verified_mapping(config, false);
                reply(image, *mapping_failed ? "MAPPING_ERROR" : "MAPPED", image->bytes);
                lapwing_flash_ready_changed();
            } else {
                reply(image, "VERIFIED", image->bytes);
            }
        }
    }
}

#endif /* SERIAL_PROVISIONING */

static void lapwing_worker(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    nrfx_qspi_config_t config = NRFX_QSPI_DEFAULT_CONFIG(
        NRF_QSPI_PIN_NOT_CONNECTED, NRF_QSPI_PIN_NOT_CONNECTED, NRF_QSPI_PIN_NOT_CONNECTED,
        NRF_QSPI_PIN_NOT_CONNECTED, NRF_QSPI_PIN_NOT_CONNECTED, NRF_QSPI_PIN_NOT_CONNECTED);
    config.skip_gpio_cfg = true;
    config.skip_psel_cfg = true;
    /* 32 MHz / divider. The enum is the divider minus one. */
    BUILD_ASSERT(NRF_QSPI_FREQ_DIV1 == 0 && NRF_QSPI_FREQ_DIV16 == 15);
    config.phy_if.sck_freq =
        (nrf_qspi_frequency_t)(CONFIG_ZMK_JAVELIN_STENO_QSPI_SCK_DIVIDER - 1);

    bool mapping_failed = false;
#ifdef CONFIG_ZMK_SLEEP
    bool qspi_ok = false;
#else
    const bool qspi_ok = bring_up_flash(&config, &mapping_failed);
#endif

    for (;;) {
#ifdef CONFIG_ZMK_SLEEP
        if (atomic_get(&restart_requested)) {
            k_mutex_lock(&flash_lock, K_FOREVER);
            /* Several callers may ask for one wake. Only a powered-down flash
             * is brought up; a request that arrives late is already served.
             */
            if (atomic_cas(&restart_requested, 1, 0) && flash_suspended) {
                qspi_ok = bring_up_flash(&config, &mapping_failed);
                flash_suspended = false;
#if IDLE_OFF
                note_use();
                atomic_clear(&idle_off);
#endif
            }
            k_mutex_unlock(&flash_lock);
        }
#if IDLE_OFF
        if (atomic_get(&mapped_ready) &&
            k_uptime_get_32() - (uint32_t)atomic_get(&last_use) >= IDLE_OFF_MS) {
            k_mutex_lock(&flash_lock, K_FOREVER);
            /* Recheck under the lock: a lookup may have just used the flash. */
            const bool initialized = nrfx_qspi_init_check();
            if (atomic_get(&mapped_ready) && !flash_suspended &&
                k_uptime_get_32() - (uint32_t)atomic_get(&last_use) >= IDLE_OFF_MS &&
                flash_is_idle(initialized)) {
                if (power_down_flash(initialized) == 0) {
                    atomic_set(&idle_off, 1);
                    printk("External flash powered down while idle\n");
                } else {
                    request_restart();
                }
            }
            k_mutex_unlock(&flash_lock);
        }
#endif
#endif
#if SERIAL_PROVISIONING
        uint8_t command;
        if (uart_poll_in(console, &command) == 0) {
            if (command == 'W') {
                reply(&images[0], last_check_quick ? "CHECK_QUICK" : "CHECK_FULL",
                      last_check_ms);
                continue;
            }
#ifdef CONFIG_ZMK_JAVELIN_BATTERY_LOG
            if (command == 'B') {
                zen_battery_log_export(console);
                continue;
            }
#endif
#ifdef CONFIG_ZMK_SLEEP
            k_mutex_lock(&flash_lock, K_FOREVER);
#if IDLE_OFF
            /* A provisioning command needs the flash, so end an idle power-down. */
            if (flash_suspended && atomic_get(&idle_off)) {
                atomic_clear(&restart_requested);
                qspi_ok = bring_up_flash(&config, &mapping_failed);
                flash_suspended = false;
                atomic_clear(&idle_off);
            }
            note_use();
#endif
            if (!flash_suspended) {
                handle_command(command, &config, qspi_ok, &mapping_failed);
            }
            k_mutex_unlock(&flash_lock);
#else
            handle_command(command, &config, qspi_ok, &mapping_failed);
#endif
        }
        k_msleep(10);
#elif defined(CONFIG_ZMK_SLEEP)
        /* Nothing to poll. Sleep until something asks for the mapping again,
         * waking now and then only while an idle power-down is still due.
         */
        ARG_UNUSED(qspi_ok);
#if IDLE_OFF
        k_sem_take(&restart_wake, atomic_get(&mapped_ready)
                                      ? K_MSEC(MAX(IDLE_OFF_MS / 4U, 1000U)) : K_FOREVER);
#else
        k_sem_take(&restart_wake, K_FOREVER);
#endif
#else
        ARG_UNUSED(qspi_ok);
        return;
#endif
    }
}

K_THREAD_DEFINE(lapwing_flash, 2048, lapwing_worker, NULL, NULL, NULL, 5, 0, 200);
