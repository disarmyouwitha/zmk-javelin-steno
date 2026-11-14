#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/toolchain.h>

#define STENO_SQUID_STATUS_BEHAVIOR "zrst"
#define STENO_SQUID_STATUS_PROTOCOL 1
#define STENO_SQUID_PRACTICE_LEGACY_PROTOCOL 4
#define STENO_SQUID_PRACTICE_RUN_PROTOCOL 5
#define STENO_SQUID_PRACTICE_PROTOCOL 6
/* A renamed layer uses the high bit of param2 as its tag. The remaining
 * 63 bits hold nine 7-bit characters, first character in the lowest bits.
 */
#define STENO_SQUID_LAYER_NAME_PROTOCOL (1u << 31)
#define STENO_SQUID_LAYER_NAME_CHARS 9
#define STENO_SQUID_PRACTICE_ACTIVE (1u << 31)
#define STENO_SQUID_PRACTICE_PROMPT_MASK 15u
#define STENO_SQUID_PRACTICE_FEEDBACK_SHIFT 4
#define STENO_SQUID_PRACTICE_ROUND_SHIFT 6
#define STENO_SQUID_PRACTICE_STREAK_SHIFT 12
#define STENO_SQUID_PRACTICE_BEST_SHIFT 18
#define STENO_SQUID_PRACTICE_COUNTER_MASK 63u
#define STENO_SQUID_PRACTICE_NO_DICTIONARY (1u << 24)
#define STENO_SQUID_PRACTICE_DRILL_SHIFT 25
#define STENO_SQUID_PRACTICE_DRILL_MASK (3u << STENO_SQUID_PRACTICE_DRILL_SHIFT)
#define STENO_SQUID_PRACTICE_MENU (1u << 27)
/* Local flag used by the receiver for old 30-prompt hand drills. */
#define STENO_SQUID_PRACTICE_LEGACY (1u << 30)
#define STENO_SQUID_PRACTICE_ROUNDS 50
BUILD_ASSERT(STENO_SQUID_PRACTICE_ROUNDS <= STENO_SQUID_PRACTICE_COUNTER_MASK,
             "A practice counter field cannot hold the round count");

enum steno_squid_practice_drill {
    STENO_SQUID_PRACTICE_WORDS,
    STENO_SQUID_PRACTICE_LEFT,
    STENO_SQUID_PRACTICE_RIGHT,
    STENO_SQUID_PRACTICE_DRILL_COUNT,
};

enum steno_squid_practice_feedback {
    STENO_SQUID_PRACTICE_PROMPT,
    STENO_SQUID_PRACTICE_CORRECT,
    STENO_SQUID_PRACTICE_INCORRECT,
    STENO_SQUID_PRACTICE_DONE,
};

static const char *const steno_squid_practice_words[] = {"CAT", "THE", "IF", "IS"};
static const char *const steno_squid_practice_outlines[] = {"KAT", "-T", "TP", "S"};

static const char *const steno_squid_practice_left[] = {"S", "T", "K", "P", "W", "H", "R"};
static const char *const steno_squid_practice_right[] = {
    "-F", "-R", "-P", "-B", "-L", "-G", "-T", "-S", "-D", "-Z",
};

static inline unsigned steno_squid_practice_prompt_count(enum steno_squid_practice_drill drill) {
    if (drill == STENO_SQUID_PRACTICE_LEFT) {
        return sizeof(steno_squid_practice_left) / sizeof(steno_squid_practice_left[0]);
    }
    if (drill == STENO_SQUID_PRACTICE_RIGHT) {
        return sizeof(steno_squid_practice_right) / sizeof(steno_squid_practice_right[0]);
    }
    return sizeof(steno_squid_practice_words) / sizeof(steno_squid_practice_words[0]);
}

static inline const char *steno_squid_practice_outline(enum steno_squid_practice_drill drill,
                                                 unsigned prompt) {
    if (drill == STENO_SQUID_PRACTICE_LEFT) {
        return steno_squid_practice_left[prompt];
    }
    if (drill == STENO_SQUID_PRACTICE_RIGHT) {
        return steno_squid_practice_right[prompt];
    }
    return steno_squid_practice_outlines[prompt];
}

static inline unsigned steno_squid_practice_rounds(enum steno_squid_practice_drill drill) {
    return drill == STENO_SQUID_PRACTICE_WORDS ? 10 : STENO_SQUID_PRACTICE_ROUNDS;
}

static inline unsigned steno_squid_practice_feedback_value(uint32_t snapshot) {
    return (snapshot >> STENO_SQUID_PRACTICE_FEEDBACK_SHIFT) & 3u;
}

static inline unsigned steno_squid_practice_counter(uint32_t snapshot, unsigned shift) {
    return (snapshot >> shift) & STENO_SQUID_PRACTICE_COUNTER_MASK;
}

#ifdef __cplusplus
extern "C" {
#endif
void steno_squid_split_status_refresh(void);
void steno_squid_practice_set_active(bool active);
/* The practice key: on the steno layer it turns practice on or off. Anywhere
 * else it selects the steno layer with practice on.
 */
void steno_squid_practice_toggle(void);
uint32_t steno_squid_practice_snapshot(void);

struct steno_squid_tape;
/* Left half: send the last stroke, its translation and the typing speed to the
 * right display. See stroke_feed.h for the messages.
 */
void steno_squid_tape_publish(const struct steno_squid_tape *tape);
/* Wrap one key event given to the engine: the engine's stroke events are
 * caught between the two calls and published.
 */
void steno_squid_stroke_feed_begin(void);
void steno_squid_stroke_feed_end(void);
#ifdef __cplusplus
}
#endif

enum steno_squid_mode {
    STENO_SQUID_MODE_UNKNOWN,
    STENO_SQUID_MODE_TYPING,
    STENO_SQUID_MODE_SYMBOLS,
    STENO_SQUID_MODE_JAVELIN,
    STENO_SQUID_MODE_PLOVER,
    /* A steno or practice layer is selected, but the dictionaries are not usable. */
    STENO_SQUID_MODE_LOADING,
    STENO_SQUID_MODE_NO_DICTIONARY,
    STENO_SQUID_MODE_COUNT,
};
