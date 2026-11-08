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

  bool Load(uintptr_t address, size_t bytes, const char *name, const StenoStroke &stroke,
            const char *translation) {
    if (dictionary) {
      return true;
    }
    const auto *collection = reinterpret_cast<const uint8_t *>(address);
    if (xip_load_word(collection) != STENO_MAP_DICTIONARY_COLLECTION_MAGIC ||
        xip_load_word(collection + offsetof(StenoDictionaryCollection, dictionaryCount)) != 1) {
      return false;
    }
    const auto definition_address =
        xip_load_word(collection + offsetof(StenoDictionaryCollection, dictionaries));
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
    auto result = map->Lookup(StenoDictionaryLookup(&stroke, 1));
    const bool matches = result.IsValid() && strcmp(result.GetText(), translation) == 0;
    result.Destroy();
    if (!matches) {
      printk("%s native lookup failed\n", name);
      delete map;
      return false;
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
