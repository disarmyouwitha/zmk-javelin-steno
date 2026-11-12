#include "zmk_javelin_steno/zmk_platform_shim.h"

#include "console.h"
#include "button_script_manager.h"
#include "container/list.h"
#include "dictionary/dictionary_definition.h"
#include "dictionary/dictionary_list.h"
#include "dictionary/user_dictionary.h"
#include "engine.h"
#include "flash.h"
#include "orthography.h"
#include "english_orthography.h"
#ifdef CONFIG_ZMK_JAVELIN_STENO_MACOS_US_LAYOUT
#include "host_layout.h"
#include "platform/macos_us_layout.h"
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
#include "platform/windows_us_layout.h"
#endif
#endif
#include "processor/all_up.h"
#include "processor/first_up.h"
#include "processor/jeff_modifiers.h"
#include "processor/processor.h"
#include "processor/repeat.h"
#include "steno_key_state.h"
#include "static_allocate.h"
#include "stroke.h"
#ifdef CONFIG_ZMK_ZEN_R4_PRACTICE
#include "zen_r4/split_status.h"
#include <new>
#include <strings.h>
#include <zephyr/sys/atomic.h>
#endif

#ifdef CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING
#include "lapwing_dictionary.h"
#include "lapwing_flash.h"
#endif
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
#include "user_dictionary_store.h"
#endif

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
#include <zephyr/settings/settings.h>
#include <errno.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Linker symbols for embedded dictionary binary (dict_embed.S)
extern "C" const uint8_t _javelin_dict_start[];
extern "C" const uint8_t _javelin_dict_end[];

static JavelinStaticAllocate<StenoCompiledOrthography> compiledOrthography;
static JavelinStaticAllocate<StenoDictionaryList> dictionaryList;
static JavelinStaticAllocate<StenoJeffModifiers> jeffModifiers;
static JavelinStaticAllocate<StenoRepeat> stenoRepeat;
static JavelinStaticAllocate<StenoAllUp> allUp;
static StenoProcessor *processor = nullptr;

static bool initialized = false;

#if defined(CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING) && defined(CONFIG_ZMK_SLEEP)
class JavelinEngineLock {
public:
  JavelinEngineLock() : held(lapwing_flash_acquire()) {}
  ~JavelinEngineLock() {
    if (held) {
      lapwing_flash_release();
    }
  }
  explicit operator bool() const { return held; }

private:
  bool held;
};
#elif defined(CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY)
K_MUTEX_DEFINE(javelin_engine_mutex);

class JavelinEngineLock {
public:
  JavelinEngineLock() { k_mutex_lock(&javelin_engine_mutex, K_FOREVER); }
  ~JavelinEngineLock() { k_mutex_unlock(&javelin_engine_mutex); }
  explicit operator bool() const { return true; }
};
#else
class JavelinEngineLock {
public:
  explicit operator bool() const { return true; }
};
#endif

#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
// Versioned ZMK settings record. Preserve selection independently of base images
// and personal dictionary snapshots. A fresh keyboard starts with Lapwing.
// personal=1, Lapwing=2, Plover=4, then the Lapwing extras from bit 3 up.
#define EXTRAS_SELECTION_SHIFT 3
#define EXTRAS_SELECTION_BIT(index) (uint8_t(1) << (EXTRAS_SELECTION_SHIFT + (index)))
// Commands, numbers and proper nouns start on. UK additions start off.
#define EXTRAS_SELECTION_DEFAULT                                                                   \
  (EXTRAS_SELECTION_BIT(EXTRAS_COMMANDS) | EXTRAS_SELECTION_BIT(EXTRAS_NUMBERS) |                  \
   EXTRAS_SELECTION_BIT(EXTRAS_PROPER_NOUNS))
static uint8_t dictionary_selection = 3 | EXTRAS_SELECTION_DEFAULT;
#endif

#ifdef CONFIG_ZMK_ZEN_R4_PRACTICE
K_MUTEX_DEFINE(practice_mutex);
static atomic_t practice_screen;
static StenoKeyState practice_pending_releases;
static struct {
  bool active;
  bool ignore_chord;
  bool no_dictionary;
  uint8_t round;
  uint8_t streak;
  uint8_t best;
  int64_t advance_at;
  zen_r4_practice_drill drill;
  zen_r4_practice_feedback feedback;
  StenoKeyState keys;
  StenoKeyState chord;
} practice;

static void practice_publish() {
  uint32_t snapshot = 0;
  if (practice.active) {
    snapshot = ZEN_R4_PRACTICE_ACTIVE |
               (practice.no_dictionary ? ZEN_R4_PRACTICE_NO_DICTIONARY : 0) |
               (practice.round % 4) |
               (uint32_t(practice.feedback) << 2) |
               (uint32_t(practice.round) << 4) |
               (uint32_t(practice.streak) << 9) |
               (uint32_t(practice.best) << 14) |
               (uint32_t(practice.drill) << ZEN_R4_PRACTICE_DRILL_SHIFT);
  }
  atomic_set(&practice_screen, snapshot);
  zen_r4_split_status_refresh();
}

static void practice_advance(struct k_work *) {
  k_mutex_lock(&practice_mutex, K_FOREVER);
  if (practice.active && practice.feedback == ZEN_R4_PRACTICE_CORRECT &&
      k_uptime_get() >= practice.advance_at) {
    ++practice.round;
    practice.feedback = practice.round == zen_r4_practice_rounds(practice.drill)
                            ? ZEN_R4_PRACTICE_DONE
                            : ZEN_R4_PRACTICE_PROMPT;
    practice_publish();
  }
  k_mutex_unlock(&practice_mutex);
}

K_WORK_DELAYABLE_DEFINE(practice_advance_work, practice_advance);

static const StenoDictionary *practice_fallback_dictionary() {
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
  // Use the verified base locally. Never enable a dictionary in the engine
  // or change the saved selection on behalf of a drill.
  if (!(dictionary_selection & 6)) {
    return lapwing_dictionary();
  }
#endif
  return nullptr;
}

static void practice_refresh_dictionary() {
  practice.no_dictionary =
      practice.drill == ZEN_R4_PRACTICE_WORDS &&
      StenoEngine::container.value.GetDictionary().GetMaximumOutlineLength() == 0 &&
      !practice_fallback_dictionary();
}

static void practice_switch_drill(int direction) {
  k_work_cancel_delayable(&practice_advance_work);
  const int count = ZEN_R4_PRACTICE_DRILL_COUNT;
  const auto drill = static_cast<zen_r4_practice_drill>((practice.drill + direction + count) % count);
  practice = {};
  practice.active = true;
  practice.drill = drill;
  practice_refresh_dictionary();
  practice_publish();
}

static bool practice_correct(const StenoStroke &stroke) {
  if (practice.drill != ZEN_R4_PRACTICE_WORDS) {
    StenoStroke expected;
    expected.Set(zen_r4_practice_outline(practice.drill, practice.round));
    return stroke == expected;
  }
  const StenoDictionaryLookup lookup(&stroke, 1);
  auto result = StenoEngine::container.value.GetDictionary().Lookup(lookup);
  if (!result.IsValid()) {
    if (const auto *base = practice_fallback_dictionary()) {
      result = base->Lookup(lookup);
    }
  }
  const bool correct =
      result.IsValid() && strcasecmp(result.GetText(), zen_r4_practice_words[practice.round % 4]) == 0;
  result.Destroy();
  return correct;
}

extern "C" uint32_t zen_r4_practice_snapshot(void) {
  return uint32_t(atomic_get(&practice_screen));
}

extern "C" void zen_r4_practice_set_active(bool active) {
  if (active) {
    zmk_javelin_steno_init();
  } else {
    // Leaving a layer that never started practice needs no engine access,
    // so ordinary layer changes do not touch the dictionary flash.
    k_mutex_lock(&practice_mutex, K_FOREVER);
    const bool was_active = practice.active;
    k_mutex_unlock(&practice_mutex);
    if (!was_active) {
      return;
    }
  }
  const JavelinEngineLock engine_lock;
  if (!engine_lock) {
    return;
  }
  k_mutex_lock(&practice_mutex, K_FOREVER);
  if (active != practice.active) {
    k_work_cancel_delayable(&practice_advance_work);
    if (processor) {
      // Cancel partial strokes and repeats at the practice boundary.
      practice_pending_releases |= processor->GetCurrentKeyState();
      stenoRepeat.value.Process(StenoKeyState(), StenoAction::CANCEL_ALL);
      new (processor) StenoProcessor(stenoRepeat.value);
    }
    practice_pending_releases |= practice.keys;
    practice = {};
    practice.active = active && processor;
    if (practice.active) {
      practice_refresh_dictionary();
    }
    practice_publish();
  }
  k_mutex_unlock(&practice_mutex);
}

// Called under the engine lock, so dictionary edits and flash sleep cannot
// race a lookup. Practice chords never enter the host-output processor.
static bool practice_process_key(int index, bool pressed) {
  k_mutex_lock(&practice_mutex, K_FOREVER);
  StenoKeyState key;
  key.Process(static_cast<StenoKey>(index), true);
  if ((practice_pending_releases & key).IsNotEmpty()) {
    practice_pending_releases &= ~key;
    if (!pressed) {
      k_mutex_unlock(&practice_mutex);
      return true;
    }
  }
  if (!practice.active) {
    k_mutex_unlock(&practice_mutex);
    return false;
  }
  if (pressed && practice.keys.IsEmpty()) {
    practice.ignore_chord = practice.feedback == ZEN_R4_PRACTICE_CORRECT ||
                            practice.feedback == ZEN_R4_PRACTICE_DONE;
  }
  practice.keys.Process(static_cast<StenoKey>(index), pressed);
  practice.chord |= practice.keys;
  if (!pressed && practice.keys.IsEmpty() && practice.chord.IsNotEmpty()) {
    const auto steno_key = static_cast<StenoKey>(index);
    if (practice.chord == key && steno_key >= StenoKey::STAR1 && steno_key <= StenoKey::STAR4) {
      practice_switch_drill(steno_key <= StenoKey::STAR2 ? -1 : 1);
    } else if (!practice.ignore_chord && practice.feedback != ZEN_R4_PRACTICE_CORRECT &&
               practice.feedback != ZEN_R4_PRACTICE_DONE) {
      practice_refresh_dictionary();
      if (practice.no_dictionary) {
        practice.feedback = ZEN_R4_PRACTICE_PROMPT;
      } else if (practice_correct(practice.chord.ToStroke())) {
        ++practice.streak;
        if (practice.streak > practice.best) {
          practice.best = practice.streak;
        }
        practice.feedback = ZEN_R4_PRACTICE_CORRECT;
        practice.advance_at = k_uptime_get() + 250;
        k_work_reschedule(&practice_advance_work, K_TIMEOUT_ABS_MS(practice.advance_at));
      } else {
        practice.streak = 0;
        practice.feedback = ZEN_R4_PRACTICE_INCORRECT;
      }
      practice_publish();
    }
    practice.chord.Reset();
  }
  k_mutex_unlock(&practice_mutex);
  return true;
}

static void practice_dictionary_changed() {
  k_mutex_lock(&practice_mutex, K_FOREVER);
  if (practice.active) {
    practice_refresh_dictionary();
    practice_publish();
  }
  k_mutex_unlock(&practice_mutex);
}
#endif

#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
#ifdef CONFIG_ZMK_JAVELIN_STENO_MACOS_US_LAYOUT
static constexpr const char *host_profiles[] = {"Mac U.S.", "Windows U.S. (plain text)"};
static uint8_t host_profile = 0;

static void apply_host_profile() {
  HostLayouts::SetActiveLayout(host_profile == 0 ? zmk_javelin_macos_us_layout()
                                                : zmk_javelin_windows_us_layout());
}

static void get_host_parameter(void *, const char *line) {
  const char *name = strchr(line, ' ');
  if (name && strcmp(name + 1, "available_host_layouts") == 0) {
    Console::Printf("[\"%s\",\"%s\"]\n\n", host_profiles[0], host_profiles[1]);
  } else if (name && strcmp(name + 1, "host_layout") == 0) {
    Console::Printf("%s\n\n", host_profiles[host_profile]);
  } else {
    Console::Printf("ERR Unknown parameter\n\n");
  }
}

static void set_host_profile(void *, const char *line) {
  const char *name = strchr(line, ' ');
  for (uint8_t profile = 0; name && profile < ARRAY_SIZE(host_profiles); ++profile) {
    if (strcmp(name + 1, host_profiles[profile]) != 0) {
      continue;
    }
    if (profile != host_profile) {
      if (settings_save_one("javelin/host_profile", &profile, sizeof(profile)) != 0) {
        Console::Printf("ERR Unable to save host layout\n\n");
        return;
      }
      host_profile = profile;
      apply_host_profile();
    }
    Console::SendOk();
    return;
  }
  Console::Printf("ERR Unknown host layout\n\n");
}
#endif

static int javelin_settings_set(const char *name, size_t length,
                                settings_read_cb read, void *argument) {
#ifdef CONFIG_ZMK_JAVELIN_STENO_MACOS_US_LAYOUT
  if (strcmp(name, "host_profile") == 0) {
    uint8_t profile;
    if (length != sizeof(profile) || read(argument, &profile, sizeof(profile)) != sizeof(profile) ||
        profile >= ARRAY_SIZE(host_profiles)) {
      return -EINVAL;
    }
    host_profile = profile;
    return 0;
  }
#endif
  if (strcmp(name, "dictionaries") != 0) {
    return -ENOENT;
  }
  // Version 1 predates the Lapwing extras and holds the first three bits.
  // Version 2 holds all seven.
  uint8_t record[2];
  if (length != sizeof(record) || read(argument, record, sizeof(record)) != sizeof(record)) {
    return -EINVAL;
  }
  if (record[0] == 1 && !(record[1] & ~7)) {
    dictionary_selection = record[1] | EXTRAS_SELECTION_DEFAULT;
  } else if (record[0] == 2 && !(record[1] & 0x80)) {
    dictionary_selection = record[1];
  } else {
    return -EINVAL;
  }
  return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(javelin, "javelin", nullptr, javelin_settings_set,
                               nullptr, nullptr);

static void select_dictionary(void *, const char *line) {
  const char *name = strchr(line, ' ');
  if (!name) {
    Console::Printf("ERR No dictionary specified\n\n");
    return;
  }
  ++name;
  uint8_t bit = 0;
  if (strcmp(name, "user_dictionary") == 0) {
    bit = 1;
  } else if (strcmp(name, "Lapwing Base") == 0) {
    bit = 2;
  } else if (strcmp(name, "Plover Base") == 0 && plover_dictionary()) {
    bit = 4;
  }
  for (size_t i = 0; !bit && i < EXTRAS_DICTIONARY_COUNT; i++) {
    if (extras_dictionary(i) && strcmp(name, extras_dictionary_name(i)) == 0) {
      bit = EXTRAS_SELECTION_BIT(i);
    }
  }
  if (!bit) {
    Console::Printf("ERR Unknown dictionary\n\n");
    return;
  }
  uint8_t selected = dictionary_selection;
  if (strncmp(line, "enable_dictionary ", 18) == 0) {
    selected |= bit;
  } else if (strncmp(line, "disable_dictionary ", 19) == 0) {
    selected &= ~bit;
  } else {
    selected ^= bit;
  }
  // Persist before acknowledging or changing the active engine. On failure the
  // previous selection remains active and the browser receives an error.
  const uint8_t record[] = {2, selected};
  if (selected != dictionary_selection &&
      settings_save_one("javelin/dictionaries", record, sizeof(record)) != 0) {
    Console::Printf("ERR Unable to save dictionary selection\n\n");
    return;
  }
  dictionary_selection = selected;
  BufferWriter events;
  ConsoleWriter::Push(&events);
  if (selected & bit) {
    StenoEngine::container.value.EnableDictionary(name);
  } else {
    StenoEngine::container.value.DisableDictionary(name);
  }
  ConsoleWriter::Pop();
#ifdef CONFIG_ZMK_ZEN_R4_PRACTICE
  practice_dictionary_changed();
#endif
  Console::SendOk();
  events.WriteBufferTo(ConsoleWriter::GetActiveWriter());
}
#endif

static bool load_dictionary_collection() {
  List<StenoDictionaryListEntry> entries;
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
  auto *personal = zen_user_dictionary_open();
  if (personal) {
    entries.Add(StenoDictionaryListEntry(personal, dictionary_selection & 1));
  }
#endif
#ifdef CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING
  // The Lapwing extras rank above the bases, in the order Plover gives them.
  for (size_t i = 0; i < EXTRAS_DICTIONARY_COUNT; i++) {
    if (auto *extra = extras_dictionary(i)) {
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
      entries.Add(StenoDictionaryListEntry(extra, dictionary_selection & EXTRAS_SELECTION_BIT(i)));
#else
      entries.Add(StenoDictionaryListEntry(extra, i != EXTRAS_UK_ADDITIONS));
#endif
    }
  }
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
  entries.Add(StenoDictionaryListEntry(lapwing_dictionary(), dictionary_selection & 2));
#else
  entries.Add(StenoDictionaryListEntry(lapwing_dictionary(), true));
#endif
  if (auto *plover = plover_dictionary()) {
    // Personal overrides remain first. Fresh settings default to Lapwing.
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
    entries.Add(StenoDictionaryListEntry(plover, dictionary_selection & 4));
#else
    entries.Add(StenoDictionaryListEntry(plover, false));
#endif
  }
#else
  const auto *collection =
      reinterpret_cast<const StenoDictionaryCollection *>(_javelin_dict_start);

  if (collection->magic != STENO_MAP_DICTIONARY_COLLECTION_MAGIC) {
    LOG_ERR("Steno dict magic mismatch: 0x%08x (expected 0x%08x)",
            collection->magic, STENO_MAP_DICTIONARY_COLLECTION_MAGIC);
    return false;
  }

  if (!collection->HasMatchingTimestamp()) {
    LOG_ERR("Steno dict timestamp mismatch");
    return false;
  }

  printk("[JAV] header and timestamp valid\n");

  // Load dictionaries from collection
  printk("[JAV] creating native dictionary objects\n");
  collection->AddDictionariesToList(entries);
  printk("[JAV] native dictionary objects created\n");
#endif

  if (entries.IsEmpty()) {
    LOG_ERR("Steno dict collection empty");
    return false;
  }

  new (dictionaryList)
      StenoDictionaryList(static_cast<List<StenoDictionaryListEntry> &&>(entries));

#ifndef CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING
  // Exercise the ACTUAL engine constructor/cache/lookup path before translation.
  const struct { uint32_t stroke; const char *text; } checks[] = {
      {0x14, "if"}, {0x80000, "the"}, {0x80108, "cat"}, {0x2, "is"}};
  for (const auto &check : checks) {
    const StenoStroke stroke(check.stroke);
    auto result = dictionaryList.value.Lookup(StenoDictionaryLookup(&stroke, 1));
    if (!result.IsValid() || strcmp(result.GetText(), check.text) != 0) {
      printk("[JAV] native lookup FAILED for 0x%x\n", check.stroke);
      result.Destroy();
      return false;
    }
    printk("[JAV] native lookup 0x%x -> %s\n", check.stroke, result.GetText());
    result.Destroy();
  }
#endif

  printk("[JAV] initializing orthography\n");
  new (compiledOrthography) StenoCompiledOrthography(english_orthography);
#ifdef CONFIG_ZMK_JAVELIN_STENO_MACOS_US_LAYOUT
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
  apply_host_profile();
#else
  HostLayouts::SetActiveLayout(zmk_javelin_macos_us_layout());
#endif
#endif

  printk("[JAV] constructing engine\n");
  // Construct engine with the read-only test dictionary
  new (StenoEngine::container) StenoEngine(
      dictionaryList.value,
      nullptr,
      compiledOrthography.value,
      StenoStroke(StrokeMask::STAR),  // undo stroke = *
      nullptr);

  // Register console commands
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
#ifdef CONFIG_ZMK_JAVELIN_STENO_MACOS_US_LAYOUT
  Console::instance.RegisterCommand("get_parameter", "Read host layout parameters",
                                    get_host_parameter, nullptr);
  Console::instance.RegisterCommand("set_host_layout", "Set and save the host layout",
                                    set_host_profile, nullptr);
#endif
  Console::instance.RegisterCommand("enable_dictionary", "Enable and save selection",
                                    select_dictionary, nullptr);
  Console::instance.RegisterCommand("disable_dictionary", "Disable and save selection",
                                    select_dictionary, nullptr);
  Console::instance.RegisterCommand("toggle_dictionary", "Toggle and save selection",
                                    select_dictionary, nullptr);
  if (personal) {
    zen_user_dictionary_register(Console::instance, dictionaryList.value);
  }
#endif
  StenoEngine::container.value.AddConsoleCommands(Console::instance);
#ifndef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
  Flash::AddConsoleCommands(Console::instance);
#endif

  LOG_INF("Steno engine initialized");
  return true;
}

extern "C" {

void zmk_javelin_steno_init(void) {
  const JavelinEngineLock lock;
  if (!lock || initialized) {
    return;
  }
#ifdef CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING
  if (!lapwing_dictionary()) {
    LOG_WRN("External Lapwing dictionary is unavailable");
    return;
  }
#endif
  initialized = true;
  printk("[JAV] lazy init start; thread=%s\n", k_thread_name_get(k_current_get()));

  // Construct the manager queried by the emitter even though ZMK handles keys.
  alignas(4) static const uint8_t empty_button_script[64] = {0x4a, 0x53, 0x53, 0x34};
  ButtonScriptManager::Initialize(empty_button_script);

  if (!load_dictionary_collection()) {
    LOG_WRN("Steno engine not started");
    return;
  }

  // Processor pipeline: Repeat → AllUp → JeffModifiers → Engine
  new (jeffModifiers) StenoJeffModifiers(StenoEngine::container.value);
  new (allUp) StenoAllUp(jeffModifiers.value);
  new (stenoRepeat) StenoRepeat(allUp.value);

  static StenoProcessor processorInstance(stenoRepeat.value);
  processor = &processorInstance;

  printk("[JAV] processor pipeline ready\n");
  LOG_INF("Steno processor pipeline ready");
}

void zmk_javelin_steno_process_key(int steno_key_index, bool is_press) {
  // Steno keys whose press reached the engine. A key held while the flash
  // becomes ready must not deliver a release without its press. Key events
  // all arrive on the system work queue, so this needs no lock.
  static uint64_t delivered_presses;
  const uint64_t bit =
      steno_key_index >= 0 && steno_key_index < 64 ? uint64_t(1) << steno_key_index : 0;

  const JavelinEngineLock lock;
  const bool ready = lock && processor;
  if (is_press) {
    if (!ready) {
      return;
    }
    delivered_presses |= bit;
  } else {
    const bool was_delivered = !bit || (delivered_presses & bit);
    delivered_presses &= ~bit;
    if (!ready || !was_delivered) {
      return;
    }
  }
#ifdef CONFIG_ZMK_ZEN_R4_PRACTICE
  if (practice_process_key(steno_key_index, is_press)) {
    return;
  }
#endif
  LOG_DBG("[JAV] key %d %s", steno_key_index, is_press ? "down" : "up");
  processor->Process(static_cast<StenoKey>(steno_key_index), is_press);
}

} // extern "C"

#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
bool zen_javelin_console_line(const char *line, size_t size, IWriter &writer) {
  const JavelinEngineLock lock;
  if (!lock) {
    // The flash is asleep, unprovisioned or still verifying.
    return false;
  }
  zmk_javelin_steno_init();
  ConsoleWriter::Push(&writer);
  Console::instance.HandleInput(line, size, ConnectionId::SERIAL_CONSOLE);
  ConsoleWriter::Pop();
  return true;
}
#endif
