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

#include <zephyr/kernel.h>
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

  // Load dictionaries from collection
  List<StenoDictionaryListEntry> entries;
  collection->AddDictionariesToList(entries);

  if (entries.IsEmpty()) {
    LOG_ERR("Steno dict collection empty");
    return false;
  }

  new (dictionaryList)
      StenoDictionaryList(static_cast<List<StenoDictionaryListEntry> &&>(entries));

  // Load orthography from collection if present, else empty
  const StenoOrthography *ortho = &StenoOrthography::emptyOrthography;
  new (compiledOrthography) StenoCompiledOrthography(*ortho);

  // Construct engine with full dict stack + user dict
  new (StenoEngine::container) StenoEngine(
      dictionaryList.value,
      nullptr,
      compiledOrthography.value,
      StenoStroke(StrokeMask::STAR),  // undo stroke = *
      nullptr);

  // Register console commands
  StenoEngine::container.value.AddConsoleCommands(Console::instance);
  Flash::AddConsoleCommands(Console::instance);

  LOG_INF("Steno engine: %d read-only test dicts", collection->dictionaryCount);
  return true;
}

extern "C" {

void zmk_javelin_steno_init(void) {
  if (initialized) {
    return;
  }
  initialized = true;

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

  LOG_INF("Steno processor pipeline ready");
}

void zmk_javelin_steno_process_key(int steno_key_index, bool is_press) {
  if (!processor) {
    return;
  }
  processor->Process(static_cast<StenoKey>(steno_key_index), is_press);
}

} // extern "C"
