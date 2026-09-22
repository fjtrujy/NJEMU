#ifndef PALETTE_CONVERT_H
#define PALETTE_CONVERT_H

#include <stdint.h>

static inline uint16_t neogeo_palette_to_555(uint16_t color)
{
	uint16_t r = ((color >> 14) & 0x01) | ((color >> 7) & 0x1e);
	uint16_t g = ((color >> 13) & 0x01) | ((color >> 3) & 0x1e);
	uint16_t b = ((color >> 12) & 0x01) | ((color << 1) & 0x1e);

	return r | (g << 5) | (b << 10);
}

static inline uint8_t cps_palette_component_to_5(uint8_t component, uint8_t brightness)
{
	int value = ((int)component * ((int)brightness + 16) * 255) / (15 * 31) - 15;

	if (value < 0)
		value = 0;

	return (uint8_t)(value >> 3);
}

static inline void cps_palette_component_lut_init(uint8_t lut[16][16])
{
	int brightness;
	int component;

	for (brightness = 0; brightness < 16; brightness++)
	{
		for (component = 0; component < 16; component++)
		{
			lut[brightness][component] =
				cps_palette_component_to_5((uint8_t)component, (uint8_t)brightness);
		}
	}
}

static inline uint16_t cps_palette_to_555(const uint8_t lut[16][16], uint16_t color)
{
	const uint8_t *components = lut[color >> 12];

	return (uint16_t)(components[(color >> 8) & 0x0f]
		| (components[(color >> 4) & 0x0f] << 5)
		| (components[color & 0x0f] << 10));
}

#endif /* PALETTE_CONVERT_H */
