// Catches what the Javelin engine prints for each stroke and hands the last
// stroke, its translation and words per minute to the split status sender.
//
// The engine reports strokes as console events: a paper tape event for every
// stroke and, if enabled, a suggestion event when a shorter outline exists. No
// host reads those events on this keyboard, so they are enabled only for the
// duration of one key event, with a writer that keeps the text.
#include "console.h"
#include "wpm_tracker.h"
#include "writer.h"
#include "split_status.h"
#include "stroke_feed.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

namespace {

class TapeWriter final : public IWriter {
public:
  void Write(const char *data, size_t length) override {
    // Text past the end is dropped. A stroke's own paper tape block comes
    // first, so the part that matters is kept.
    if (length > sizeof(buffer) - count) {
      length = sizeof(buffer) - count;
    }
    memcpy(buffer + count, data, length);
    count += length;
  }

  char buffer[1024];
  size_t count = 0;
};

TapeWriter tapeWriter;
bool paperTapeWasEnabled;
#ifdef CONFIG_ZMK_STENO_SQUID_STROKE_HINT
bool suggestionWasEnabled;
uint32_t startCycles;
#endif
bool active;

void Publish(const steno_squid_tape_event &event) {
  static uint8_t sequence;
  sequence ^= 1;

  steno_squid_tape tape = {};
  tape.valid = true;
  tape.seq = sequence;
  tape.mask = event.mask;
  if (event.has_text) {
    steno_squid_tape_clean_text(event.raw, tape.text, sizeof(tape.text));
  } else if (event.undo) {
    strcpy(tape.text, "undo");
  }

  // Count what this stroke typed, then read the rate. The tracker leaves the
  // current second out, so a first stroke reads zero until the next second.
  if (event.has_text) {
    const unsigned typed = steno_squid_tape_typed_chars(event.raw);
    if (typed) {
      WpmTracker::instance.Tally(typed);
    }
  }
  const int wpm = WpmTracker::instance.Get10sWpm();
  tape.wpm = wpm > 255 ? 255 : wpm < 0 ? 0 : wpm;

  if (event.hint) {
    tape.hint_mask = event.hint_mask;
    tape.hint_count = event.hint_count;
  }
  steno_squid_tape_publish(&tape);
}

} // namespace

extern "C" void steno_squid_stroke_feed_begin(void) {
  if (active) {
    return; // Never nested. A second begin would lose the first one's flags.
  }
  active = true;
  tapeWriter.count = 0;
  paperTapeWasEnabled = Console::IsEventEnabled(ConsoleEvent::PAPER_TAPE);
  Console::EnableEvent(ConsoleEvent::PAPER_TAPE);
#ifdef CONFIG_ZMK_STENO_SQUID_STROKE_HINT
  suggestionWasEnabled = Console::IsEventEnabled(ConsoleEvent::SUGGESTION);
  Console::EnableEvent(ConsoleEvent::SUGGESTION);
  startCycles = k_cycle_get_32();
#endif
  ConsoleWriter::Push(&tapeWriter);
}

extern "C" void steno_squid_stroke_feed_end(void) {
  if (!active) {
    return;
  }
  active = false;
  ConsoleWriter::Pop();
  if (!paperTapeWasEnabled) {
    Console::DisableEvent(ConsoleEvent::PAPER_TAPE);
  }
#ifdef CONFIG_ZMK_STENO_SQUID_STROKE_HINT
  if (!suggestionWasEnabled) {
    Console::DisableEvent(ConsoleEvent::SUGGESTION);
  }
  const uint32_t cycles = k_cycle_get_32() - startCycles;
#endif
  if (tapeWriter.count == 0) {
    return;
  }

  steno_squid_tape_event event;
  if (!steno_squid_tape_parse(tapeWriter.buffer, tapeWriter.count, &event)) {
    return;
  }
#ifdef CONFIG_ZMK_STENO_SQUID_STROKE_HINT
  // How long a stroke takes with suggestions on. This runs on the thread that
  // handles keys, so watch for anything over a few milliseconds.
  LOG_INF("[tape] stroke handled in %u us%s", (unsigned)k_cyc_to_us_floor32(cycles),
          event.hint ? ", with a hint" : "");
#endif
  Publish(event);
}
