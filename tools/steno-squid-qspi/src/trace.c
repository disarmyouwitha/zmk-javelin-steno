#include "trace.h"
#include "quad.h"

#include <hal/nrf_gpio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define TRACE_READS 8

static uint8_t trace_data[QUAD_SAMPLE_SIZE] __aligned(4);

static nrfx_err_t set_timing(nrf_gpio_pin_drive_t drive, uint8_t rx_delay)
{
	nrfx_err_t rc = nrfx_qspi_deactivate();
	nrf_qspi_pins_t pins;

	if (rc != NRFX_SUCCESS) {
		return rc;
	}
	nrf_qspi_pins_get(NRF_QSPI, &pins);
	const uint8_t bus_pins[] = {
		pins.sck_pin, pins.csn_pin, pins.io0_pin, pins.io1_pin, pins.io2_pin, pins.io3_pin,
	};

	for (size_t i = 0; i < ARRAY_SIZE(bus_pins); i++) {
		nrf_gpio_reconfigure(bus_pins[i], NULL, NULL, NULL, &drive, NULL);
	}
	nrf_qspi_iftiming_set(NRF_QSPI, rx_delay);
	return NRFX_SUCCESS;
}

void run_quad_trace(uint32_t address, const uint8_t *expected)
{
	const uint8_t initial_delay =
		(NRF_QSPI->IFTIMING & QSPI_IFTIMING_RXDELAY_Msk) >> QSPI_IFTIMING_RXDELAY_Pos;
	const struct {
		const char *label;
		nrf_gpio_pin_drive_t drive;
		uint8_t rx_delay;
	} settings[] = {
		{"standard drive", NRF_GPIO_PIN_S0S1, initial_delay},
		{"high drive", NRF_GPIO_PIN_H0H1, initial_delay},
		{"high drive, delayed sampling", NRF_GPIO_PIN_H0H1, 4},
	};

	for (size_t setting = 0; setting < ARRAY_SIZE(settings); setting++) {
		nrfx_err_t rc = set_timing(settings[setting].drive, settings[setting].rx_delay);
		unsigned int matched = 0;
		unsigned int mismatched_bytes = 0;

		printk("Trace %u: %s, RXDELAY=%u, %u reads from 0x%06x\n",
		       (unsigned int)setting + 1, settings[setting].label,
		       settings[setting].rx_delay, TRACE_READS, address);
		k_msleep(400);
		for (unsigned int trial = 0; trial < TRACE_READS && rc == NRFX_SUCCESS; trial++) {
			rc = nrfx_qspi_read(trace_data, sizeof(trace_data), address);
			if (rc != NRFX_SUCCESS) {
				break;
			}
			unsigned int mismatches = 0;

			for (size_t i = 0; i < sizeof(trace_data); i++) {
				mismatches += trace_data[i] != expected[i];
			}
			mismatched_bytes += mismatches;
			matched += mismatches == 0;
			k_msleep(20);
		}
		printk("Trace %u result: %u/%u exact reads, %u mismatched bytes, nrfx rc=0x%08x\n",
		       (unsigned int)setting + 1, matched, TRACE_READS, mismatched_bytes,
		       (unsigned int)rc);
	}

	nrfx_err_t rc = set_timing(NRF_GPIO_PIN_H0H1, initial_delay);

	if (rc != NRFX_SUCCESS) {
		printk("Trace timing restore failed: nrfx rc=0x%08x\n", (unsigned int)rc);
	}
}
