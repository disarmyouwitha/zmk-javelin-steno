#include "../../src/steno_squid/hint_index.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

static unsigned reads;
static uint32_t word(const void *p) {
  ++reads;
  assert((uintptr_t)p % 4 == 0);
  uint32_t value;
  memcpy(&value, p, 4);
  return value;
}
static uint8_t byte(const void *p) { ++reads; return *(const uint8_t *)p; }
int main(int argc, char **argv) {
  assert(argc == 3);
  std::ifstream file(argv[1], std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
  assert(data.size() > 48);
  const uintptr_t image = (uintptr_t)data.data();
  const uint32_t base = strtoul(argv[2], nullptr, 0);
  const uint32_t dictionaries = word(data.data() + 4) & 0xffff;
  size_t checks = 0;
  for (size_t index = 0; index < dictionaries; ++index) {
    const uint32_t definition = word(data.data() + 36 + 4 * index) - base;
    const uintptr_t text = image + word(data.data() + definition + 8) - base;
    StenoSquidHintIndex hints(image, data.size(), index, dictionaries, text,
                             word(data.data() + 8), word, byte);
    std::string query;
    while (std::getline(std::cin, query) && query != "--NEXT--") {
      const size_t tab = query.find('\t');
      assert(tab != std::string::npos);
      const uint32_t expected = strtoul(query.c_str(), nullptr, 10);
      uint32_t mask = 0;
      reads = 0;
      const bool found = hints.Find(query.c_str() + tab + 1, mask);
      assert(found == (expected != 0));
      assert(!found || mask == expected);
      assert(reads < 3000); // No linear scans of the flash dictionary.
      ++checks;
    }
    uint32_t mask;
    StenoSquidHintIndex legacy(image, 48, index, dictionaries, image, 4, word, byte);
    assert(!legacy.Find("hello", mask));
    StenoSquidHintIndex badText(image, data.size(), index, dictionaries,
                              image + data.size(), 4, word, byte);
    assert(!badText.Find("hello", mask));
  }
  printf("PASS: %zu indexed hint lookups; legacy and invalid images rejected\n", checks);
}
