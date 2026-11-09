#pragma once

#include <stdbool.h>
#include <stdint.h>

// Independent base regions. Personal snapshots start at 0xF00000.
#define PLOVER_FLASH_OFFSET 0x200000U
#define BASE_DICTIONARY_LIMIT 0xF00000U

#ifdef __cplusplus
extern "C" {
#endif

uintptr_t lapwing_mapped_address(void);
bool lapwing_verify_native_dictionary(uintptr_t address);
bool plover_verify_native_dictionary(uintptr_t address);

#ifdef CONFIG_ZMK_JAVELIN_BATTERY_LOG
bool lapwing_flash_is_powered(void);
#endif

#ifdef CONFIG_ZMK_SLEEP
/* Hold across mapped reads and personal dictionary writes. */
bool lapwing_flash_acquire(void);
void lapwing_flash_release(void);
#endif

#ifdef __cplusplus
}
#endif
