#pragma once

/* Diagnostics for the logging build only (CONFIG_ZMK_JAVELIN_DIAG).
 *
 * - diag_log() prints like printk and also keeps the line in a RAM ring that
 *   survives a soft reset, so start-up messages sent before the serial port
 *   was opened can still be read afterwards.
 * - diag_note() counts events. The rare ones are saved in settings, so they
 *   add up across reboots and show whether the retry paths ever run.
 * - wake timing measures how long an idle or resumed flash takes to come back.
 *
 * Everything here compiles away in other builds. To remove it for good, delete
 * diag.c, this file and the calls to diag_*() in lapwing_flash.c.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_ZMK_JAVELIN_DIAG

#include <zephyr/device.h>
#include <zephyr/toolchain.h>

enum diag_event {
    /* Saved across reboots. */
    DIAG_BOOT,
    DIAG_RETRY,          /* A start-up attempt failed before reading the images. */
    DIAG_POWER_CYCLE,    /* The flash rail was taken down between attempts. */
    DIAG_QUICK_FALLBACK, /* A quick check failed and a full check followed. */
    DIAG_UNAVAILABLE,    /* A bring-up ended with no usable dictionaries. */
    DIAG_ERASE,          /* An image erase over serial succeeded. */
    DIAG_PROGRAM,        /* An image was written over serial. */
    DIAG_PERSISTED_COUNT,
    /* Since this boot only. */
    DIAG_QUICK_CHECK = DIAG_PERSISTED_COUNT,
    DIAG_FULL_CHECK,
    DIAG_IDLE_DOWN,
    DIAG_EVENT_COUNT,
};

void diag_log(const char *fmt, ...) __printf_like(1, 2);
void diag_note(enum diag_event event);
/* How many times an event happened since this boot. */
uint32_t diag_session_count(enum diag_event event);
/* Call from the flash worker, on every pass of its loop. Saves counters. */
void diag_tick(void);
void diag_wake_requested(bool from_idle);
void diag_wake_ready(bool mapped, bool quick, uint32_t check_ms);

/* Serial output helpers for the status, dump and clear commands. */
void diag_printf(const struct device *console, const char *fmt, ...) __printf_like(2, 3);
void diag_print_status(const struct device *console);
/* USB power and battery level, to put a rail measurement in context. */
void diag_print_power(const struct device *console);
void diag_dump_log(const struct device *console);
void diag_clear(const struct device *console);

#else

#include <zephyr/sys/printk.h>

#define diag_log printk
enum diag_event {
    DIAG_BOOT, DIAG_RETRY, DIAG_POWER_CYCLE, DIAG_QUICK_FALLBACK, DIAG_UNAVAILABLE,
    DIAG_ERASE, DIAG_PROGRAM, DIAG_QUICK_CHECK, DIAG_FULL_CHECK, DIAG_IDLE_DOWN,
};
static inline void diag_note(enum diag_event event) { (void)event; }
static inline void diag_tick(void) {}
static inline void diag_wake_requested(bool from_idle) { (void)from_idle; }
static inline void diag_wake_ready(bool mapped, bool quick, uint32_t check_ms) {
    (void)mapped; (void)quick; (void)check_ms;
}

#endif
