#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "common/palette_convert.h"

static uint16_t reference_neogeo_color(int r, int g, int b)
{
	uint16_t color = (uint16_t)(((r & 1) << 14) | ((r & 0x1e) << 7)
		| ((g & 1) << 13) | ((g & 0x1e) << 3)
		| ((b & 1) << 12) | ((b & 0x1e) >> 1));

	return (uint16_t)(r | (g << 5) | (b << 10));
}

static uint16_t reference_cps_color(uint16_t color)
{
	int brightness = (color >> 12) + 16;
	int r = (color >> 8) & 0x0f;
	int g = (color >> 4) & 0x0f;
	int b = color & 0x0f;
	float fr = (float)(r * brightness) / (15.0 * 31.0);
	float fg = (float)(g * brightness) / (15.0 * 31.0);
	float fb = (float)(b * brightness) / (15.0 * 31.0);
	int r2 = (int)(fr * 255.0) - 15;
	int g2 = (int)(fg * 255.0) - 15;
	int b2 = (int)(fb * 255.0) - 15;

	if (r2 < 0)
		r2 = 0;
	if (g2 < 0)
		g2 = 0;
	if (b2 < 0)
		b2 = 0;

	return (uint16_t)(((b2 & 0xf8) << 7)
		| ((g2 & 0xf8) << 2)
		| ((r2 & 0xf8) >> 3));
}

int main(void)
{
	uint8_t cps_lut[16][16];
	int r;
	int g;
	int b;
	uint32_t color;

	for (r = 0; r < 32; r++)
	{
		for (g = 0; g < 32; g++)
		{
			for (b = 0; b < 32; b++)
			{
				uint16_t encoded = (uint16_t)(((r & 1) << 14) | ((r & 0x1e) << 7)
					| ((g & 1) << 13) | ((g & 0x1e) << 3)
					| ((b & 1) << 12) | ((b & 0x1e) >> 1));

				assert(neogeo_palette_to_555(encoded) == reference_neogeo_color(r, g, b));
			}
		}
	}

	cps_palette_component_lut_init(cps_lut);
	for (color = 0; color <= UINT16_MAX; color++)
		assert(cps_palette_to_555(cps_lut, (uint16_t)color) == reference_cps_color((uint16_t)color));

	puts("palette conversion exhaustive validation passed");
	return 0;
}
