#include "lapwing_flash.h"
#include "quad.h"

#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
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
BUILD_ASSERT(LAPWING_BYTES <= FLASH_BYTES - SECTOR_BYTES);

PINCTRL_DT_DEFINE(QSPI_NODE);

static const struct gpio_dt_spec power =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), flash_power_gpios);
static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static atomic_t mapped_ready;
static uint8_t incoming[CHUNK_BYTES] __aligned(4);
static uint8_t readback[CHUNK_BYTES] __aligned(4);

uintptr_t lapwing_mapped_address(void) {
    return atomic_get(&mapped_ready) ? XIP_BASE : 0;
}

static bool start_qspi(nrfx_qspi_config_t *config) {
    if (!gpio_is_ready_dt(&power) || gpio_pin_configure_dt(&power, GPIO_OUTPUT_ACTIVE) != 0) {
        return false;
    }
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

static bool read_image_crc(void) {
    uint32_t crc = 0;

    for (uint32_t offset = 0; offset < LAPWING_BYTES; offset += CHUNK_BYTES) {
        const size_t size = MIN(CHUNK_BYTES, LAPWING_BYTES - offset);

        if (nrfx_qspi_read(readback, size, offset) != NRFX_SUCCESS) {
            return false;
        }
        crc = crc32_ieee_update(crc, readback, size);
        k_yield();
    }
    return crc == LAPWING_CRC;
}

static bool enable_verified_mapping(nrfx_qspi_config_t *config) {
    if (w25q128_enable_volatile_quad() != NRFX_SUCCESS) {
        return false;
    }
    config->prot_if.readoc = NRF_QSPI_READOC_READ4O;
    if (nrfx_qspi_reconfigure(config) != NRFX_SUCCESS ||
        nrfx_qspi_activate(true) != NRFX_SUCCESS) {
        return false;
    }

    uint32_t crc = 0;
    const volatile uint32_t *mapped = (const volatile uint32_t *)XIP_BASE;
    for (size_t offset = 0; offset < LAPWING_BYTES; offset += CHUNK_BYTES) {
        const size_t size = MIN(CHUNK_BYTES, LAPWING_BYTES - offset);

        for (size_t index = 0; index < size; index += sizeof(uint32_t)) {
            const uint32_t word = mapped[(offset + index) / sizeof(uint32_t)];
            __DSB();
            memcpy(readback + index, &word, sizeof(word));
        }
        crc = crc32_ieee_update(crc, readback, size);
        k_yield();
    }
    if (crc != LAPWING_CRC) {
        printk("Lapwing mapped CRC mismatch: %08x\n", crc);
        return false;
    }
    if (!lapwing_verify_native_dictionary(XIP_BASE)) {
        return false;
    }

    // No peripheral register access after entering mapped-read mode.
    atomic_set(&mapped_ready, 1);
    return true;
}

static void reply(const char *message, uint32_t value) {
    char line[64];

    snprintk(line, sizeof(line), "LAPWING %s %u\r\n", message, value);
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

static bool transfer_image(bool program) {
    uint32_t crc = 0;

    reply(program ? "WRITE" : "PREFLIGHT", LAPWING_BYTES);
    for (uint32_t offset = 0; offset < LAPWING_BYTES; offset += CHUNK_BYTES) {
        const size_t size = MIN(CHUNK_BYTES, LAPWING_BYTES - offset);

        if (!receive_chunk(size) || nrfx_qspi_read(readback, size, offset) != NRFX_SUCCESS) {
            reply("TRANSFER_ERROR", offset);
            return false;
        }
        crc = crc32_ieee_update(crc, incoming, size);
        // NOR writes may clear bits. A conflicting image needs a deliberate erase.
        for (size_t index = 0; index < size; index++) {
            if ((readback[index] & incoming[index]) != incoming[index]) {
                reply("FLASH_CONFLICT", offset + index);
                return false;
            }
        }
        if (program) {
            for (size_t index = 0; index < size; index += PAGE_BYTES) {
                const size_t page = MIN(PAGE_BYTES, size - index);

                if (memcmp(readback + index, incoming + index, page) != 0 &&
                    (nrfx_qspi_write(incoming + index, page, offset + index) != NRFX_SUCCESS ||
                     !wait_until_ready())) {
                    reply("PROGRAM_ERROR", offset + index);
                    return false;
                }
            }
            if (nrfx_qspi_read(readback, size, offset) != NRFX_SUCCESS ||
                memcmp(readback, incoming, size) != 0) {
                reply("READBACK_ERROR", offset);
                return false;
            }
        }
        reply("ACK", offset + size);
    }
    if (crc != LAPWING_CRC) {
        reply("TRANSFER_CRC_ERROR", crc);
        return false;
    }
    return true;
}

static void lapwing_worker(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    nrfx_qspi_config_t config = NRFX_QSPI_DEFAULT_CONFIG(
        NRF_QSPI_PIN_NOT_CONNECTED, NRF_QSPI_PIN_NOT_CONNECTED, NRF_QSPI_PIN_NOT_CONNECTED,
        NRF_QSPI_PIN_NOT_CONNECTED, NRF_QSPI_PIN_NOT_CONNECTED, NRF_QSPI_PIN_NOT_CONNECTED);
    config.skip_gpio_cfg = true;
    config.skip_psel_cfg = true;
    config.phy_if.sck_freq = NRF_QSPI_FREQ_DIV16;

    const bool qspi_ok = start_qspi(&config);
    uint32_t magic = 0;
    bool mapping_failed = false;
    if (qspi_ok && nrfx_qspi_read(&magic, sizeof(magic), 0) == NRFX_SUCCESS &&
        magic == JSC4_MAGIC && read_image_crc()) {
        mapping_failed = !enable_verified_mapping(&config);
    }

    for (;;) {
        uint8_t command;

        if (uart_poll_in(console, &command) == 0 && (command == 'L' || command == '?')) {
            if (!qspi_ok || mapping_failed) {
                reply("QSPI_ERROR", 0);
            } else if (lapwing_mapped_address()) {
                reply("MAPPED", LAPWING_BYTES);
            } else if (command == '?') {
                reply("UNPROVISIONED", LAPWING_BYTES);
            } else if (transfer_image(false) && transfer_image(true)) {
                mapping_failed = !enable_verified_mapping(&config);
                reply(mapping_failed ? "MAPPING_ERROR" : "MAPPED", LAPWING_BYTES);
            }
        }
        k_msleep(10);
    }
}

K_THREAD_DEFINE(lapwing_flash, 2048, lapwing_worker, NULL, NULL, NULL, 5, 0, 200);
