#include "javelin_lookup.h"

#include "console.h"
#include "dictionary/compact_map_dictionary.h"
#include "dictionary/dictionary_definition.h"
#include "flash.h"
#include "xip_load.h"

#include <nrf.h>
#include <nrf52_erratas.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

// These hooks belong to dictionary editing and console commands. This image
// exercises read-only lookups, so editing must never reach the flash driver.
void Flash::Write(const void *, const void *, size_t, FlashWriteMode)
{
	k_panic();
}

void Console::PrintfInternal(const char *, ...)
{
}

ConsoleWriter::ClassData ConsoleWriter::classData{};

const char *javelin_lookup_phrase(uintptr_t address, char *phrase)
{
	printk("Using synchronized XIP loads (erratum 118: %u, erratum 216: %u)\n",
	       nrf52_errata_118(), nrf52_errata_216());
	const auto *collection = reinterpret_cast<const uint8_t *>(address);
	if (xip_load_word(collection) != STENO_MAP_DICTIONARY_COLLECTION_MAGIC ||
	    (xip_load_word(collection + offsetof(StenoDictionaryCollection, dictionaryCount)) &
	     0xffff) != 1) {
		return "FAIL: mapped JSC4 header";
	}
	const auto definition_address =
		xip_load_word(collection + offsetof(StenoDictionaryCollection, dictionaries));
	const auto fixture_end = address + (javelin_fixture_end - javelin_fixture_start);
	if (definition_address < address ||
	    definition_address > fixture_end - sizeof(StenoCompactMapDictionaryDefinition)) {
		return "FAIL: mapped definition address";
	}
	StenoCompactMapDictionaryDefinition definition;
	copy_xip_bytes(&definition, reinterpret_cast<const void *>(definition_address),
		       sizeof(definition));
	if (definition.type != StenoDictionaryType::COMPACT_MAP ||
	    definition.maximumOutlineLength != 1) {
		return "FAIL: mapped compact dictionary definition";
	}
	StenoCompactMapDictionaryStrokesDefinition stroke_table;
	copy_xip_bytes(&stroke_table, definition.strokes, sizeof(stroke_table));
	definition.strokes = &stroke_table;
	auto *dictionary = new (definition) StenoCompactMapDictionary(definition);
	if (!dictionary) {
		return "FAIL: dictionary allocation";
	}

	const struct {
		uint32_t stroke;
		const char *outline;
		const char *expected;
	} strokes[] = {
		{0x14, "TP", "if"}, {0x80000, "-T", "the"}, {0x80108, "KAT", "cat"},
		{0x2, "S", "is"}, {0x80000, "-T", "the"}, {0x80108, "KAT", "cat"},
	};
	const char *outcome = "PASS: native Javelin lookups used mapped external flash";
	size_t size = 0;
	phrase[0] = '\0';
	for (const auto &step : strokes) {
		const StenoStroke stroke(step.stroke);
		auto result = dictionary->Lookup(&stroke, 1);
		if (!result.IsValid()) {
			outcome = "FAIL: native lookup returned no entry";
			break;
		}
		const char *word = result.GetText();
		const auto word_address = reinterpret_cast<uintptr_t>(word);
		const auto expected_size = strlen(step.expected);
		char word_copy[4];
		if (word_address < address || word_address >= fixture_end ||
		    fixture_end - word_address <= expected_size || expected_size >= sizeof(word_copy)) {
			outcome = "FAIL: native lookup did not return a mapped text address";
			result.Destroy();
			break;
		}
		copy_xip_bytes(word_copy, word, expected_size + 1);
		if (memcmp(word_copy, step.expected, expected_size + 1) != 0) {
			outcome = "FAIL: mapped lookup text differed from expected";
			result.Destroy();
			break;
		}
		printk("Native stroke %s -> %s at 0x%08x\n", step.outline, word_copy,
		       static_cast<unsigned int>(word_address));
		if (size) {
			phrase[size++] = ' ';
		}
		memcpy(phrase + size, word_copy, expected_size + 1);
		size += expected_size;
		result.Destroy();
	}
	delete dictionary;
	return outcome;
}
