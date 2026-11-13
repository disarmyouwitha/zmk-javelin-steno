#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/toolchain.h>

#define ZEN_R4_STATUS_BEHAVIOR "zrst"
#define ZEN_R4_STATUS_PROTOCOL 1
#define ZEN_R4_PRACTICE_LEGACY_PROTOCOL 4
#define ZEN_R4_PRACTICE_PROTOCOL 5
/* A renamed layer uses the high bit of param2 as its tag. The remaining
 * 63 bits hold nine 7-bit characters, first character in the lowest bits.
 */
#define ZEN_R4_LAYER_NAME_PROTOCOL (1u << 31)
#define ZEN_R4_LAYER_NAME_CHARS 9
#define ZEN_R4_PRACTICE_ACTIVE (1u << 31)
#define ZEN_R4_PRACTICE_PROMPT_MASK 15u
#define ZEN_R4_PRACTICE_FEEDBACK_SHIFT 4
#define ZEN_R4_PRACTICE_ROUND_SHIFT 6
#define ZEN_R4_PRACTICE_STREAK_SHIFT 12
#define ZEN_R4_PRACTICE_BEST_SHIFT 18
#define ZEN_R4_PRACTICE_COUNTER_MASK 63u
#define ZEN_R4_PRACTICE_NO_DICTIONARY (1u << 24)
#define ZEN_R4_PRACTICE_DRILL_SHIFT 25
#define ZEN_R4_PRACTICE_DRILL_MASK (3u << ZEN_R4_PRACTICE_DRILL_SHIFT)
/* Local flag used by the receiver for old 30-prompt hand drills. */
#define ZEN_R4_PRACTICE_LEGACY (1u << 30)
#define ZEN_R4_PRACTICE_ROUNDS 50
BUILD_ASSERT(ZEN_R4_PRACTICE_ROUNDS <= ZEN_R4_PRACTICE_COUNTER_MASK,
             "A practice counter field cannot hold the round count");

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

static const char *const zen_r4_practice_left[] = {"S", "T", "K", "P", "W", "H", "R"};
static const char *const zen_r4_practice_right[] = {
    "-F", "-R", "-P", "-B", "-L", "-G", "-T", "-S", "-D", "-Z",
};

static inline unsigned zen_r4_practice_prompt_count(enum zen_r4_practice_drill drill) {
    if (drill == ZEN_R4_PRACTICE_LEFT) {
        return sizeof(zen_r4_practice_left) / sizeof(zen_r4_practice_left[0]);
    }
    if (drill == ZEN_R4_PRACTICE_RIGHT) {
        return sizeof(zen_r4_practice_right) / sizeof(zen_r4_practice_right[0]);
    }
    return sizeof(zen_r4_practice_words) / sizeof(zen_r4_practice_words[0]);
}

static inline const char *zen_r4_practice_outline(enum zen_r4_practice_drill drill,
                                                 unsigned prompt) {
    if (drill == ZEN_R4_PRACTICE_LEFT) {
        return zen_r4_practice_left[prompt];
    }
    if (drill == ZEN_R4_PRACTICE_RIGHT) {
        return zen_r4_practice_right[prompt];
    }
    return zen_r4_practice_outlines[prompt];
}

static inline unsigned zen_r4_practice_rounds(enum zen_r4_practice_drill drill) {
    return drill == ZEN_R4_PRACTICE_WORDS ? 10 : ZEN_R4_PRACTICE_ROUNDS;
}

static inline unsigned zen_r4_practice_feedback_value(uint32_t snapshot) {
    return (snapshot >> ZEN_R4_PRACTICE_FEEDBACK_SHIFT) & 3u;
}

static inline unsigned zen_r4_practice_counter(uint32_t snapshot, unsigned shift) {
    return (snapshot >> shift) & ZEN_R4_PRACTICE_COUNTER_MASK;
}

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
