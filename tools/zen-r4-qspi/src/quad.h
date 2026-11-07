#ifndef ZEN_R4_QUAD_H
#define ZEN_R4_QUAD_H

#include <nrfx_qspi.h>
#ifdef XIP_PROBE
#include "mapped.h"
#endif
#ifdef JAVELIN_PROBE
#include "javelin_lookup.h"
#endif

struct quad_result {
	const char *outcome;
	nrfx_err_t error;
	nrfx_err_t restore_error;
	uint32_t compared_bytes;
	uint8_t lane_coverage;
#ifdef XIP_PROBE
	struct mapped_result mapped;
#endif
#ifdef JAVELIN_PROBE
	const char *javelin_outcome;
	char phrase[JAVELIN_PHRASE_SIZE];
	bool javelin_reactivation_failed;
#endif
};

#define QUAD_SAMPLE_SIZE 256

void run_quad_probe(nrfx_qspi_config_t *config, struct quad_result *result,
		    uint32_t start, uint32_t length);

#endif
