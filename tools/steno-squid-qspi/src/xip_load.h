#ifndef STENO_SQUID_XIP_LOAD_H
#define STENO_SQUID_XIP_LOAD_H

#include <stddef.h>
#include <stdint.h>

// Keep subword reads out of the memory window (erratum 118), and serialize
// each load before using its value in a dependent access (erratum 216).
static inline uint32_t xip_load_word(const void *address)
{
	const auto value = *static_cast<const volatile uint32_t *>(address);
	asm volatile("dsb" ::: "memory");
	return value;
}

static inline uint8_t xip_load_byte(const void *address)
{
	const auto offset = reinterpret_cast<uintptr_t>(address);
	const auto word = xip_load_word(reinterpret_cast<const void *>(offset & ~uintptr_t(3)));
	return static_cast<uint8_t>(word >> (8 * (offset & 3)));
}

static inline uint32_t xip_load_u24(const void *address)
{
	const auto *bytes = static_cast<const uint8_t *>(address);
	const uint32_t low = xip_load_byte(bytes);
	const uint32_t middle = xip_load_byte(bytes + 1);
	const uint32_t high = xip_load_byte(bytes + 2);
	return low | (middle << 8) | (high << 16);
}

static inline void copy_xip_bytes(void *destination, const void *address, size_t length)
{
	auto *out = static_cast<uint8_t *>(destination);
	const auto *in = static_cast<const uint8_t *>(address);
	for (size_t i = 0; i < length; ++i) {
		out[i] = xip_load_byte(in + i);
	}
}

#endif
