#ifndef STENO_SQUID_SCRATCH_H
#define STENO_SQUID_SCRATCH_H

#include "quad.h"

#define SCRATCH_FIRST_PATTERN 0xa5
#define SCRATCH_SECOND_PATTERN 0x3c

struct scratch_result {
	const char *outcome;
	const char *cleanup;
	nrfx_err_t error;
	nrfx_err_t cleanup_error;
};

void run_scratch_probe(nrfx_qspi_config_t *config, struct scratch_result *result,
		       struct quad_result *quad);

#endif
