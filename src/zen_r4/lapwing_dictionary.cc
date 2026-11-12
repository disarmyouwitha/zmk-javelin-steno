#include "lapwing_dictionary.h"
#include "lapwing_flash.h"
#include "dictionary/compact_map_dictionary.h"
#include "dictionary/dictionary_definition.h"
#include "xip_load.h"

#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>

static constexpr uintptr_t image_start = DT_REG_ADDR_BY_NAME(DT_NODELABEL(qspi), qspi_mm);
static constexpr uintptr_t image_end = image_start + LAPWING_BYTES;
static StenoDictionary *dictionary;
static StenoDictionary *plover;
static StenoDictionary *extras[EXTRAS_DICTIONARY_COUNT];
// The image stores them in this order. tools/zen-r4-lapwing/prepare_extras.py
// must compile them in the same order.
static const char *const extras_names[EXTRAS_DICTIONARY_COUNT] = {
    "Lapwing Commands",
    "Lapwing Numbers",
    "Lapwing UK Additions",
    "Lapwing Proper Nouns",
};

#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
class ReadOnlyBase final : public StenoDictionary {
public:
  explicit ReadOnlyBase(StenoCompactMapDictionary &map)
      : StenoDictionary(map.GetMaximumOutlineLength()), map(map) {}

  StenoDictionaryLookupResult Lookup(const StenoDictionaryLookup &lookup) const override {
    return map.Lookup(lookup);
  }

  const char *GetName() const override { return map.GetName(); }

private:
  StenoCompactMapDictionary &map;
};
#endif

StenoDictionary *lapwing_dictionary() {
  return lapwing_mapped_address() ? dictionary : nullptr;
}

StenoDictionary *plover_dictionary() {
  return lapwing_mapped_address() ? plover : nullptr;
}

const char *extras_dictionary_name(size_t index) {
  return index < EXTRAS_DICTIONARY_COUNT ? extras_names[index] : nullptr;
}

StenoDictionary *extras_dictionary(size_t index) {
  return index < EXTRAS_DICTIONARY_COUNT && lapwing_mapped_address() ? extras[index] : nullptr;
}

StenoDictionaryLookupResult lapwing_copy_lookup_text(const uint8_t *text) {
  // Return RAM strings so the engine can tokenize them with ordinary byte loads.
  char buffer[128];
  auto address = reinterpret_cast<uintptr_t>(text);
  uintptr_t end = image_end;
#if PLOVER_BYTES > 0
  if (address >= image_start + PLOVER_FLASH_OFFSET &&
      address < image_start + PLOVER_FLASH_OFFSET + PLOVER_BYTES) {
    end = image_start + PLOVER_FLASH_OFFSET + PLOVER_BYTES;
  }
#endif
#if EXTRAS_BYTES > 0
  if (address >= image_start + EXTRAS_FLASH_OFFSET &&
      address < image_start + EXTRAS_FLASH_OFFSET + EXTRAS_BYTES) {
    end = image_start + EXTRAS_FLASH_OFFSET + EXTRAS_BYTES;
  }
#endif
  for (size_t i = 0; i < sizeof(buffer) && address >= image_start && address < end;
       i++, address++) {
    buffer[i] = xip_load_byte(reinterpret_cast<const void *>(address));
    if (buffer[i] == '\0') {
      return StenoDictionaryLookupResult::CreateDupN(buffer, i);
    }
  }
  return StenoDictionaryLookupResult::CreateInvalid();
}

namespace {
// Definition objects and their stroke tables must outlive the native maps.
struct BaseDictionary {
  StenoCompactMapDictionaryDefinition definition;
  StenoCompactMapDictionaryStrokesDefinition strokes[16];
  StenoDictionary *dictionary = nullptr;

  // Loads dictionary `index` of the `count` the image must hold. A null
  // `translation` skips the lookup check and keeps the structural ones.
  bool Load(uintptr_t address, size_t bytes, const char *name, const StenoStroke &stroke,
            const char *translation, size_t index = 0, size_t count = 1) {
    if (dictionary) {
      return true;
    }
    const auto *collection = reinterpret_cast<const uint8_t *>(address);
    if (xip_load_word(collection) != STENO_MAP_DICTIONARY_COLLECTION_MAGIC ||
        xip_load_word(collection + offsetof(StenoDictionaryCollection, dictionaryCount)) !=
            count ||
        index >= count) {
      return false;
    }
    const auto definition_address = xip_load_word(
        collection + offsetof(StenoDictionaryCollection, dictionaries) + index * sizeof(uint32_t));
    if (definition_address % 4 || definition_address < address ||
        definition_address + sizeof(definition) > address + bytes) {
      return false;
    }
    copy_xip_bytes(&definition, reinterpret_cast<const void *>(definition_address), sizeof(definition));
    const auto table = reinterpret_cast<uintptr_t>(definition.strokes);
    if (definition.type != StenoDictionaryType::COMPACT_MAP ||
        definition.maximumOutlineLength == 0 ||
        definition.maximumOutlineLength > ARRAY_SIZE(strokes) || table % 4 || table < address ||
        table + definition.maximumOutlineLength * sizeof(strokes[0]) > address + bytes) {
      return false;
    }
    copy_xip_bytes(strokes, definition.strokes,
                   definition.maximumOutlineLength * sizeof(strokes[0]));
    definition.strokes = strokes;
    definition.name = name;
    auto *map = new (definition) StenoCompactMapDictionary(definition);
    if (!map) {
      return false;
    }
    if (translation) {
      auto result = map->Lookup(StenoDictionaryLookup(&stroke, 1));
      const bool matches = result.IsValid() && strcmp(result.GetText(), translation) == 0;
      result.Destroy();
      if (!matches) {
        printk("%s native lookup failed\n", name);
        delete map;
        return false;
      }
    }
#ifdef CONFIG_ZMK_JAVELIN_STENO_BROWSER_DICTIONARY
    dictionary = new ReadOnlyBase(*map);
#else
    dictionary = map;
#endif
    printk("%s native lookup passed\n", name);
    return dictionary != nullptr;
  }
};
BaseDictionary lapwing_base;
#if PLOVER_BYTES > 0
BaseDictionary plover_base;
#endif
#if EXTRAS_BYTES > 0
BaseDictionary extras_base[EXTRAS_DICTIONARY_COUNT];
#endif
} // namespace

extern "C" bool lapwing_verify_native_dictionary(uintptr_t address) {
  if (!lapwing_base.Load(address, LAPWING_BYTES, "Lapwing Base", StenoStroke("KAT"), "cat")) {
    return false;
  }
  // Keep the existing multi-stroke regression check on the original base.
  const StenoStroke strokes[] = {StenoStroke("HE"), StenoStroke("HROE")};
  auto result = lapwing_base.dictionary->Lookup(StenoDictionaryLookup(strokes, 2));
  const bool matches = result.IsValid() && strcmp(result.GetText(), "hello") == 0;
  result.Destroy();
  if (!matches) {
    return false;
  }
  dictionary = lapwing_base.dictionary;
  return true;
}

extern "C" bool plover_verify_native_dictionary(uintptr_t address) {
#if PLOVER_BYTES > 0
  if (!plover_base.Load(address, PLOVER_BYTES, "Plover Base", StenoStroke("KAT"), "cat")) {
    return false;
  }
  plover = plover_base.dictionary;
  return true;
#else
  return false;
#endif
}

extern "C" bool extras_verify_native_dictionary(uintptr_t address) {
#if EXTRAS_BYTES > 0
  // One known entry is looked up in the two dictionaries that have outlines
  // without the number key. The other two get the structural checks only.
  const struct {
    const char *outline;
    const char *translation;
  } checks[EXTRAS_DICTIONARY_COUNT] = {
      {"SP", "{^^}"},      // commands
      {"S", nullptr},      // numbers
      {"O*ERD", "odour"},  // UK additions
      {"S", nullptr},      // proper nouns
  };
  for (size_t i = 0; i < EXTRAS_DICTIONARY_COUNT; i++) {
    // The stroke constructor takes string literals only.
    StenoStroke stroke;
    stroke.Set(checks[i].outline);
    if (!extras_base[i].Load(address, EXTRAS_BYTES, extras_names[i], stroke,
                             checks[i].translation, i, EXTRAS_DICTIONARY_COUNT)) {
      return false;
    }
  }
  for (size_t i = 0; i < EXTRAS_DICTIONARY_COUNT; i++) {
    extras[i] = extras_base[i].dictionary;
  }
  return true;
#else
  return false;
#endif
}
