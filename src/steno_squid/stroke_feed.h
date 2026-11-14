#pragma once
/* The stroke tape: the last stroke, its translation, a shorter-outline hint
 * and words per minute, sent from the left half to the right nice!view.
 *
 * This header has no Zephyr or engine dependencies, so the left half, the
 * right half and the host tests in tools/tests/test_stroke_feed.c all use the
 * same code. It is valid C and C++.
 *
 * Messages use the status behavior's two 32-bit parameters. Bit 30 of param2
 * tags a tape message (bit 31 stays clear, so it is never a layer name) and
 * bits 28 and 29 of param2 give the kind. A stroke is 23 key bits, in the
 * engine's own bit order, without its '^' and '+' extension keys.
 *
 *   STROKE  param1: stroke bits 0-22, seq bit 23, WPM bits 24-31
 *           param2: tag, kind 0, first four translation characters
 *   MORE    param1: characters 5-8, seq bit 28
 *           param2: tag, kind 1, characters 9-12
 *   HINT    param1: shorter outline's first stroke bits 0-22, seq bit 23,
 *                   its stroke count bits 24-26
 *           param2: tag, kind 2
 *
 * Characters are seven-bit ASCII, first in the lowest bits, zero at the end.
 * The sequence bit flips on every stroke, so a MORE or HINT that arrives after
 * a newer STROKE is recognised as stale and dropped.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define STENO_SQUID_TAPE_TAG (1u << 30)
#define STENO_SQUID_TAPE_KIND_SHIFT 28
#define STENO_SQUID_TAPE_PAYLOAD_MASK ((1u << STENO_SQUID_TAPE_KIND_SHIFT) - 1u)
#define STENO_SQUID_TAPE_KEYS_MASK 0x7fffffu
#define STENO_SQUID_TAPE_TEXT_MAX 12
#define STENO_SQUID_TAPE_RAW_MAX 64
#define STENO_SQUID_TAPE_STROKE_TEXT_MAX 24

enum steno_squid_tape_kind {
    STENO_SQUID_TAPE_KIND_STROKE,
    STENO_SQUID_TAPE_KIND_MORE,
    STENO_SQUID_TAPE_KIND_HINT,
};

/* Results of steno_squid_tape_decode() other than a kind. */
#define STENO_SQUID_TAPE_INVALID (-1)
#define STENO_SQUID_TAPE_STALE (-2)

struct steno_squid_tape {
    bool valid;
    uint8_t seq;
    uint8_t wpm;
    uint8_t hint_count; /* strokes in a shorter outline, 0 for none */
    uint32_t mask;
    uint32_t hint_mask; /* the shorter outline's first stroke */
    char text[STENO_SQUID_TAPE_TEXT_MAX + 1];
};

/* ---- Strokes as text ---------------------------------------------------- */

struct steno_squid_tape_key {
    char c;
    uint32_t mask;
};

/* The engine's stroke parser table, in its order. A '-' separates left and
 * right keys and carries no mask.
 */
static const struct steno_squid_tape_key steno_squid_tape_parser[] = {
    {'#', 0x1},      {'^', 0x800000}, {'+', 0x1000000}, {'1', 0x3},     {'S', 0x2},
    {'2', 0x5},      {'T', 0x4},      {'K', 0x8},       {'3', 0x11},    {'P', 0x10},
    {'W', 0x20},     {'4', 0x41},     {'H', 0x40},      {'R', 0x80},    {'5', 0x101},
    {'A', 0x100},    {'0', 0x201},    {'O', 0x200},     {'*', 0x400},   {'-', 0x0},
    {'E', 0x800},    {'U', 0x1000},   {'6', 0x2001},    {'F', 0x2000},  {'R', 0x4000},
    {'7', 0x8001},   {'P', 0x8000},   {'B', 0x10000},   {'8', 0x20001}, {'L', 0x20000},
    {'G', 0x40000},  {'9', 0x80001},  {'T', 0x80000},   {'S', 0x100000}, {'D', 0x200000},
    {'Z', 0x400000},
};

/* Parses a stroke the way the engine prints it, such as "KAT" or "-T". False
 * for text that is not a stroke.
 */
static inline bool steno_squid_stroke_from_text(const char *text, uint32_t *mask) {
    uint32_t value = 0;
    const char *p = text;
    for (size_t i = 0; i < sizeof(steno_squid_tape_parser) / sizeof(steno_squid_tape_parser[0]) && *p; i++) {
        if (steno_squid_tape_parser[i].c == *p) {
            value |= steno_squid_tape_parser[i].mask;
            p++;
        }
    }
    value &= STENO_SQUID_TAPE_KEYS_MASK;
    if (*p || value == 0) {
        return false;
    }
    *mask = value;
    return true;
}

/* Writes a stroke the way the engine prints it. `out` holds at least
 * STENO_SQUID_TAPE_STROKE_TEXT_MAX + 1 characters.
 */
static inline void steno_squid_stroke_to_text(uint32_t mask, char *out) {
    static const char left[] = "STKPWHR";
    static const char middle[] = "AO*EU";
    static const char right[] = "FRPBLGTSDZ";
    size_t n = 0;
    if (mask & 0x1u) {
        out[n++] = '#';
    }
    for (unsigned i = 0; i < 7; i++) {
        if (mask & (0x2u << i)) {
            out[n++] = left[i];
        }
    }
    if (mask & 0x1f00u) {
        for (unsigned i = 0; i < 5; i++) {
            if (mask & (0x100u << i)) {
                out[n++] = middle[i];
            }
        }
    } else if (mask & 0x7fe000u) {
        out[n++] = '-';
    }
    for (unsigned i = 0; i < 10; i++) {
        if (mask & (0x2000u << i)) {
            out[n++] = right[i];
        }
    }
    out[n] = '\0';
}

/* ---- Translations as display text -------------------------------------- */

static inline bool steno_squid_tape_is_command(const char *inner) {
    switch (inner[0]) {
    case ':': case '-': case '>': case '<': case '*': case '~': case '#':
        return true;
    default:
        return false;
    }
}

/* The characters between the braces of a dictionary entry such as "{^ing}",
 * with the attach markers removed. Returns false for text without braces.
 */
static inline bool steno_squid_tape_unbrace(const char *raw, char *inner, size_t cap) {
    const size_t length = strlen(raw);
    if (length < 2 || raw[0] != '{' || raw[length - 1] != '}') {
        return false;
    }
    size_t start = 1;
    size_t end = length - 1;
    while (start < end && (raw[start] == '^' || raw[start] == '&')) {
        start++;
    }
    while (end > start && raw[end - 1] == '^') {
        end--;
    }
    size_t n = end - start;
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(inner, raw + start, n);
    inner[n] = '\0';
    return true;
}

/* Turns a dictionary entry into what the display shows: braces and attach
 * markers removed, ASCII only. At most `cap - 1` characters.
 */
static inline void steno_squid_tape_clean_text(const char *raw, char *out, size_t cap) {
    char inner[STENO_SQUID_TAPE_RAW_MAX];
    const char *source = raw;
    if (steno_squid_tape_unbrace(raw, inner, sizeof(inner))) {
        source = inner[0] ? inner : "^";
    }
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)source; *p && n + 1 < cap; p++) {
        if (*p >= 0x80) {
            if (*p >= 0xc0) { /* one question mark per UTF-8 character */
                out[n++] = '?';
            }
        } else {
            out[n++] = *p < 0x20 || *p == 0x7f ? ' ' : (char)*p;
        }
    }
    out[n] = '\0';
}

/* How many characters this entry types, counting the space after a word.
 * Commands such as "{-|}" or "{:retro_shift}" type none. A rough figure, but
 * it is what a words-per-minute count needs.
 */
static inline unsigned steno_squid_tape_typed_chars(const char *raw) {
    char inner[STENO_SQUID_TAPE_RAW_MAX];
    char clean[STENO_SQUID_TAPE_RAW_MAX];
    if (steno_squid_tape_unbrace(raw, inner, sizeof(inner))) {
        if (!inner[0] || steno_squid_tape_is_command(inner)) {
            return 0;
        }
        steno_squid_tape_clean_text(inner, clean, sizeof(clean));
        return (unsigned)strlen(clean);
    }
    steno_squid_tape_clean_text(raw, clean, sizeof(clean));
    const size_t length = strlen(clean);
    return length ? (unsigned)length + 1 : 0;
}

/* ---- Messages ------------------------------------------------------------ */

static inline uint64_t steno_squid_tape_pack(const char *text, size_t start, size_t count) {
    uint64_t bits = 0;
    size_t length = 0;
    while (length < start + count && text[length]) {
        length++;
    }
    for (size_t i = 0; i < count && start + i < length; i++) {
        bits |= (uint64_t)((unsigned char)text[start + i] & 0x7f) << (7 * i);
    }
    return bits;
}

/* False when a character is not printable ASCII or follows the end. */
static inline bool steno_squid_tape_unpack(uint64_t bits, size_t count, char *out) {
    bool ended = false;
    for (size_t i = 0; i < count; i++) {
        const unsigned c = (unsigned)((bits >> (7 * i)) & 0x7f);
        if (c == 0) {
            ended = true;
        } else if (ended || c < 0x20 || c > 0x7e) {
            return false;
        }
        out[i] = (char)c;
    }
    return true;
}

static inline size_t steno_squid_tape_text_length(const struct steno_squid_tape *tape) {
    size_t n = 0;
    while (n < STENO_SQUID_TAPE_TEXT_MAX && tape->text[n]) {
        n++;
    }
    return n;
}

static inline void steno_squid_tape_encode_stroke(const struct steno_squid_tape *tape, uint32_t *param1,
                                             uint32_t *param2) {
    *param1 = (tape->mask & STENO_SQUID_TAPE_KEYS_MASK) | ((uint32_t)(tape->seq & 1u) << 23) |
              ((uint32_t)tape->wpm << 24);
    *param2 = STENO_SQUID_TAPE_TAG | ((uint32_t)STENO_SQUID_TAPE_KIND_STROKE << STENO_SQUID_TAPE_KIND_SHIFT) |
              (uint32_t)steno_squid_tape_pack(tape->text, 0, 4);
}

/* True when the text is longer than a STROKE message holds. */
static inline bool steno_squid_tape_needs_more(const struct steno_squid_tape *tape) {
    return steno_squid_tape_text_length(tape) > 4;
}

static inline void steno_squid_tape_encode_more(const struct steno_squid_tape *tape, uint32_t *param1,
                                           uint32_t *param2) {
    const uint64_t rest = steno_squid_tape_pack(tape->text, 4, 8);
    *param1 = (uint32_t)(rest & 0xfffffffu) | ((uint32_t)(tape->seq & 1u) << 28);
    *param2 = STENO_SQUID_TAPE_TAG | ((uint32_t)STENO_SQUID_TAPE_KIND_MORE << STENO_SQUID_TAPE_KIND_SHIFT) |
              (uint32_t)(rest >> 28);
}

static inline void steno_squid_tape_encode_hint(const struct steno_squid_tape *tape, uint32_t *param1,
                                           uint32_t *param2) {
    *param1 = (tape->hint_mask & STENO_SQUID_TAPE_KEYS_MASK) | ((uint32_t)(tape->seq & 1u) << 23) |
              ((uint32_t)(tape->hint_count & 7u) << 24);
    *param2 = STENO_SQUID_TAPE_TAG | ((uint32_t)STENO_SQUID_TAPE_KIND_HINT << STENO_SQUID_TAPE_KIND_SHIFT);
}

/* Applies one message to `tape`. Returns the message kind, or
 * STENO_SQUID_TAPE_INVALID for a malformed message, or STENO_SQUID_TAPE_STALE for a
 * well-formed MORE or HINT that belongs to a stroke that is no longer shown.
 * `tape` is only changed when a kind is returned.
 */
static inline int steno_squid_tape_decode(uint32_t param1, uint32_t param2, struct steno_squid_tape *tape) {
    if (!(param2 & STENO_SQUID_TAPE_TAG) || (param2 & (1u << 31))) {
        return STENO_SQUID_TAPE_INVALID;
    }
    const unsigned kind = (param2 >> STENO_SQUID_TAPE_KIND_SHIFT) & 3u;
    const uint32_t payload = param2 & STENO_SQUID_TAPE_PAYLOAD_MASK;
    switch (kind) {
    case STENO_SQUID_TAPE_KIND_STROKE: {
        char text[4];
        const uint32_t mask = param1 & STENO_SQUID_TAPE_KEYS_MASK;
        if (!mask || !steno_squid_tape_unpack(payload, 4, text)) {
            return STENO_SQUID_TAPE_INVALID;
        }
        memset(tape, 0, sizeof(*tape));
        tape->valid = true;
        tape->seq = (uint8_t)((param1 >> 23) & 1u);
        tape->wpm = (uint8_t)(param1 >> 24);
        tape->mask = mask;
        memcpy(tape->text, text, sizeof(text));
        return STENO_SQUID_TAPE_KIND_STROKE;
    }
    case STENO_SQUID_TAPE_KIND_MORE: {
        char text[8];
        const uint64_t bits = (uint64_t)(param1 & 0xfffffffu) | ((uint64_t)payload << 28);
        if ((param1 >> 29) || !steno_squid_tape_unpack(bits, 8, text)) {
            return STENO_SQUID_TAPE_INVALID;
        }
        if (!tape->valid || ((param1 >> 28) & 1u) != tape->seq ||
            steno_squid_tape_text_length(tape) != 4) {
            return STENO_SQUID_TAPE_STALE;
        }
        memcpy(tape->text + 4, text, sizeof(text));
        tape->text[STENO_SQUID_TAPE_TEXT_MAX] = '\0';
        return STENO_SQUID_TAPE_KIND_MORE;
    }
    case STENO_SQUID_TAPE_KIND_HINT: {
        const uint32_t mask = param1 & STENO_SQUID_TAPE_KEYS_MASK;
        const unsigned count = (param1 >> 24) & 7u;
        if (!mask || !count || (param1 >> 27) || payload) {
            return STENO_SQUID_TAPE_INVALID;
        }
        if (!tape->valid || ((param1 >> 23) & 1u) != tape->seq) {
            return STENO_SQUID_TAPE_STALE;
        }
        tape->hint_mask = mask;
        tape->hint_count = (uint8_t)count;
        return STENO_SQUID_TAPE_KIND_HINT;
    }
    default:
        return STENO_SQUID_TAPE_INVALID;
    }
}

/* ---- The engine's paper tape and suggestion events ----------------------- */

struct steno_squid_tape_event {
    bool stroke;   /* a paper tape event was found */
    bool undo;     /* the stroke undid earlier output */
    bool has_text; /* `raw` holds the translation */
    uint32_t mask;
    char raw[STENO_SQUID_TAPE_RAW_MAX];
    bool hint;
    uint32_t hint_mask;
    uint8_t hint_count;
};

/* A YAML scalar as the engine writes it: bare, or in double quotes with JSON
 * escapes. Copies it to `out`, stopping at `end`, at a closing quote, or when
 * `stop_at_comma` and a comma outside quotes. Returns the pointer after it.
 */
static inline const char *steno_squid_tape_scalar(const char *p, const char *end, bool stop_at_comma,
                                             char *out, size_t cap) {
    size_t n = 0;
    if (p < end && *p == '"') {
        p++;
        while (p < end && *p != '"') {
            char c = *p++;
            if (c == '\\' && p < end) {
                const char escaped = *p++;
                switch (escaped) {
                case 'n': case 'r': case 't': case 'b': case 'f':
                    c = ' ';
                    break;
                default:
                    c = escaped;
                    break;
                }
            }
            if (n + 1 < cap) {
                out[n++] = c;
            }
        }
        if (p < end) {
            p++;
        }
    } else {
        while (p < end && !(stop_at_comma && (*p == ',' || *p == ']'))) {
            if (n + 1 < cap) {
                out[n++] = *p;
            }
            p++;
        }
    }
    out[n] = '\0';
    return p;
}

static inline bool steno_squid_tape_starts_with(const char *line, const char *end, const char *prefix) {
    const size_t n = strlen(prefix);
    return (size_t)(end - line) >= n && memcmp(line, prefix, n) == 0;
}

/* Reads what the engine printed for one key event: the paper tape block for a
 * stroke ("EV e: p") and the first suggestion block ("EV e: s"), if any. False
 * when there was no stroke.
 */
static inline bool steno_squid_tape_parse(const char *buffer, size_t length,
                                     struct steno_squid_tape_event *event) {
    enum { NONE, PAPER, SUGGESTION } block = NONE;
    memset(event, 0, sizeof(*event));
    const char *p = buffer;
    const char *const limit = buffer + length;
    while (p < limit) {
        const char *end = (const char *)memchr(p, '\n', (size_t)(limit - p));
        const char *next = end ? end + 1 : limit;
        if (!end) {
            end = limit;
        }
        const char *line_end = end > p && end[-1] == '\r' ? end - 1 : end;
        if (line_end == p) {
            block = NONE;
        } else if (steno_squid_tape_starts_with(p, line_end, "EV e: p")) {
            memset(event, 0, sizeof(*event));
            event->stroke = true;
            block = PAPER;
        } else if (steno_squid_tape_starts_with(p, line_end, "EV e: s")) {
            block = SUGGESTION;
        } else if (block == PAPER && steno_squid_tape_starts_with(p, line_end, "o: ")) {
            char text[STENO_SQUID_TAPE_STROKE_TEXT_MAX + 8];
            steno_squid_tape_scalar(p + 3, line_end, false, text, sizeof(text));
            uint32_t mask;
            if (steno_squid_stroke_from_text(text, &mask)) {
                event->mask = mask;
            } else {
                event->stroke = false;
            }
        } else if (block == PAPER && steno_squid_tape_starts_with(p, line_end, "u: ")) {
            event->undo = p + 3 < line_end && p[3] != '0';
        } else if (block == PAPER && steno_squid_tape_starts_with(p, line_end, "t: ")) {
            steno_squid_tape_scalar(p + 3, line_end, false, event->raw, sizeof(event->raw));
            event->has_text = true;
        } else if (block == SUGGESTION && !event->hint &&
                   steno_squid_tape_starts_with(p, line_end, "o: ")) {
            /* One outline, or a list of them. Use the first. */
            const char *value = p + 3;
            if (value < line_end && *value == '[') {
                value++;
            }
            char outline[STENO_SQUID_TAPE_RAW_MAX];
            steno_squid_tape_scalar(value, line_end, true, outline, sizeof(outline));
            unsigned strokes = 1;
            char first[STENO_SQUID_TAPE_STROKE_TEXT_MAX + 8];
            size_t n = 0;
            bool in_first = true;
            for (const char *q = outline; *q; q++) {
                if (*q == '/') {
                    strokes++;
                    in_first = false;
                } else if (in_first && n + 1 < sizeof(first)) {
                    first[n++] = *q;
                }
            }
            first[n] = '\0';
            uint32_t mask;
            if (steno_squid_stroke_from_text(first, &mask)) {
                event->hint = true;
                event->hint_mask = mask;
                event->hint_count = (uint8_t)(strokes > 7 ? 7 : strokes);
            }
        }
        p = next;
    }
    return event->stroke;
}
