#include "scratch.h"

#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zmk/hid.h>
#include <zmk/usb.h>

#define QSPI_NODE DT_NODELABEL(qspi)
PINCTRL_DT_DEFINE(QSPI_NODE);

static const struct gpio_dt_spec flash_power =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), flash_power_gpios);
static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static bool capturing;
static bool report_error;
static bool held;
static size_t report_count;
static size_t text_length;
static char captured[JAVELIN_PHRASE_SIZE];

int __real_hid_int_ep_write(const struct device *dev, const uint8_t *data,
                           uint32_t length, uint32_t *bytes_ret);

int __wrap_hid_int_ep_write(const struct device *dev, const uint8_t *data,
                           uint32_t length, uint32_t *bytes_ret)
{
    if (!capturing) {
        return __real_hid_int_ep_write(dev, data, length, bytes_ret);
    }
    if (length != sizeof(struct zmk_hid_keyboard_report) ||
        data[0] != ZMK_HID_REPORT_ID_KEYBOARD) {
        report_error = true;
        return -ENOTSUP;
    }

    const struct zmk_hid_keyboard_report *report = (const void *)data;
    const uint8_t usage = report->body.keys[0];
    ++report_count;
    for (size_t i = 1; i < sizeof(report->body.keys); i++) {
        report_error |= report->body.keys[i] != 0;
    }
    report_error |= report->body.modifiers != 0 || (usage != 0 && held);

    if (usage) {
        if (text_length >= sizeof(captured) - 1) {
            report_error = true;
        } else if (usage >= 4 && usage <= 29) {
            captured[text_length++] = 'a' + usage - 4;
        } else if (usage == 44) {
            captured[text_length++] = ' ';
        } else {
            report_error = true;
        }
    }
    held = usage != 0;

    // Submit a harmless key-up frame. Capture the original before replacing it.
    static const uint8_t neutral[sizeof(struct zmk_hid_keyboard_report)] = {
        ZMK_HID_REPORT_ID_KEYBOARD
    };
    int rc = __real_hid_int_ep_write(dev, neutral, sizeof(neutral), bytes_ret);
    report_error |= rc != 0;
    return rc;
}

void bench_begin_reports(void)
{
    capturing = true;
}

bool bench_end_reports(char *phrase)
{
    capturing = false;
    captured[text_length] = '\0';
    memcpy(phrase, captured, text_length + 1);
    printk("Captured %u ZMK keyboard reports at the USB write boundary\n",
           (unsigned int)report_count);
    return !report_error && !held && report_count == text_length * 2;
}

static bool start_qspi(void)
{
    if (!gpio_is_ready_dt(&flash_power) ||
        gpio_pin_configure_dt(&flash_power, GPIO_OUTPUT_ACTIVE) != 0) {
        return false;
    }
    k_msleep(50);
    if (pinctrl_apply_state(PINCTRL_DT_DEV_CONFIG_GET(QSPI_NODE), PINCTRL_STATE_DEFAULT) != 0) {
        return false;
    }
    nrfx_qspi_config_t config = NRFX_QSPI_DEFAULT_CONFIG(
        NRF_QSPI_PIN_NOT_CONNECTED, NRF_QSPI_PIN_NOT_CONNECTED,
        NRF_QSPI_PIN_NOT_CONNECTED, NRF_QSPI_PIN_NOT_CONNECTED,
        NRF_QSPI_PIN_NOT_CONNECTED, NRF_QSPI_PIN_NOT_CONNECTED);
    config.skip_gpio_cfg = true;
    config.skip_psel_cfg = true;
    if (nrfx_qspi_init(&config, NULL, NULL) != NRFX_SUCCESS) {
        return false;
    }

    const nrf_qspi_cinstr_conf_t read_id = {
        .opcode = 0x9f, .length = NRF_QSPI_CINSTR_LEN_4B,
        .io2_level = true, .io3_level = true,
    };
    uint8_t id[3];
    if (nrfx_qspi_cinstr_xfer(&read_id, NULL, id) != NRFX_SUCCESS) {
        return false;
    }
    printk("Bench JEDEC ID: %02x %02x %02x\n", id[0], id[1], id[2]);
    if (id[0] != 0xef || id[1] != 0x40 || id[2] != 0x18) {
        return false;
    }

    struct scratch_result scratch = {0};
    struct quad_result quad = {0};
    run_scratch_probe(&config, &scratch, &quad);
    printk("Bench scratch %s\n", scratch.outcome);
    if (quad.javelin_outcome) {
        printk("Bench Javelin %s\n", quad.javelin_outcome);
        printk("Bench report phrase: %s\n", quad.phrase);
    }
    if (scratch.cleanup) {
        printk("Bench cleanup %s\n", scratch.cleanup);
    }
    return true;
}

static void bench_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    uint32_t dtr = 0;
    do {
        k_msleep(100);
        uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr);
    } while (!dtr || !zmk_usb_is_hid_ready());

    printk("Zen R4 ZMK bench: USB connected, starting simulated chords\n");
    if (!start_qspi()) {
        printk("Bench FAIL: QSPI initialization or JEDEC ID\n");
    }
    printk("Bench finished\n");
}

K_THREAD_DEFINE(bench, 6144, bench_thread, NULL, NULL, NULL, 5, 0, 200);
