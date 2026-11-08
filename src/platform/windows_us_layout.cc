#include "windows_us_layout.h"

#include "host_layout.h"
#include "key_code.h"
#include "steno_key_code.h"

#include <stddef.h>

namespace {

// Leading-zero Alt codes on US Windows use Windows-1252. U+00A0..U+00FF
// have the same code as their byte. These are the other printable mappings,
// sorted by Unicode for Javelin's binary search.
constexpr struct {
    uint16_t unicode;
    uint8_t byte;
} extra_characters[] = {
    {0x0152, 140}, {0x0153, 156}, {0x0160, 138}, {0x0161, 154},
    {0x0178, 159}, {0x017d, 142}, {0x017e, 158}, {0x0192, 131},
    {0x02c6, 136}, {0x02dc, 152}, {0x2013, 150}, {0x2014, 151},
    {0x2018, 145}, {0x2019, 146}, {0x201a, 130}, {0x201c, 147},
    {0x201d, 148}, {0x201e, 132}, {0x2020, 134}, {0x2021, 135},
    {0x2022, 149}, {0x2026, 133}, {0x2030, 137}, {0x2039, 139},
    {0x203a, 155}, {0x20ac, 128}, {0x2122, 153},
};

constexpr uint16_t keypad_digit(unsigned digit) {
    return MODIFIER_L_ALT_FLAG | (digit ? KeyCode::KP_1 + digit - 1 : KeyCode::KP_0);
}

constexpr HostLayoutEntry alt_code(uint16_t unicode, unsigned byte) {
    // A zero entry releases Alt before the next character, including another
    // Alt code. The ZMK key shim treats HID usage zero as no key.
    return {unicode, 5, {keypad_digit(0), keypad_digit(byte / 100),
                         keypad_digit(byte / 10 % 10), keypad_digit(byte % 10), 0}};
}

struct WindowsHostLayout {
    HostLayout header = HostLayout::ansi;
    HostLayoutEntry entries[96 + sizeof(extra_characters) / sizeof(extra_characters[0])];

    WindowsHostLayout() {
        size_t index = 0;
        for (uint16_t code = 0xa0; code <= 0xff; ++code) {
            entries[index++] = alt_code(code, code);
        }
        for (const auto &entry : extra_characters) {
            entries[index++] = alt_code(entry.unicode, entry.byte);
        }
        header.entries.SetCount(index);
    }
};

static_assert(offsetof(WindowsHostLayout, entries) == sizeof(HostLayout));
} // namespace

const HostLayout &zmk_javelin_windows_us_layout() {
    static const WindowsHostLayout layout;
    return layout.header;
}
