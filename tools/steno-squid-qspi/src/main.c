#include <nrfx_qspi.h>

#ifdef QUAD_PROBE
#include "quad.h"
#endif
#ifdef SCRATCH_PROBE
#include "scratch.h"
#endif

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usb_device.h>

LOG_MODULE_REGISTER(qspi_probe);

#define QSPI_NODE DT_NODELABEL(qspi)

PINCTRL_DT_DEFINE(QSPI_NODE);

static const struct gpio_dt_spec flash_power =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), flash_power_gpios);
static const struct gpio_dt_spec status_led =
	GPIO_DT_SPEC_GET(DT_NODELABEL(blue_led), gpios);
static const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static const nrf_qspi_cinstr_conf_t read_id = {
	.opcode = 0x9f,
	.length = NRF_QSPI_CINSTR_LEN_4B,
	.io2_level = true,
	.io3_level = true,
	.wipwait = false,
	.wren = false,
};

struct probe_result {
	const char *failure;
	int gpio_or_pinctrl_error;
	nrfx_err_t nrfx_error;
	bool id_read;
	uint8_t id[3];
	uint8_t data[16] __aligned(4);
#ifdef QUAD_PROBE
	struct quad_result quad;
#endif
#ifdef SCRATCH_PROBE
	struct scratch_result scratch;
#endif
};

static void run_probe(struct probe_result *result)
{
	nrfx_qspi_config_t config = NRFX_QSPI_DEFAULT_CONFIG(
		NRF_QSPI_PIN_NOT_CONNECTED, NRF_QSPI_PIN_NOT_CONNECTED,
		NRF_QSPI_PIN_NOT_CONNECTED, NRF_QSPI_PIN_NOT_CONNECTED,
		NRF_QSPI_PIN_NOT_CONNECTED, NRF_QSPI_PIN_NOT_CONNECTED);

	/* Zephyr pinctrl supplies both the pin assignments and GPIO configuration. */
	config.skip_gpio_cfg = true;
	config.skip_psel_cfg = true;
	config.phy_if.sck_freq = NRF_QSPI_FREQ_DIV16;

	printk("Enabling switched flash VCC\n");
	if (!gpio_is_ready_dt(&flash_power)) {
		result->failure = "VCC GPIO device unavailable";
		return;
	}
	result->gpio_or_pinctrl_error = gpio_pin_configure_dt(&flash_power, GPIO_OUTPUT_ACTIVE);
	if (result->gpio_or_pinctrl_error) {
		result->failure = "VCC GPIO configuration";
		return;
	}
	k_msleep(50);

	printk("Applying QSPI pinctrl\n");
	result->gpio_or_pinctrl_error =
		pinctrl_apply_state(PINCTRL_DT_DEV_CONFIG_GET(QSPI_NODE), PINCTRL_STATE_DEFAULT);
	if (result->gpio_or_pinctrl_error) {
		result->failure = "QSPI pinctrl";
		return;
	}

	printk("Initializing Nordic QSPI (2 MHz, FASTREAD, 24-bit address)\n");
	result->nrfx_error = nrfx_qspi_init(&config, NULL, NULL);
	if (result->nrfx_error != NRFX_SUCCESS) {
		result->failure = "nrfx_qspi_init";
		return;
	}

	printk("Activating QSPI\n");
	result->nrfx_error = nrfx_qspi_activate(true);
	if (result->nrfx_error != NRFX_SUCCESS) {
		result->failure = "nrfx_qspi_activate";
		return;
	}

	printk("Reading JEDEC ID (0x9F)\n");
	result->nrfx_error = nrfx_qspi_cinstr_xfer(&read_id, NULL, result->id);
	if (result->nrfx_error != NRFX_SUCCESS) {
		result->failure = "JEDEC ID transfer";
		return;
	}
	result->id_read = true;

	if (result->id[0] != 0xef || result->id[1] != 0x40 || result->id[2] != 0x18) {
		result->failure = "unexpected JEDEC ID";
		return;
	}

	printk("Reading 16 bytes at 0x000000 with FASTREAD (0x0B)\n");
	result->nrfx_error = nrfx_qspi_read(result->data, sizeof(result->data), 0);
	if (result->nrfx_error != NRFX_SUCCESS) {
		result->failure = "FASTREAD transfer";
		return;
	}
#ifdef SCRATCH_PROBE
	run_scratch_probe(&config, &result->scratch, &result->quad);
#elif defined(QUAD_PROBE)
	run_quad_probe(&config, &result->quad, 0, 64 * 1024);
#endif
}

static void print_result(const struct probe_result *result)
{
	if (result->id_read) {
		printk("JEDEC ID: %02X %02X %02X (expected EF 40 18)\n",
		       result->id[0], result->id[1], result->id[2]);
	}
	if (result->failure) {
		printk("FAIL: %s; Zephyr rc=%d, nrfx rc=0x%08x\n",
		       result->failure, result->gpio_or_pinctrl_error, (unsigned int)result->nrfx_error);
		return;
	}

	printk("FASTREAD 0x000000:");
	for (size_t i = 0; i < sizeof(result->data); i++) {
		printk(" %02X", result->data[i]);
	}
#ifdef SCRATCH_PROBE
	printk("\nScratch %s (nrfx rc=0x%08x)\n",
	       result->scratch.outcome, (unsigned int)result->scratch.error);
	if (result->scratch.cleanup) {
		printk("Scratch cleanup %s (nrfx rc=0x%08x)\n",
		       result->scratch.cleanup, (unsigned int)result->scratch.cleanup_error);
	}
#endif
#ifdef QUAD_PROBE
	if (result->quad.outcome) {
		printk("\nQuad %s (compared %u bytes, lane mask 0x%x, nrfx rc=0x%08x)\n",
		       result->quad.outcome, result->quad.compared_bytes, result->quad.lane_coverage,
		       (unsigned int)result->quad.error);
#ifdef XIP_PROBE
		if (result->quad.mapped.outcome) {
			printk("XIP %s (matched %u words, nrfx rc=0x%08x)\n",
			       result->quad.mapped.outcome, result->quad.mapped.matched_words,
			       (unsigned int)result->quad.mapped.dma_error);
		}
#endif
#ifdef JAVELIN_PROBE
		if (result->quad.javelin_outcome) {
			printk("Javelin %s\n", result->quad.javelin_outcome);
			if (result->quad.phrase[0]) {
				printk("Javelin phrase: %s\n", result->quad.phrase);
			}
		}
#endif
		if (result->quad.restore_error != NRFX_SUCCESS) {
			printk("Quad SR2 restore failed: nrfx rc=0x%08x; power-cycle the flash\n",
			       (unsigned int)result->quad.restore_error);
		}
	}
#else
	printk("\nPASS: JEDEC ID and 16-byte single-data-line read completed\n");
#endif
}

int main(void)
{
	struct probe_result result = {.nrfx_error = NRFX_SUCCESS};
	uint32_t dtr = 0;
	bool led_ready = gpio_is_ready_dt(&status_led) &&
			 gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_ACTIVE) == 0;
	int rc = device_is_ready(console) ? usb_enable(NULL) : -ENODEV;

	if (rc) {
		LOG_ERR("USB CDC startup failed: %d", rc);
		for (;;) {
			if (led_ready) {
				gpio_pin_toggle_dt(&status_led);
			}
			k_msleep(120);
		}
	}

	while (!dtr) {
		rc = uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr);
		if (rc) {
			LOG_ERR("CDC DTR query failed: %d", rc);
		}
		k_msleep(100);
	}
	if (led_ready) {
		gpio_pin_set_dt(&status_led, 0);
	}

#ifdef TRACE_PROBE
	printk("\nSteno Squid QSPI trace ready. Arm the analyzer, then send t.\n");
	for (;;) {
		unsigned char key;

		if (uart_poll_in(console, &key) == 0 && key == 't') {
			break;
		}
		k_msleep(20);
	}
#elif defined(XIP_PROBE)
	printk("\nSteno Squid QSPI probe - mapped quad data reads\n");
#elif defined(JAVELIN_PROBE)
	printk("\nSteno Squid QSPI probe - native Javelin mapped dictionary\n");
#elif defined(SCRATCH_PROBE)
	printk("\nSteno Squid QSPI probe - scratch-sector program, read and erase\n");
#elif defined(QUAD_PROBE)
	printk("\n%s - quad output with volatile QE if needed\n", CONFIG_USB_DEVICE_PRODUCT);
#else
	printk("\n%s - read only\n", CONFIG_USB_DEVICE_PRODUCT);
#endif
	run_probe(&result);
	for (;;) {
		print_result(&result);
		k_sleep(K_SECONDS(3));
	}
}
