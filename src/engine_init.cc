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

#ifdef CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING
#include "lapwing_dictionary.h"
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

#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
K_MUTEX_DEFINE(javelin_engine_mutex);

class JavelinEngineLock {
public:
  JavelinEngineLock() { k_mutex_lock(&javelin_engine_mutex, K_FOREVER); }
  ~JavelinEngineLock() { k_mutex_unlock(&javelin_engine_mutex); }
};

// Versioned ZMK settings record. Preserve selection independently of base images
// and personal dictionary snapshots. A fresh keyboard starts with Lapwing.
static uint8_t dictionary_selection = 3; // personal=1, Lapwing=2, Plover=4

#ifdef CONFIG_ZMK_JAVELIN_STENO_MACOS_US_LAYOUT
static constexpr const char *host_profiles[] = {"Mac U.S./ABC", "Windows U.S. (plain text)"};
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
  uint8_t record[2];
  if (length != sizeof(record) || read(argument, record, sizeof(record)) != sizeof(record) ||
      record[0] != 1 || (record[1] & ~7)) {
    return -EINVAL;
  }
  dictionary_selection = record[1];
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
  } else {
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
  const uint8_t record[] = {1, selected};
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
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
  const JavelinEngineLock lock;
#endif
  if (initialized) {
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
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
  const JavelinEngineLock lock;
#endif
  if (!processor) {
    return;
  }
  printk("[JAV] key %d %s\n", steno_key_index, is_press ? "down" : "up");
  processor->Process(static_cast<StenoKey>(steno_key_index), is_press);
  printk("[JAV] key processed\n");
}

} // extern "C"

#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
void zen_javelin_console_line(const char *line, size_t size, IWriter &writer) {
  const JavelinEngineLock lock;
  zmk_javelin_steno_init();
  ConsoleWriter::Push(&writer);
  Console::instance.HandleInput(line, size, ConnectionId::SERIAL_CONSOLE);
  ConsoleWriter::Pop();
}
#endif
