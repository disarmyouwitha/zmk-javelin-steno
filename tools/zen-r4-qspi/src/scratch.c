#include "scratch.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define FLASH_BYTES (16U * 1024U * 1024U)
#define SECTOR_BYTES 4096U
#define SCRATCH_ADDRESS (FLASH_BYTES - SECTOR_BYTES)
#define PAGE_BYTES QUAD_SAMPLE_SIZE

static uint8_t page[PAGE_BYTES] __aligned(4);
static uint8_t readback[PAGE_BYTES] __aligned(4);

static nrfx_err_t wait_until_ready(void)
{
	int64_t deadline = k_uptime_get() + 2000;

	do {
		nrfx_err_t rc = nrfx_qspi_mem_busy_check();

		if (rc != NRFX_ERROR_BUSY) {
			return rc;
		}
		k_msleep(2);
	} while (k_uptime_get() < deadline);

	return NRFX_ERROR_TIMEOUT;
}

static nrfx_err_t verify_erased(bool *blank)
{
	*blank = false;
	for (uint32_t offset = 0; offset < SECTOR_BYTES; offset += sizeof(readback)) {
		nrfx_err_t rc = nrfx_qspi_read(readback, sizeof(readback), SCRATCH_ADDRESS + offset);

		if (rc != NRFX_SUCCESS) {
			return rc;
		}
		for (size_t i = 0; i < sizeof(readback); i++) {
			if (readback[i] != 0xff) {
				return NRFX_SUCCESS;
			}
		}
	}
	*blank = true;
	return NRFX_SUCCESS;
}

#ifndef JAVELIN_PROBE
static void fill_pattern(uint8_t pattern)
{
	for (size_t i = 0; i < sizeof(page); i++) {
		page[i] = (uint8_t)i ^ pattern;
	}
}
#endif

static const char *program_and_read(uint32_t address, nrfx_err_t *rc)
{
	printk("Programming %u bytes at 0x%06x (single-data-line Page Program)\n",
	       (unsigned int)sizeof(page), address);
	*rc = nrfx_qspi_write(page, sizeof(page), address);
	if (*rc != NRFX_SUCCESS) {
		return "FAIL: page program command";
	}
	*rc = wait_until_ready();
	if (*rc != NRFX_SUCCESS) {
		return "FAIL: waiting for page program";
	}
	*rc = nrfx_qspi_read(readback, sizeof(readback), address);
	if (*rc != NRFX_SUCCESS) {
		return "FAIL: programmed page read";
	}
	if (memcmp(page, readback, sizeof(page)) != 0) {
		for (size_t i = 0; i < sizeof(page); i++) {
			if (page[i] != readback[i]) {
				printk("Mismatch at 0x%06x: expected %02X, got %02X\n",
				       address + (unsigned int)i, page[i], readback[i]);
				break;
			}
		}
		return "FAIL: programmed page differs from expected pattern";
	}
	return NULL;
}

static const char *erase_and_verify(nrfx_qspi_config_t *config, nrfx_err_t *rc)
{
	bool blank;

	config->prot_if.readoc = NRF_QSPI_READOC_FASTREAD;
	*rc = nrfx_qspi_reconfigure(config);
	if (*rc != NRFX_SUCCESS) {
		return "FAIL: reconfiguring single-data-line reads";
	}
	*rc = wait_until_ready();
	if (*rc != NRFX_SUCCESS) {
		return "FAIL: waiting before sector erase";
	}
	printk("Erasing the 4 KiB scratch sector at 0x%06x\n", SCRATCH_ADDRESS);
	*rc = nrfx_qspi_erase(NRF_QSPI_ERASE_LEN_4KB, SCRATCH_ADDRESS);
	if (*rc != NRFX_SUCCESS) {
		return "FAIL: sector erase command";
	}
	*rc = wait_until_ready();
	if (*rc != NRFX_SUCCESS) {
		return "FAIL: waiting for sector erase";
	}
	*rc = verify_erased(&blank);
	if (*rc != NRFX_SUCCESS) {
		return "FAIL: reading sector after erase";
	}
	return blank ? "PASS: all 4,096 bytes verified as FF" : "FAIL: sector still contains data";
}

void run_scratch_probe(nrfx_qspi_config_t *config, struct scratch_result *result,
		       struct quad_result *quad)
{
	bool blank;

	printk("Checking all 4 KiB at 0x%06x before writing\n", SCRATCH_ADDRESS);
	result->error = verify_erased(&blank);
	if (result->error != NRFX_SUCCESS) {
		result->outcome = "FAIL: initial scratch-sector read";
		return;
	}
	if (!blank) {
		result->outcome = "SKIP: scratch sector contains data";
		return;
	}

#ifdef JAVELIN_PROBE
	const size_t fixture_size = javelin_fixture_end - javelin_fixture_start;
	if (fixture_size == 0 || fixture_size > sizeof(page)) {
		result->outcome = "FAIL: Javelin fixture does not fit scratch page";
		return;
	}
	memset(page, 0xff, sizeof(page));
	memcpy(page, javelin_fixture_start, fixture_size);
	printk("Staging %u-byte JSC4 fixture in the external scratch sector\n",
	       (unsigned int)fixture_size);
#else
	fill_pattern(SCRATCH_FIRST_PATTERN);
#endif
	result->outcome = program_and_read(SCRATCH_ADDRESS, &result->error);
#ifdef XIP_PROBE
	quad->mapped.second_address = SCRATCH_ADDRESS + SECTOR_BYTES - PAGE_BYTES;
	if (!result->outcome) {
		fill_pattern(SCRATCH_SECOND_PATTERN);
		result->outcome = program_and_read(quad->mapped.second_address, &result->error);
	}
#endif
	if (!result->outcome) {
#ifdef XIP_PROBE
		result->outcome = "PASS: both 256-byte programmed pages matched";
#else
		result->outcome = "PASS: all 256 programmed bytes matched";
#endif
		run_quad_probe(config, quad, SCRATCH_ADDRESS, sizeof(page));
	}
#ifdef XIP_PROBE
	if (quad->mapped.reactivation_failed) {
#elif defined(JAVELIN_PROBE)
	if (quad->javelin_reactivation_failed) {
#endif
#if defined(XIP_PROBE) || defined(JAVELIN_PROBE)
		result->cleanup = "SKIP: QSPI reactivation failed; scratch data may remain";
		result->cleanup_error = NRFX_ERROR_TIMEOUT;
		return;
	}
#endif
	result->cleanup = erase_and_verify(config, &result->cleanup_error);
}
