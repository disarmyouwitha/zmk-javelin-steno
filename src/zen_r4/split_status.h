#pragma once
#include <stdbool.h>
#include <stdint.h>

#define ZEN_R4_STATUS_BEHAVIOR "zrst"
#define ZEN_R4_STATUS_PROTOCOL 1
#define ZEN_R4_PRACTICE_PROTOCOL 2
/* A renamed layer, or one the firmware has no mode for, is shown by its name. The low byte of
 * the second parameter is this tag. The other 56 bits of the two parameters
 * hold up to eight 7-bit characters, the first in the lowest bits.
 */
#define ZEN_R4_LAYER_NAME_PROTOCOL 3
#define ZEN_R4_LAYER_NAME_CHARS 8
#define ZEN_R4_PRACTICE_ACTIVE (1u << 31)
#define ZEN_R4_PRACTICE_NO_DICTIONARY (1u << 16)
#define ZEN_R4_PRACTICE_DRILL_SHIFT 17
#define ZEN_R4_PRACTICE_DRILL_MASK (3u << ZEN_R4_PRACTICE_DRILL_SHIFT)
#define ZEN_R4_PRACTICE_ROUNDS 10

enum zen_r4_practice_drill {
    ZEN_R4_PRACTICE_WORDS,
    ZEN_R4_PRACTICE_LEFT,
    ZEN_R4_PRACTICE_RIGHT,
    ZEN_R4_PRACTICE_DRILL_COUNT,
};

enum zen_r4_practice_feedback {
    ZEN_R4_PRACTICE_PROMPT,
    ZEN_R4_PRACTICE_CORRECT,
    ZEN_R4_PRACTICE_INCORRECT,
    ZEN_R4_PRACTICE_DONE,
};

static const char *const zen_r4_practice_words[] = {"CAT", "THE", "IF", "IS"};
static const char *const zen_r4_practice_outlines[] = {"KAT", "-T", "TP", "S"};

static inline const char *zen_r4_practice_outline(enum zen_r4_practice_drill drill,
                                                 unsigned round) {
    static const char *const left[] = {"S", "T", "K", "P", "W", "H", "R"};
    static const char *const right[] = {"-F", "-R", "-P", "-B", "-L", "-G", "-T", "-S", "-D", "-Z"};
    if (drill == ZEN_R4_PRACTICE_LEFT) {
        return left[round % (sizeof(left) / sizeof(left[0]))];
    }
    if (drill == ZEN_R4_PRACTICE_RIGHT) {
        return right[round % (sizeof(right) / sizeof(right[0]))];
    }
    return zen_r4_practice_outlines[round % 4];
}

/* One complete screen snapshot fits in the existing split behavior packet.
 * Bits 0..1 word, 2..3 feedback, 4..7 round, 8..11 streak, 12..15 best,
 * bit 16 dictionary warning, bits 17..18 drill, bit 31 active.
 */
#ifdef __cplusplus
extern "C" {
#endif
void zen_r4_split_status_refresh(void);
void zen_r4_practice_set_active(bool active);
/* The practice key: on the steno layer it turns practice on or off. Anywhere
 * else it selects the steno layer with practice on.
 */
void zen_r4_practice_toggle(void);
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
    /* A steno or practice layer is selected, but the dictionaries are not usable. */
    ZEN_R4_MODE_LOADING,
    ZEN_R4_MODE_NO_DICTIONARY,
    ZEN_R4_MODE_COUNT,
};
