/* Host tests for the stroke tape codec in src/zen_r4/stroke_feed.h.
 *
 *   cc -std=c11 -Wall -Wextra -Werror -o test_stroke_feed tools/tests/test_stroke_feed.c
 *   ./test_stroke_feed
 *
 * CI builds it as C and as C++.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/zen_r4/stroke_feed.h"

static unsigned checks;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition);                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

#define CHECK_STR(actual, expected)                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (strcmp((actual), (expected)) != 0) {                                                   \
            fprintf(stderr, "%s:%d: got \"%s\", expected \"%s\"\n", __FILE__, __LINE__, (actual),  \
                    (expected));                                                                   \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static uint32_t parse(const char *text) {
    uint32_t mask = 0;
    CHECK(zen_r4_stroke_from_text(text, &mask));
    return mask;
}

static void test_strokes(void) {
    char text[ZEN_R4_TAPE_STROKE_TEXT_MAX + 1];

    zen_r4_stroke_to_text(parse("KAT"), text);
    CHECK_STR(text, "KAT");
    zen_r4_stroke_to_text(parse("-T"), text);
    CHECK_STR(text, "-T");
    zen_r4_stroke_to_text(parse("H-L"), text);
    CHECK_STR(text, "H-L");
    zen_r4_stroke_to_text(parse("*"), text);
    CHECK_STR(text, "*");
    zen_r4_stroke_to_text(parse("STKPWHRAO*EUFRPBLGTSDZ"), text);
    CHECK_STR(text, "STKPWHRAO*EUFRPBLGTSDZ");
    zen_r4_stroke_to_text(0x7fffffu, text);
    CHECK_STR(text, "#STKPWHRAO*EUFRPBLGTSDZ");

    /* Numbers are the number bar plus the key. */
    CHECK(parse("1") == 0x3u);
    CHECK(parse("12") == 0x7u);
    zen_r4_stroke_to_text(0x7u, text);
    CHECK_STR(text, "#ST");

    /* Not strokes. */
    uint32_t mask = 0;
    CHECK(!zen_r4_stroke_from_text("", &mask));
    CHECK(!zen_r4_stroke_from_text("kat", &mask));
    CHECK(!zen_r4_stroke_from_text("TKAT-", &mask));
    CHECK(!zen_r4_stroke_from_text("{^ing}", &mask));
    CHECK(!zen_r4_stroke_from_text("^", &mask)); /* an extension key alone */

    /* Every possible stroke survives writing and reading back. This is what
     * checks the hyphen rule against the parser's order.
     */
    for (uint32_t m = 1; m <= ZEN_R4_TAPE_KEYS_MASK; m++) {
        zen_r4_stroke_to_text(m, text);
        uint32_t back = 0;
        if (!zen_r4_stroke_from_text(text, &back) || back != m) {
            fprintf(stderr, "stroke %06x wrote \"%s\" and read back %06x\n", m, text, back);
            exit(1);
        }
    }
    checks++;
}

static void test_text(void) {
    char out[ZEN_R4_TAPE_TEXT_MAX + 1];

    zen_r4_tape_clean_text("cat", out, sizeof(out));
    CHECK_STR(out, "cat");
    zen_r4_tape_clean_text("{^ing}", out, sizeof(out));
    CHECK_STR(out, "ing");
    zen_r4_tape_clean_text("{.}", out, sizeof(out));
    CHECK_STR(out, ".");
    zen_r4_tape_clean_text("{^}", out, sizeof(out));
    CHECK_STR(out, "^");
    zen_r4_tape_clean_text("{-|}", out, sizeof(out));
    CHECK_STR(out, "-|");
    zen_r4_tape_clean_text("{:retro_shift:1}", out, sizeof(out));
    CHECK_STR(out, ":retro_shift");
    zen_r4_tape_clean_text("caf\xc3\xa9", out, sizeof(out));
    CHECK_STR(out, "caf?");
    zen_r4_tape_clean_text("a\tb", out, sizeof(out));
    CHECK_STR(out, "a b");
    zen_r4_tape_clean_text("", out, sizeof(out));
    CHECK_STR(out, "");

    CHECK(zen_r4_tape_typed_chars("cat") == 4);
    CHECK(zen_r4_tape_typed_chars("the cat") == 8);
    CHECK(zen_r4_tape_typed_chars("{^ing}") == 3);
    CHECK(zen_r4_tape_typed_chars("{&a}") == 1);
    CHECK(zen_r4_tape_typed_chars("{.}") == 1);
    CHECK(zen_r4_tape_typed_chars("{-|}") == 0);
    CHECK(zen_r4_tape_typed_chars("{:retro_shift:1}") == 0);
    CHECK(zen_r4_tape_typed_chars("{>}") == 0);
    CHECK(zen_r4_tape_typed_chars("") == 0);
}

static void make_tape(struct zen_r4_tape *tape, uint32_t mask, const char *text, unsigned seq,
                      unsigned wpm) {
    memset(tape, 0, sizeof(*tape));
    tape->valid = true;
    tape->seq = (uint8_t)seq;
    tape->wpm = (uint8_t)wpm;
    tape->mask = mask;
    strncpy(tape->text, text, ZEN_R4_TAPE_TEXT_MAX);
}

/* Sends `sent` through the messages and returns what a display would hold. */
static void deliver(const struct zen_r4_tape *sent, struct zen_r4_tape *shown) {
    uint32_t p1, p2;
    zen_r4_tape_encode_stroke(sent, &p1, &p2);
    CHECK((p2 & (1u << 31)) == 0);
    CHECK(zen_r4_tape_decode(p1, p2, shown) == ZEN_R4_TAPE_KIND_STROKE);
    if (zen_r4_tape_needs_more(sent)) {
        zen_r4_tape_encode_more(sent, &p1, &p2);
        CHECK((p2 & (1u << 31)) == 0);
        CHECK(zen_r4_tape_decode(p1, p2, shown) == ZEN_R4_TAPE_KIND_MORE);
    }
    if (sent->hint_count) {
        zen_r4_tape_encode_hint(sent, &p1, &p2);
        CHECK((p2 & (1u << 31)) == 0);
        CHECK(zen_r4_tape_decode(p1, p2, shown) == ZEN_R4_TAPE_KIND_HINT);
    }
}

static void test_messages(void) {
    static const char *const texts[] = {
        "", "a", "cat", "four", "fives", "sixsix", "seven77", "eightchr", "ninechars",
        "tenchars10", "elevenchar1", "twelve chars", "thirteen char",
    };
    for (unsigned t = 0; t < sizeof(texts) / sizeof(texts[0]); t++) {
        for (unsigned seq = 0; seq < 2; seq++) {
            struct zen_r4_tape sent, shown;
            make_tape(&sent, parse("TKPW"), texts[t], seq, t * 20);
            memset(&shown, 0, sizeof(shown));
            deliver(&sent, &shown);
            CHECK(shown.valid);
            CHECK(shown.seq == seq);
            CHECK(shown.mask == sent.mask);
            CHECK(shown.wpm == sent.wpm);
            CHECK(shown.hint_count == 0);
            CHECK(strcmp(shown.text, sent.text) == 0);
        }
    }

    /* Hints, and the full range of WPM. */
    for (unsigned wpm = 0; wpm < 256; wpm++) {
        struct zen_r4_tape sent, shown;
        make_tape(&sent, 0x7fffffu, "ninechars", wpm & 1, wpm);
        sent.hint_mask = parse("KAT");
        sent.hint_count = (uint8_t)(1 + wpm % 7);
        memset(&shown, 0, sizeof(shown));
        deliver(&sent, &shown);
        CHECK(shown.wpm == wpm);
        CHECK(shown.hint_mask == sent.hint_mask);
        CHECK(shown.hint_count == sent.hint_count);
        CHECK_STR(shown.text, "ninechars");
    }

    /* A new stroke clears the old hint and text. */
    struct zen_r4_tape shown, sent;
    make_tape(&sent, parse("KAT"), "cat", 0, 40);
    sent.hint_mask = parse("TKPW");
    sent.hint_count = 2;
    memset(&shown, 0, sizeof(shown));
    deliver(&sent, &shown);
    CHECK(shown.hint_count == 2);
    make_tape(&sent, parse("-T"), "the", 1, 41);
    deliver(&sent, &shown);
    CHECK(shown.hint_count == 0);
    CHECK_STR(shown.text, "the");

    /* Late messages for an earlier stroke are recognised and dropped. */
    struct zen_r4_tape older;
    make_tape(&older, parse("TKPW"), "longer word", 0, 10);
    older.hint_mask = parse("KAT");
    older.hint_count = 1;
    make_tape(&sent, parse("KAT"), "cat", 1, 12);
    uint32_t p1, p2;
    zen_r4_tape_encode_stroke(&sent, &p1, &p2);
    CHECK(zen_r4_tape_decode(p1, p2, &shown) == ZEN_R4_TAPE_KIND_STROKE);
    zen_r4_tape_encode_more(&older, &p1, &p2);
    CHECK(zen_r4_tape_decode(p1, p2, &shown) == ZEN_R4_TAPE_STALE);
    zen_r4_tape_encode_hint(&older, &p1, &p2);
    CHECK(zen_r4_tape_decode(p1, p2, &shown) == ZEN_R4_TAPE_STALE);
    CHECK_STR(shown.text, "cat");
    CHECK(shown.hint_count == 0);

    /* Nothing to continue or hint before any stroke. */
    struct zen_r4_tape empty;
    memset(&empty, 0, sizeof(empty));
    zen_r4_tape_encode_more(&older, &p1, &p2);
    CHECK(zen_r4_tape_decode(p1, p2, &empty) == ZEN_R4_TAPE_STALE);
    zen_r4_tape_encode_hint(&older, &p1, &p2);
    CHECK(zen_r4_tape_decode(p1, p2, &empty) == ZEN_R4_TAPE_STALE);
    CHECK(!empty.valid);
}

static void test_rejects(void) {
    struct zen_r4_tape tape, before;
    struct zen_r4_tape sent;
    make_tape(&sent, parse("KAT"), "cat", 0, 30);
    uint32_t p1, p2;
    zen_r4_tape_encode_stroke(&sent, &p1, &p2);
    make_tape(&tape, parse("S"), "x", 1, 1);
    before = tape;

    CHECK(zen_r4_tape_decode(p1, p2 & ~ZEN_R4_TAPE_TAG, &tape) == ZEN_R4_TAPE_INVALID);
    CHECK(zen_r4_tape_decode(p1, p2 | (1u << 31), &tape) == ZEN_R4_TAPE_INVALID);
    CHECK(zen_r4_tape_decode(p1 & ~ZEN_R4_TAPE_KEYS_MASK, p2, &tape) == ZEN_R4_TAPE_INVALID);
    CHECK(zen_r4_tape_decode(p1, ZEN_R4_TAPE_TAG | (3u << ZEN_R4_TAPE_KIND_SHIFT), &tape) ==
          ZEN_R4_TAPE_INVALID);
    /* A control character in the text. */
    CHECK(zen_r4_tape_decode(p1, ZEN_R4_TAPE_TAG | 0x07u, &tape) == ZEN_R4_TAPE_INVALID);
    /* Text after the end of the text. */
    CHECK(zen_r4_tape_decode(p1, ZEN_R4_TAPE_TAG | ('a' << 7), &tape) == ZEN_R4_TAPE_INVALID);
    /* A hint with no stroke count, and one with reserved bits. */
    const uint32_t hint = ZEN_R4_TAPE_TAG | ((uint32_t)ZEN_R4_TAPE_KIND_HINT << ZEN_R4_TAPE_KIND_SHIFT);
    CHECK(zen_r4_tape_decode(0x3u | (1u << 23), hint, &tape) == ZEN_R4_TAPE_INVALID);
    CHECK(zen_r4_tape_decode(0x3u | (1u << 23) | (1u << 24) | (1u << 28), hint, &tape) ==
          ZEN_R4_TAPE_INVALID);
    /* Practice and layer messages are not tape messages. */
    CHECK(zen_r4_tape_decode(0x1234u, 6u, &tape) == ZEN_R4_TAPE_INVALID);
    CHECK(zen_r4_tape_decode(0x1234u, 1u << 31, &tape) == ZEN_R4_TAPE_INVALID);
    /* Untouched by every reject. */
    CHECK(tape.valid == before.valid && tape.seq == before.seq && tape.mask == before.mask &&
          tape.wpm == before.wpm && tape.hint_count == before.hint_count &&
          strcmp(tape.text, before.text) == 0);
}

static void test_parse(void) {
    struct zen_r4_tape_event event;

    /* What the engine prints for a plain word. */
    const char word[] = "EV e: p\no: KAT\nd: main.json\nt: cat\n\n";
    CHECK(zen_r4_tape_parse(word, strlen(word), &event));
    CHECK(event.mask == parse("KAT"));
    CHECK(event.has_text);
    CHECK_STR(event.raw, "cat");
    CHECK(!event.undo);
    CHECK(!event.hint);

    /* Text with braces comes quoted, with escapes. */
    const char suffix[] = "EV e: p\r\no: -G\r\nt: \"{^ing}\"\r\n\r\n";
    CHECK(zen_r4_tape_parse(suffix, strlen(suffix), &event));
    CHECK(event.mask == parse("-G"));
    CHECK_STR(event.raw, "{^ing}");
    const char quoted[] = "EV e: p\no: TP-PL\nt: \"say \\\"hi\\\"\"\n\n";
    CHECK(zen_r4_tape_parse(quoted, strlen(quoted), &event));
    CHECK_STR(event.raw, "say \"hi\"");

    /* A stroke that replaced earlier output. */
    const char replaced[] = "EV e: p\no: KAT\nu: 2\nt: cat\n\n";
    CHECK(zen_r4_tape_parse(replaced, strlen(replaced), &event));
    CHECK(event.undo);
    CHECK(event.has_text);

    /* The undo stroke itself has no translation. */
    const char undo[] = "EV e: p\no: *\nu: 1\n\n";
    CHECK(zen_r4_tape_parse(undo, strlen(undo), &event));
    CHECK(event.mask == parse("*"));
    CHECK(event.undo);
    CHECK(!event.has_text);

    /* A suggestion follows the paper tape. Take the first outline. */
    const char hinted[] = "EV e: p\no: TKPW\nt: again\n\nEV e: s\nc: 2\nt: again\no: TKPWEPB\n\n";
    CHECK(zen_r4_tape_parse(hinted, strlen(hinted), &event));
    CHECK(event.hint);
    CHECK(event.hint_mask == parse("TKPWEPB"));
    CHECK(event.hint_count == 1);
    const char two[] = "EV e: p\no: TKPW\nt: x\n\nEV e: s\nc: 3\nt: x\no: \"TKPW/AEUPB\"\n\n";
    CHECK(zen_r4_tape_parse(two, strlen(two), &event));
    CHECK(event.hint_mask == parse("TKPW"));
    CHECK(event.hint_count == 2);
    const char list[] = "EV e: p\no: TKPW\nt: x\n\nEV e: s\nc: 3\nt: x\no: [\"KAT\",KA/T,\"-T\"]\n\n";
    CHECK(zen_r4_tape_parse(list, strlen(list), &event));
    CHECK(event.hint_mask == parse("KAT"));
    CHECK(event.hint_count == 1);
    /* Later suggestions do not replace the first. */
    const char many[] = "EV e: p\no: S\nt: x\n\nEV e: s\nc: 1\nt: x\no: KAT\n\nEV e: s\nc: 2\nt: y\no: -T\n\n";
    CHECK(zen_r4_tape_parse(many, strlen(many), &event));
    CHECK(event.hint_mask == parse("KAT"));

    /* Other output and unusable events. */
    const char nothing[] = "OK\n\nERR something\n\n";
    CHECK(!zen_r4_tape_parse(nothing, strlen(nothing), &event));
    CHECK(!zen_r4_tape_parse("", 0, &event));
    const char garbled[] = "EV e: p\no: kat\nt: cat\n\n";
    CHECK(!zen_r4_tape_parse(garbled, strlen(garbled), &event));
    const char cut[] = "EV e: p\no: KAT\nt: ca";
    CHECK(zen_r4_tape_parse(cut, strlen(cut), &event));
    CHECK_STR(event.raw, "ca");
    char longtext[400] = "EV e: p\no: S\nt: ";
    memset(longtext + strlen(longtext), 'x', 300);
    strcat(longtext, "\n\n");
    CHECK(zen_r4_tape_parse(longtext, strlen(longtext), &event));
    CHECK(strlen(event.raw) == ZEN_R4_TAPE_RAW_MAX - 1);
}

int main(void) {
    test_strokes();
    test_text();
    test_messages();
    test_rejects();
    test_parse();
    printf("stroke tape: %u checks passed\n", checks);
    return 0;
}
