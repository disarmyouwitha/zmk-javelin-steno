#pragma once

#include <stdbool.h>
#include <stdint.h>

// Independent base regions. Personal snapshots start at 0xF00000.
#define PLOVER_FLASH_OFFSET 0x200000U
// One image holding the smaller Lapwing dictionaries, each with its own
// checkbox in the browser.
#define EXTRAS_FLASH_OFFSET 0x500000U
#define BASE_DICTIONARY_LIMIT 0xF00000U

#ifdef __cplusplus
extern "C" {
#endif

uintptr_t lapwing_mapped_address(void);
bool lapwing_verify_native_dictionary(uintptr_t address);
bool plover_verify_native_dictionary(uintptr_t address);
bool extras_verify_native_dictionary(uintptr_t address);

enum lapwing_flash_state {
    LAPWING_FLASH_LOADING,     /* Still being checked after a boot or wake. */
    LAPWING_FLASH_READY,       /* Mapped, or idle and restored on first use. */
    LAPWING_FLASH_UNAVAILABLE, /* Missing, unprovisioned, failed or asleep. */
};
enum lapwing_flash_state lapwing_flash_status(void);

/* Hold the flash on while a layer that uses the dictionaries is selected.
 * Taking the hold starts a flash that powered down while idle. Releasing it
 * lets the idle period run again. Never blocks.
 */
void lapwing_flash_hold(bool hold);

/* Weak hook, called when the status above changes. It runs on the flash
 * worker with the flash lock held, so an override must only submit work.
 */
void lapwing_flash_ready_changed(void);

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
