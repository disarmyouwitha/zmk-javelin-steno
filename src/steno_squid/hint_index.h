#pragma once

#include <stddef.h>
#include <stdint.h>

// Optional SHI1 trailer on our JSC4 images. Offsets are image-relative, so
// existing relocatable dictionary pointers and the native format stay intact.
// Records are sorted by translation: uint32 text offset, uint32 single stroke.
// All reads go through the QSPI-safe word/byte accessors on the device.
class StenoSquidHintIndex {
public:
  using WordReader = uint32_t (*)(const void *);
  using ByteReader = uint8_t (*)(const void *);

  StenoSquidHintIndex(uintptr_t image, size_t bytes, size_t index, size_t dictionaries,
                     uintptr_t text, size_t textBytes, WordReader word, ByteReader byte)
      : text(text), textBytes(textBytes), word(word), byte(byte) {
    if (bytes < 12 || bytes % 4 || index >= dictionaries ||
        text < image || text - image > bytes || textBytes > bytes - (text - image)) {
      return;
    }
    const uintptr_t footer = image + bytes - 12;
    if (word((const void *)footer) != 0x31494853 ||
        word((const void *)(footer + 8)) != dictionaries) {
      return; // Legacy images have no hint index.
    }
    const uint32_t directory = word((const void *)(footer + 4));
    if (directory % 4 || directory > bytes - 12 ||
        dictionaries > (bytes - 12 - directory) / 8) {
      return;
    }
    const uintptr_t entry = image + directory + index * 8;
    const uint32_t offset = word((const void *)entry);
    const uint32_t entries = word((const void *)(entry + 4));
    if (offset % 4 || offset > directory || entries > (directory - offset) / 8) {
      return;
    }
    records = image + offset;
    count = entries;
  }

  bool Find(const char *translation, uint32_t &stroke) const {
    size_t left = 0, right = count;
    while (left < right) {
      const size_t mid = left + (right - left) / 2;
      const uintptr_t record = records + mid * 8;
      const uint32_t offset = word((const void *)record);
      if (offset >= textBytes) {
        return false;
      }
      size_t i = 0;
      int comparison;
      do {
        if (offset + i >= textBytes) {
          return false;
        }
        const uint8_t actual = byte((const void *)(text + offset + i));
        const uint8_t wanted = (uint8_t)translation[i];
        comparison = (int)wanted - actual;
        if (comparison || !wanted) {
          break;
        }
        ++i;
      } while (true);
      if (comparison < 0) {
        right = mid;
      } else if (comparison > 0) {
        left = mid + 1;
      } else {
        stroke = word((const void *)(record + 4));
        return stroke && !(stroke & ~0x7fffffu);
      }
    }
    return false;
  }

private:
  uintptr_t text, records = 0;
  size_t textBytes, count = 0;
  WordReader word;
  ByteReader byte;
};
