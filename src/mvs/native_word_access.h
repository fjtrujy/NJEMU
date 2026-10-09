#ifndef MVS_NATIVE_WORD_ACCESS_H
#define MVS_NATIVE_WORD_ACCESS_H

#include <stdint.h>
#include <string.h>

#if !defined(__BYTE_ORDER__) || (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error MVS native word access requires a little-endian host
#endif

/* Neo Geo word buffers are stored little-endian and have aligned bases.
 * memcpy keeps C effective types valid. The alignment hint allows the
 * PS2 EE compiler to emit one halfword access on the even-address path. */
static inline uint16_t mvs_native_read_word(const uint8_t *memory, uint32_t address)
{
	if ((address & 1u) == 0) {
		uint16_t value;
		memcpy(&value, __builtin_assume_aligned(memory + address, 2), sizeof(value));
		return value;
	}
	return (uint16_t)(memory[address] | (memory[address + 1u] << 8));
}

static inline uint16_t mvs_native_read_mirrored_word(const uint8_t *memory,
	uint32_t address, uint32_t mask)
{
	uint32_t index = address & mask;
	if ((index & 1u) == 0) {
		uint16_t value;
		memcpy(&value, __builtin_assume_aligned(memory + index, 2), sizeof(value));
		return value;
	}
	return (uint16_t)(memory[index] | (memory[(address + 1u) & mask] << 8));
}

static inline void mvs_native_write_mirrored_word(uint8_t *memory,
	uint32_t address, uint32_t data, uint32_t mask)
{
	uint32_t index = address & mask;
	if ((index & 1u) == 0) {
		uint16_t value = (uint16_t)data;
		memcpy(__builtin_assume_aligned(memory + index, 2), &value, sizeof(value));
		return;
	}
	memory[index] = (uint8_t)data;
	memory[(address + 1u) & mask] = (uint8_t)(data >> 8);
}

#endif /* MVS_NATIVE_WORD_ACCESS_H */
