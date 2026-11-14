#pragma once

#include "dictionary/dictionary.h"
#include <stddef.h>

StenoDictionary *lapwing_dictionary();
StenoDictionary *plover_dictionary();

// The dictionaries inside the Lapwing extras image, highest priority first.
enum {
  EXTRAS_COMMANDS,
  EXTRAS_NUMBERS,
  EXTRAS_UK_ADDITIONS,
  EXTRAS_PROPER_NOUNS,
  EXTRAS_DICTIONARY_COUNT,
};
// The name the browser shows for one of them.
const char *extras_dictionary_name(size_t index);
// Null until the extras image is mapped, and in builds without one.
StenoDictionary *extras_dictionary(size_t index);
StenoDictionaryLookupResult lapwing_copy_lookup_text(const uint8_t *text);
