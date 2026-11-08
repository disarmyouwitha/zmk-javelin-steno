#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uintptr_t lapwing_mapped_address(void);
bool lapwing_verify_native_dictionary(uintptr_t address);

#ifdef __cplusplus
}
#endif
