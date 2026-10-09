#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "mvs/native_word_access.h"

#define TEST_BYTES 65536u

static uint16_t aligned_storage[TEST_BYTES / 2];
static uint8_t reference_storage[TEST_BYTES];

static uint16_t reference_word(const uint8_t *memory, uint32_t address,
	uint32_t mask)
{
	return (uint16_t)(memory[address & mask] |
		(memory[(address + 1u) & mask] << 8));
}

static void reference_write(uint8_t *memory, uint32_t address,
	uint16_t data, uint32_t mask)
{
	memory[address & mask] = (uint8_t)data;
	memory[(address + 1u) & mask] = (uint8_t)(data >> 8);
}

static void test_reads(uint32_t mask)
{
	uint32_t address;
	uint8_t *actual = (uint8_t *)aligned_storage;

	for (address = 0; address < TEST_BYTES; address++)
		actual[address] = (uint8_t)(address * 31u + (address >> 8));

	for (address = 0; address < TEST_BYTES + 257u; address++) {
		assert(mvs_native_read_mirrored_word(actual, address, mask) ==
			reference_word(actual, address, mask));
		if (address < TEST_BYTES - 1u)
			assert(mvs_native_read_word(actual, address) ==
				(uint16_t)(actual[address] | (actual[address + 1u] << 8)));
	}
}

static void test_writes(uint32_t mask)
{
	uint32_t i;
	uint8_t *actual = (uint8_t *)aligned_storage;

	memset(actual, 0x69, TEST_BYTES);
	memset(reference_storage, 0x69, TEST_BYTES);
	for (i = 0; i < 8192u; i++) {
		uint32_t address = i * 377u + (i >> 2);
		uint16_t value = (uint16_t)((i << 5) ^ (i * 97u));
		mvs_native_write_mirrored_word(actual, address, value, mask);
		reference_write(reference_storage, address, value, mask);
	}
	assert(memcmp(actual, reference_storage, TEST_BYTES) == 0);
}

int main(void)
{
	test_reads(0x00ffu);
	test_reads(0xffffu);
	test_writes(0x00ffu);
	test_writes(0xffffu);
	return 0;
}
