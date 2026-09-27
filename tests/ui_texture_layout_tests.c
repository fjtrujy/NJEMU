#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "common/ui_texture_layout.h"

static void build_tiled_16x16(uint16_t *packed)
{
	int x, y;

	for (y = 0; y < 16; y++)
	{
		for (x = 0; x < 16; x++)
		{
			const int block = (y >> 3) * 2 + (x >> 3);
			const int offset = block * 64 + (y & 7) * 8 + (x & 7);
			packed[offset] = (uint16_t)(y * 16 + x + 1);
		}
	}
}

static void verify_linear_16x16(const uint16_t *linear)
{
	int x, y;

	for (y = 0; y < 16; y++)
		for (x = 0; x < 16; x++)
			assert(linear[y * 16 + x] == (uint16_t)(y * 16 + x + 1));
}

static void test_tiled_copy(void)
{
	uint16_t packed[16 * 16];
	uint16_t linear[16 * 16];

	build_tiled_16x16(packed);
	memset(linear, 0, sizeof(linear));
	assert(ui_texture_copy_to_linear16(linear, 16, packed, 16, 16, 16, true));
	verify_linear_16x16(linear);
}

static void test_tiled_alias_copy(void)
{
	uint16_t buffer[16 * 16];

	build_tiled_16x16(buffer);
	assert(ui_texture_copy_to_linear16(buffer, 16, buffer, 16, 16, 16, true));
	verify_linear_16x16(buffer);
}

static void test_linear_copy_with_pitch(void)
{
	uint16_t src[3 * 6] = {
		1, 2, 3, 4, 90, 91,
		5, 6, 7, 8, 92, 93,
		9, 10, 11, 12, 94, 95,
	};
	uint16_t dst[3 * 5];

	memset(dst, 0, sizeof(dst));
	assert(ui_texture_copy_to_linear16(dst, 5, src, 6, 4, 3, false));
	assert(dst[0] == 1 && dst[1] == 2 && dst[2] == 3 && dst[3] == 4);
	assert(dst[5] == 5 && dst[6] == 6 && dst[7] == 7 && dst[8] == 8);
	assert(dst[10] == 9 && dst[11] == 10 && dst[12] == 11 && dst[13] == 12);
}

int main(void)
{
	test_tiled_copy();
	test_tiled_alias_copy();
	test_linear_copy_with_pitch();
	return 0;
}
