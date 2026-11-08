#pragma once

#include "dictionary/dictionary.h"

StenoDictionary *lapwing_dictionary();
StenoDictionary *plover_dictionary();
StenoDictionaryLookupResult lapwing_copy_lookup_text(const uint8_t *text);
