#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ZEN_R4_STATUS_BEHAVIOR "zrst"
#define ZEN_R4_STATUS_PROTOCOL 1
#define ZEN_R4_PRACTICE_PROTOCOL 2
#define ZEN_R4_PRACTICE_ACTIVE (1u << 31)
#define ZEN_R4_PRACTICE_ROUNDS 10

enum zen_r4_practice_feedback {
    ZEN_R4_PRACTICE_PROMPT,
    ZEN_R4_PRACTICE_CORRECT,
    ZEN_R4_PRACTICE_INCORRECT,
    ZEN_R4_PRACTICE_DONE,
};

static const char *const zen_r4_practice_words[] = {"CAT", "THE", "IF", "IS"};

/* One complete screen snapshot fits in the existing split behavior packet.
 * Bits 0..1 word, 2..3 feedback, 4..7 round, 8..11 streak, 12..15 best.
 */
#ifdef __cplusplus
extern "C" {
#endif
void zen_r4_split_status_refresh(void);
void zen_r4_practice_set_active(bool active);
uint32_t zen_r4_practice_snapshot(void);
#ifdef __cplusplus
}
#endif

enum zen_r4_mode {
    ZEN_R4_MODE_UNKNOWN,
    ZEN_R4_MODE_TYPING,
    ZEN_R4_MODE_SYMBOLS,
    ZEN_R4_MODE_JAVELIN,
    ZEN_R4_MODE_PLOVER,
};
