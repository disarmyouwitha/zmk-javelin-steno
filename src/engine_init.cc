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

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>

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

static bool load_dictionary_collection() {
  List<StenoDictionaryListEntry> entries;
#ifdef CONFIG_ZMK_JAVELIN_STENO_EXTERNAL_LAPWING
  entries.Add(StenoDictionaryListEntry(lapwing_dictionary(), true));
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

  // Load orthography from collection if present, else empty
  const StenoOrthography *ortho = &StenoOrthography::emptyOrthography;
  printk("[JAV] initializing orthography\n");
  new (compiledOrthography) StenoCompiledOrthography(*ortho);

  printk("[JAV] constructing engine\n");
  // Construct engine with the read-only test dictionary
  new (StenoEngine::container) StenoEngine(
      dictionaryList.value,
      nullptr,
      compiledOrthography.value,
      StenoStroke(StrokeMask::STAR),  // undo stroke = *
      nullptr);

  // Register console commands
  StenoEngine::container.value.AddConsoleCommands(Console::instance);
  Flash::AddConsoleCommands(Console::instance);

  LOG_INF("Steno engine initialized");
  return true;
}

extern "C" {

void zmk_javelin_steno_init(void) {
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
  if (!processor) {
    return;
  }
  printk("[JAV] key %d %s\n", steno_key_index, is_press ? "down" : "up");
  processor->Process(static_cast<StenoKey>(steno_key_index), is_press);
  printk("[JAV] key processed\n");
}

} // extern "C"
