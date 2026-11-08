#include "mapped.h"
#include "scratch.h"

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define XIP_BASE DT_REG_ADDR_BY_NAME(DT_NODELABEL(qspi), qspi_mm)
#define PAGE_WORDS (QUAD_SAMPLE_SIZE / sizeof(uint32_t))

static uint32_t dma_page[2][PAGE_WORDS];

void run_mapped_probe(struct mapped_result *result, uint32_t first_address)
{
	const uint32_t addresses[] = {first_address, result->second_address};
	const uint8_t patterns[] = {SCRATCH_FIRST_PATTERN, SCRATCH_SECOND_PATTERN};
	uintptr_t mismatch_address = 0;
	uint32_t mismatch_expected = 0;
	uint32_t mismatch_actual = 0;

	for (size_t page = 0; page < ARRAY_SIZE(addresses); page++) {
		result->dma_error = nrfx_qspi_read(dma_page[page], sizeof(dma_page[page]),
						 addresses[page]);
		if (result->dma_error != NRFX_SUCCESS) {
			result->outcome = "FAIL: quad EasyDMA reference read";
			return;
		}
		const uint8_t *reference = (const uint8_t *)dma_page[page];

		for (size_t byte = 0; byte < sizeof(dma_page[page]); byte++) {
			if (reference[byte] != ((uint8_t)byte ^ patterns[page])) {
				printk("Quad EasyDMA pattern mismatch at 0x%06x\n",
				       addresses[page] + (unsigned int)byte);
				result->outcome = "FAIL: quad EasyDMA reference differs from programmed pattern";
				return;
			}
		}
	}

	printk("Reading mapped quad pages at 0x%08x and 0x%08x\n",
	       (unsigned int)(XIP_BASE + addresses[0]), (unsigned int)(XIP_BASE + addresses[1]));
	for (size_t page = 0; page < ARRAY_SIZE(addresses); page++) {
		const volatile uint32_t *mapped =
			(const volatile uint32_t *)(XIP_BASE + addresses[page]);

		for (size_t word = 0; word < PAGE_WORDS; word++) {
			uint32_t actual = mapped[word];
			uint32_t expected = dma_page[page][word];

			if (actual == expected) {
				result->matched_words++;
			} else if (mismatch_address == 0) {
				mismatch_address = (uintptr_t)&mapped[word];
				mismatch_expected = expected;
				mismatch_actual = actual;
			}
		}
	}

	if (!reactivate_after_xip()) {
		result->reactivation_failed = true;
		result->outcome = "FAIL: QSPI did not become ready after mapped reads";
		return;
	}
	if (mismatch_address) {
		printk("Mapped mismatch at 0x%08x: expected %08x, got %08x\n",
		       (unsigned int)mismatch_address, mismatch_expected, mismatch_actual);
		result->outcome = "FAIL: mapped reads differ from quad EasyDMA";
		return;
	}
	result->outcome = "PASS: both mapped pages match quad EasyDMA";
}
