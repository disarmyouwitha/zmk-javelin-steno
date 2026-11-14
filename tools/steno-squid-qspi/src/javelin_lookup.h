#ifndef STENO_SQUID_JAVELIN_LOOKUP_H
#define STENO_SQUID_JAVELIN_LOOKUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t javelin_fixture_start[];
extern const uint8_t javelin_fixture_end[];

const char *javelin_lookup_phrase(uintptr_t address, char *phrase);
#define JAVELIN_PHRASE_SIZE 32

#ifdef __cplusplus
}
#endif

#endif
