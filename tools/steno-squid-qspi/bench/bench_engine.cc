#include "dictionary/compact_map_dictionary.h"
#include "dictionary/dictionary_definition.h"
#include "javelin_lookup.h"
#include "steno_key_state.h"
#include "xip_load.h"
#include "zmk_javelin_steno/zmk_platform_shim.h"

#include <string.h>
#include <zephyr/kernel.h>

extern "C" void bench_begin_reports(void);
extern "C" bool bench_end_reports(char *phrase);

static uintptr_t fixture_address;

StenoDictionary *bench_open_dictionary()
{
    const auto *collection = reinterpret_cast<const uint8_t *>(fixture_address);
    if (xip_load_word(collection) != STENO_MAP_DICTIONARY_COLLECTION_MAGIC ||
        (xip_load_word(collection + offsetof(StenoDictionaryCollection, dictionaryCount)) &
         0xffff) != 1) {
        return nullptr;
    }
    const auto definition_address =
        xip_load_word(collection + offsetof(StenoDictionaryCollection, dictionaries));
    static StenoCompactMapDictionaryDefinition definition;
    static StenoCompactMapDictionaryStrokesDefinition stroke_table;
    copy_xip_bytes(&definition, reinterpret_cast<const void *>(definition_address),
                   sizeof(definition));
    if (definition.type != StenoDictionaryType::COMPACT_MAP ||
        definition.maximumOutlineLength != 1) {
        return nullptr;
    }
    copy_xip_bytes(&stroke_table, definition.strokes, sizeof(stroke_table));
    definition.strokes = &stroke_table;
    return new (definition) StenoCompactMapDictionary(definition);
}

static void inject(StenoKey first, StenoKey second = StenoKey::RES2,
                   StenoKey third = StenoKey::RES2)
{
    const StenoKey keys[] = {first, second, third};
    for (const auto key : keys) {
        if (key != StenoKey::RES2) {
            zmk_javelin_steno_process_key(static_cast<int>(key), true);
        }
    }
    for (const auto key : keys) {
        if (key != StenoKey::RES2) {
            zmk_javelin_steno_process_key(static_cast<int>(key), false);
        }
    }
    k_msleep(25);
}

const char *javelin_lookup_phrase(uintptr_t address, char *phrase)
{
    fixture_address = address;
    zmk_javelin_steno_init();
    bench_begin_reports();
    inject(StenoKey::TL, StenoKey::PL);
    inject(StenoKey::TR);
    inject(StenoKey::KL, StenoKey::A, StenoKey::TR);
    inject(StenoKey::S1);
    inject(StenoKey::TR);
    inject(StenoKey::KL, StenoKey::A, StenoKey::TR);
    if (!bench_end_reports(phrase)) {
        return "FAIL: invalid keyboard report or a key remained held";
    }
    return strcmp(phrase, "if the cat is the cat") == 0
               ? "PASS: full Javelin processor produced the expected ZMK USB reports"
               : "FAIL: ZMK keyboard reports did not produce the expected phrase";
}
