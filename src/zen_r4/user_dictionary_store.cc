#include "user_dictionary_store.h"

#include "console.h"
#include "crc32.h"
#include "dictionary/user_dictionary.h"
#include "lapwing_flash.h"
#include "static_allocate.h"
#include "stroke_list_parser.h"

extern "C" {
#include "quad.h"
}

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>

namespace {
constexpr uint32_t STORAGE_START = 0xf00000;
constexpr size_t SECTOR_BYTES = 4096;
constexpr size_t PAGE_BYTES = 256;
constexpr size_t MIRROR_BYTES = 32 * 1024;
constexpr size_t BODY_BYTES = MIRROR_BYTES - StenoUserDictionaryData::ALL_DESCRIPTORS_SIZE;
constexpr size_t SLOT_BYTES = SECTOR_BYTES + BODY_BYTES;
constexpr uint32_t MAGIC = 0x3155445a; // ZDU1
constexpr uint32_t NATIVE_MAGIC = 0x4455534a; // JSUD, pinned Javelin ABI
constexpr uint32_t NATIVE_VERSION = 3;

static_assert(LAPWING_BYTES <= STORAGE_START);
static_assert(STORAGE_START + 2 * SLOT_BYTES <= 0xfff000);
static_assert(BODY_BYTES % SECTOR_BYTES == 0);

struct Header {
    uint32_t magic;
    uint32_t generation;
    uint32_t body_crc;
    uint32_t remaining;
    uint32_t maximum_outline;
    uint32_t header_crc;
};

alignas(SECTOR_BYTES) uint8_t mirror[MIRROR_BYTES];
alignas(4) uint8_t page[PAGE_BYTES];
StenoUserDictionaryData layout(mirror, sizeof(mirror));
JavelinStaticAllocate<StenoUserDictionary> dictionary;
StenoDictionary *parent;
int active_slot = -1;
Header active_header{};
bool store_available;

uint32_t slot_address(int slot) {
    return STORAGE_START + slot * SLOT_BYTES;
}

uint32_t header_crc(const Header &header) {
    return crc32_ieee(reinterpret_cast<const uint8_t *>(&header), offsetof(Header, header_crc));
}

bool wait_ready() {
    const int64_t deadline = k_uptime_get() + 2000;
    do {
        const nrfx_err_t rc = nrfx_qspi_mem_busy_check();
        if (rc != NRFX_ERROR_BUSY) {
            return rc == NRFX_SUCCESS;
        }
        k_msleep(1);
    } while (k_uptime_get() < deadline);
    return false;
}

bool verify_body(int slot, uint32_t expected_crc) {
    uint32_t crc = 0;
    for (size_t offset = 0; offset < BODY_BYTES; offset += sizeof(page)) {
        if (nrfx_qspi_read(page, sizeof(page), slot_address(slot) + SECTOR_BYTES + offset) !=
            NRFX_SUCCESS) {
            return false;
        }
        crc = crc32_ieee_update(crc, page, sizeof(page));
    }
    return crc == expected_crc;
}

bool valid_slot(int slot, Header &header) {
    return nrfx_qspi_read(&header, sizeof(header), slot_address(slot)) == NRFX_SUCCESS &&
           header.magic == MAGIC && header.header_crc == header_crc(header) &&
           header.remaining <= layout.dataBlockSize && header.remaining % 4 == 0 &&
           header.maximum_outline <= StenoUserDictionary::MAX_STROKE_COUNT &&
           verify_body(slot, header.body_crc);
}

void construct_dictionary() {
    new (dictionary) StenoUserDictionary(layout);
    dictionary.value.SetParentRecursively(parent);
    dictionary.value.OnLookupDataChanged();
}

bool restore_mirror() {
    memset(mirror, 0xff, sizeof(mirror));
    if (active_slot >= 0) {
        if (nrfx_qspi_read(mirror, BODY_BYTES, slot_address(active_slot) + SECTOR_BYTES) !=
                NRFX_SUCCESS ||
            crc32_ieee(mirror, BODY_BYTES) != active_header.body_crc) {
            return false;
        }
        // The persisted header has no RAM pointers. Rebuild the native descriptor
        // so entries survive changes to the firmware's RAM layout.
        StenoUserDictionaryDescriptor descriptor{};
        descriptor.magic = NATIVE_MAGIC;
        descriptor.version = NATIVE_VERSION;
        descriptor.data = layout;
        descriptor.data.dataBlockSizeRemaining = active_header.remaining;
        descriptor.data.maximumOutlineLength = active_header.maximum_outline;
        descriptor.crc32 = Crc32::Hash(&descriptor.data, sizeof(descriptor.data));
        memcpy(mirror + BODY_BYTES, &descriptor, sizeof(descriptor));
    }
    construct_dictionary();
    return true;
}

bool save_dictionary() {
    const auto *descriptor = layout.FindMostRecentDescriptor();
    if (!descriptor) {
        return false;
    }

    Header header{MAGIC, active_header.generation + 1, crc32_ieee(mirror, BODY_BYTES),
                  static_cast<uint32_t>(descriptor->data.dataBlockSizeRemaining),
                  descriptor->data.maximumOutlineLength,
                  0};
    if (active_slot >= 0 && header.body_crc == active_header.body_crc &&
        header.remaining == active_header.remaining &&
        header.maximum_outline == active_header.maximum_outline) {
        return true;
    }
    if (!reactivate_after_xip()) {
        return false;
    }
    header.header_crc = header_crc(header);
    const int slot = active_slot == 0 ? 1 : 0;
    const uint32_t base = slot_address(slot);

    // Erase the inactive header first and publish the replacement header last.
    for (size_t offset = 0; offset < SLOT_BYTES; offset += SECTOR_BYTES) {
        if (nrfx_qspi_erase(NRF_QSPI_ERASE_LEN_4KB, base + offset) != NRFX_SUCCESS ||
            !wait_ready()) {
            return false;
        }
    }
    for (size_t offset = 0; offset < BODY_BYTES; offset += PAGE_BYTES) {
        if (!Flash::IsErased(mirror + offset, PAGE_BYTES) &&
            (nrfx_qspi_write(mirror + offset, PAGE_BYTES, base + SECTOR_BYTES + offset) !=
                 NRFX_SUCCESS ||
             !wait_ready())) {
            return false;
        }
    }
    if (!verify_body(slot, header.body_crc) ||
        nrfx_qspi_write(&header, sizeof(header), base) != NRFX_SUCCESS || !wait_ready()) {
        return false;
    }
    Header readback{};
    if (nrfx_qspi_read(&readback, sizeof(readback), base) != NRFX_SUCCESS ||
        memcmp(&readback, &header, sizeof(header)) != 0) {
        return false;
    }
    active_header = header;
    active_slot = slot;
    return true;
}

void finish_edit(bool changed) {
    if (changed && save_dictionary()) {
        Console::SendOk();
        return;
    }
    if (!reactivate_after_xip() || !restore_mirror()) {
        // Keep Lapwing usable if the personal store can no longer be read.
        store_available = false;
        memset(mirror, 0xff, sizeof(mirror));
        construct_dictionary();
        parent->DisableDictionary("user_dictionary");
        Console::Printf("ERR Unable to restore the saved personal dictionary\n\n");
        return;
    }
    Console::Printf(changed ? "ERR Unable to save the personal dictionary\n\n"
                            : "ERR Entry not added; the personal dictionary may be full\n\n");
}

bool parse_outline(const char *line, StrokeListParser &parser) {
    const char *space = strchr(line, ' ');
    if (!space || !parser.Parse(space + 1) ||
        parser.GetCount() > StenoUserDictionary::MAX_STROKE_COUNT) {
        Console::Printf("ERR Invalid outline\n\n");
        return false;
    }
    return true;
}

bool can_edit() {
    if (!store_available) {
        Console::Printf("ERR Personal dictionary unavailable\n\n");
    }
    return store_available;
}

void add_entry(void *, const char *line) {
    StrokeListParser parser;
    if (!can_edit() || !parse_outline(line, parser)) {
        return;
    }
    if (*parser.failureOrEnd == '\0') {
        Console::Printf("ERR No translation specified\n\n");
        return;
    }
    finish_edit(dictionary.value.Add(parser.GetData(), parser.GetCount(), parser.failureOrEnd));
}

void remove_entry(void *, const char *line) {
    StrokeListParser parser;
    if (!can_edit() || !parse_outline(line, parser)) {
        return;
    }
    if (*parser.failureOrEnd != '\0') {
        Console::Printf("ERR Unexpected text after outline\n\n");
        return;
    }
    if (dictionary.value.Remove(parser.GetData(), parser.GetCount())) {
        finish_edit(true);
    } else {
        Console::SendOk();
    }
}

void reset_entries(void *, const char *) {
    if (!can_edit()) {
        return;
    }
    dictionary.value.Reset();
    dictionary.value.OnLookupDataChanged();
    finish_edit(true);
}

void lookup_outline(void *, const char *line) {
    StrokeListParser parser;
    if (!parse_outline(line, parser)) {
        return;
    }
    const StenoDictionaryLookup lookup(parser.GetData(), parser.GetCount());
    List<const StenoDictionary *> dictionaries;
    parent->GetDictionariesForOutline(dictionaries, lookup);
    Console::Printf("[");
    bool first = true;
    for (const auto *entry : dictionaries) {
        auto result = entry->Lookup(lookup);
        if (result.IsValid()) {
            Console::Printf(first ? "{t: %Y,d: %Y%s}" : ",{t: %Y,d: %Y%s}",
                            result.GetText(), entry->GetName(), entry->CanRemove() ? ",r: 1" : "");
            first = false;
        }
        result.Destroy();
    }
    Console::Printf("]\n\n");
}
} // namespace

StenoUserDictionary *zen_user_dictionary_open() {
    if (!reactivate_after_xip()) {
        return nullptr;
    }
    for (int slot = 0; slot < 2; slot++) {
        Header header{};
        if (valid_slot(slot, header) &&
            (active_slot < 0 || static_cast<int32_t>(header.generation - active_header.generation) > 0)) {
            active_slot = slot;
            active_header = header;
        }
    }
    store_available = restore_mirror();
    return store_available ? &dictionary.value : nullptr;
}

void zen_user_dictionary_register(Console &console, StenoDictionary &root) {
    parent = &root;
    console.RegisterCommand("lookup_stroke", "Look up an outline in the dictionaries", lookup_outline, nullptr);
    console.RegisterCommand("add_user_entry", "Save a personal translation", add_entry, nullptr);
    console.RegisterCommand("remove_user_entry", "Remove a personal translation", remove_entry, nullptr);
    console.RegisterCommand("reset_user_dictionary", "Clear personal translations", reset_entries, nullptr);
}

bool zen_user_dictionary_owns(const void *address, size_t size) {
    const auto start = reinterpret_cast<uintptr_t>(mirror);
    const auto target = reinterpret_cast<uintptr_t>(address);
    return target >= start && target - start <= sizeof(mirror) &&
           size <= sizeof(mirror) - (target - start);
}
