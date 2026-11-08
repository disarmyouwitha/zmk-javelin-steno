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
static StenoCompactMapDictionary *dictionary;

StenoDictionary *lapwing_dictionary() {
  return lapwing_mapped_address() ? dictionary : nullptr;
}

StenoDictionaryLookupResult lapwing_copy_lookup_text(const uint8_t *text) {
  // Return RAM strings so the engine can tokenize them with ordinary byte loads.
  char buffer[128];
  auto address = reinterpret_cast<uintptr_t>(text);
  for (size_t i = 0; i < sizeof(buffer) && address >= image_start && address < image_end;
       i++, address++) {
    buffer[i] = xip_load_byte(reinterpret_cast<const void *>(address));
    if (buffer[i] == '\0') {
      return StenoDictionaryLookupResult::CreateDupN(buffer, i);
    }
  }
  return StenoDictionaryLookupResult::CreateInvalid();
}

extern "C" bool lapwing_verify_native_dictionary(uintptr_t address) {
  const auto *collection = reinterpret_cast<const uint8_t *>(address);
  if (xip_load_word(collection) != STENO_MAP_DICTIONARY_COLLECTION_MAGIC ||
      xip_load_word(collection + offsetof(StenoDictionaryCollection, dictionaryCount)) != 1) {
    return false;
  }

  const auto definition_address =
      xip_load_word(collection + offsetof(StenoDictionaryCollection, dictionaries));
  static StenoCompactMapDictionaryDefinition definition;
  static StenoCompactMapDictionaryStrokesDefinition stroke_table[16];
  copy_xip_bytes(&definition, reinterpret_cast<const void *>(definition_address), sizeof(definition));
  if (definition.type != StenoDictionaryType::COMPACT_MAP ||
      definition.maximumOutlineLength == 0 ||
      definition.maximumOutlineLength > ARRAY_SIZE(stroke_table)) {
    return false;
  }
  copy_xip_bytes(stroke_table, definition.strokes,
                 definition.maximumOutlineLength * sizeof(stroke_table[0]));
  definition.strokes = stroke_table;
  definition.name = "Lapwing Base";
  dictionary = new (definition) StenoCompactMapDictionary(definition);
  if (!dictionary) {
    return false;
  }

  const struct {
    StenoStroke strokes[2];
    size_t length;
    const char *translation;
  } checks[] = {
      {{StenoStroke("TP")}, 1, "if"},
      {{StenoStroke("KAT")}, 1, "cat"},
      {{StenoStroke("HE"), StenoStroke("HROE")}, 2, "hello"},
  };
  for (const auto &check : checks) {
    auto result = dictionary->Lookup(StenoDictionaryLookup(check.strokes, check.length));
    const bool matches = result.IsValid() && strcmp(result.GetText(), check.translation) == 0;
    result.Destroy();
    if (!matches) {
      printk("Lapwing native lookup failed for %s\n", check.translation);
      return false;
    }
  }
  printk("Lapwing native lookups passed, including a two-stroke outline\n");
  return true;
}
