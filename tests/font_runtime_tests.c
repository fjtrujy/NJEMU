#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "common/font/gbk_s14_runtime.h"

#ifndef TEST_FONT_ROOT
#error TEST_FONT_ROOT must point at the generated font asset directory
#endif

static void expect_glyph(uint16_t code, const uint8_t *expected, size_t size)
{
	struct font_t font;

	assert(gbk_s14p_get_gryph(&font, code));
	assert(font.width == 14);
	assert(font.height == 14);
	assert(font.pitch == 14);
	assert(font.skipx == 0);
	assert(font.skipy == 0);
	assert(memcmp(font.data, expected, size) == 0);
}

int main(void)
{
	static const uint8_t glyph1_prefix[] = {
		0x00, 0x00, 0x10, 0x24, 0x00, 0x00, 0x00, 0x00
	};
	static const uint8_t glyph5000_middle[] = {
		0x54, 0x06, 0x10, 0xf8, 0x07, 0x8f, 0x4f, 0x03
	};
	struct font_t font;
	int code;

	assert(gbk_s14_font_init(TEST_FONT_ROOT));
	assert(gbk_s14p_get_pitch(0) == 14);
	expect_glyph(1, glyph1_prefix, sizeof(glyph1_prefix));

	assert(gbk_s14p_get_gryph(&font, 5000));
	assert(memcmp(font.data + 40, glyph5000_middle, sizeof(glyph5000_middle)) == 0);

	/* Cross the 64-entry cache capacity, then verify an evicted glyph reloads. */
	for (code = 100; code < 180; code++)
		assert(gbk_s14p_get_gryph(&font, (uint16_t)code));
	expect_glyph(1, glyph1_prefix, sizeof(glyph1_prefix));

	assert(!gbk_s14p_get_gryph(&font, 0x5e80));
	gbk_s14_font_shutdown();
	return 0;
}
