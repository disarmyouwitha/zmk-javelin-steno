#include "quad.h"
#ifdef TRACE_PROBE
#include "trace.h"
#endif
#ifdef JAVELIN_PROBE
#include "mapped.h"
#include <zephyr/devicetree.h>
#endif

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define QUAD_ENABLE BIT(1)
#define STATUS_BUSY_OR_WRITE_ENABLED (BIT(0) | BIT(1))
static uint8_t single_data[QUAD_SAMPLE_SIZE] __aligned(4);
static uint8_t quad_data[QUAD_SAMPLE_SIZE] __aligned(4);

static nrfx_err_t status_command(uint8_t opcode, const uint8_t *tx, uint8_t *rx)
{
	const nrf_qspi_cinstr_conf_t command = {
		.opcode = opcode,
		.length = tx || rx ? NRF_QSPI_CINSTR_LEN_2B : NRF_QSPI_CINSTR_LEN_1B,
		.io2_level = true,
		.io3_level = true,
		.wipwait = false,
		.wren = false,
	};

	return nrfx_qspi_cinstr_xfer(&command, tx, rx);
}

static nrfx_err_t set_volatile_sr2(uint8_t value)
{
	uint8_t status;
	nrfx_err_t rc = status_command(0x05, NULL, &status);

	if (rc != NRFX_SUCCESS) {
		return rc;
	}
	/* A preexisting WEL could turn the SR2 write into a nonvolatile write. */
	if (status & STATUS_BUSY_OR_WRITE_ENABLED) {
		return NRFX_ERROR_BUSY;
	}

	rc = status_command(0x50, NULL, NULL);
	if (rc != NRFX_SUCCESS) {
		return rc;
	}
	rc = status_command(0x31, &value, NULL);
	if (rc != NRFX_SUCCESS) {
		return rc;
	}

	k_msleep(2);
	rc = status_command(0x35, NULL, &status);
	if (rc != NRFX_SUCCESS) {
		return rc;
	}
	return status == value ? NRFX_SUCCESS : NRFX_ERROR_INTERNAL;
}

nrfx_err_t w25q128_enable_volatile_quad(void)
{
	uint8_t status;
	nrfx_err_t rc = status_command(0x35, NULL, &status);

	if (rc != NRFX_SUCCESS || (status & QUAD_ENABLE)) {
		return rc;
	}
	return set_volatile_sr2(status | QUAD_ENABLE);
}

static nrfx_err_t read_sample(nrfx_qspi_config_t *config, nrf_qspi_readoc_t mode,
			      uint32_t address, uint8_t *data)
{
	config->prot_if.readoc = mode;
	nrfx_err_t rc = nrfx_qspi_reconfigure(config);

	if (rc != NRFX_SUCCESS) {
		return rc;
	}
	return nrfx_qspi_read(data, sizeof(single_data), address);
}

static void report_quad_mismatch(uint32_t address)
{
	unsigned int mismatches = 0;
	uint8_t different = 0;
	uint8_t any_high = 0;
	uint8_t all_high = 0xf;

	for (size_t i = 0; i < sizeof(single_data); i++) {
		uint8_t actual = quad_data[i];
		uint8_t difference = single_data[i] ^ actual;

		if (difference && ++mismatches <= 8) {
			printk("Quad at 0x%06x: expected %02X, got %02X\n",
			       address + (unsigned int)i, single_data[i], actual);
		}
		different |= difference | (difference >> 4);
		any_high |= actual | (actual >> 4);
		all_high &= actual & (actual >> 4);
	}

	printk("Quad mismatches %u/%u; lane masks IO3..IO0: differ=0x%x always-high=0x%x always-low=0x%x\n",
	       mismatches, (unsigned int)sizeof(single_data), different & 0xf,
	       all_high, (unsigned int)(~any_high & 0xf));
}

void run_quad_probe(nrfx_qspi_config_t *config, struct quad_result *result,
		    uint32_t start, uint32_t length)
{
	uint8_t original_sr2;
	uint8_t seen_one = 0;
	uint8_t seen_zero = 0;

	result->restore_error = NRFX_SUCCESS;
	result->error = status_command(0x35, NULL, &original_sr2);
	if (result->error != NRFX_SUCCESS) {
		result->outcome = "FAIL: reading Status Register 2";
		return;
	}
	printk("Quad probe initial Status Register 2: 0x%02x\n", original_sr2);

	bool change_qe = (original_sr2 & QUAD_ENABLE) == 0;

	if (change_qe) {
		result->error = set_volatile_sr2(original_sr2 | QUAD_ENABLE);
		if (result->error != NRFX_SUCCESS) {
			result->outcome = "FAIL: setting or verifying volatile QE";
			result->restore_error = set_volatile_sr2(original_sr2);
			return;
		}
	}

	result->outcome = "INCONCLUSIVE: sampled data did not exercise both states on all lanes";
	for (uint32_t address = start; address < start + length; address += sizeof(single_data)) {
		result->error = read_sample(config, NRF_QSPI_READOC_FASTREAD, address, single_data);
		if (result->error != NRFX_SUCCESS) {
			result->outcome = "FAIL: single-data-line reference read";
			break;
		}
		result->error = read_sample(config, NRF_QSPI_READOC_READ4O, address, quad_data);
		if (result->error != NRFX_SUCCESS) {
			result->outcome = "FAIL: quad-output transfer";
			break;
		}
#ifdef TRACE_PROBE
		run_quad_trace(address, single_data);
#endif
		if (memcmp(single_data, quad_data, sizeof(single_data)) != 0) {
			report_quad_mismatch(address);
			nrfx_err_t dual_rc =
				read_sample(config, NRF_QSPI_READOC_READ2O, address, quad_data);
			if (dual_rc != NRFX_SUCCESS) {
				printk("Dual-output control transfer failed: nrfx rc=0x%08x\n",
				       (unsigned int)dual_rc);
			} else {
				printk("Dual-output control %s\n",
				       memcmp(single_data, quad_data, sizeof(single_data)) == 0
					       ? "matched" : "differed");
			}
			result->outcome = "FAIL: single and quad samples differ";
			break;
		}
		result->compared_bytes += sizeof(single_data);
		for (size_t i = 0; i < sizeof(single_data); i++) {
			uint8_t byte = single_data[i];

			seen_one |= byte | (byte >> 4);
			seen_zero |= (uint8_t)~byte | ((uint8_t)~byte >> 4);
		}
		result->lane_coverage = (seen_one & seen_zero) & 0xf;
		if (result->lane_coverage == 0xf) {
			result->outcome = "PASS: quad output matched reference with both states on IO0-IO3";
#ifdef XIP_PROBE
			run_mapped_probe(&result->mapped, address);
			if (result->mapped.reactivation_failed) {
				return;
			}
#endif
#ifdef JAVELIN_PROBE
			const uintptr_t mapped_address =
				DT_REG_ADDR_BY_NAME(DT_NODELABEL(qspi), qspi_mm) + address;
			printk("Opening external JSC4 at 0x%08x\n", (unsigned int)mapped_address);
			result->javelin_outcome = javelin_lookup_phrase(mapped_address, result->phrase);
			if (!reactivate_after_xip()) {
				result->javelin_reactivation_failed = true;
				return;
			}
#endif
			break;
		}
	}

	if (change_qe) {
		result->restore_error = set_volatile_sr2(original_sr2);
	}
}
