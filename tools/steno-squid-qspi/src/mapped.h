#ifndef STENO_SQUID_MAPPED_H
#define STENO_SQUID_MAPPED_H

#include <nrfx_qspi.h>

struct mapped_result {
	const char *outcome;
	uint32_t second_address;
	unsigned int matched_words;
	nrfx_err_t dma_error;
	bool reactivation_failed;
};

void run_mapped_probe(struct mapped_result *result, uint32_t first_address);

#endif
