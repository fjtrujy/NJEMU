#ifndef COMMON_UI_TEXTURE_LAYOUT_H
#define COMMON_UI_TEXTURE_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Materialize a 16-bit UI texture into conventional row-major storage.
 *
 * Some static atlases inherited from the PSP UI are stored as consecutive
 * 8x8 tiles. Linear backends use this adapter at upload time; PSP consumes
 * the tile-major source directly as a swizzled texture.
 */
static inline bool ui_texture_copy_to_linear16(uint16_t *dst, int dst_pitch,
	const uint16_t *src, int src_pitch, int width, int height,
	bool source_tiled8x8)
{
	uint16_t *temporary = NULL;
	uint16_t *out = dst;
	int out_pitch = dst_pitch;
	int x, y;

	if (!dst || !src || width <= 0 || height <= 0 ||
		dst_pitch < width || src_pitch < width)
		return false;

	if (!source_tiled8x8)
	{
		for (y = 0; y < height; y++)
			memmove(dst + (size_t)y * dst_pitch,
				src + (size_t)y * src_pitch,
				(size_t)width * sizeof(uint16_t));
		return true;
	}

	/* The static small-font upload aliases its backend staging buffer. Preserve
	 * the packed source while remapping it; non-aliased uploads can write the
	 * linear destination directly. */
	if (dst == src)
	{
		temporary = (uint16_t *)malloc(
			(size_t)width * height * sizeof(uint16_t));
		if (!temporary)
			return false;
		out = temporary;
		out_pitch = width;
	}

	{
		const int blocks_per_row = (src_pitch + 7) >> 3;

		for (y = 0; y < height; y++)
		{
			for (x = 0; x < width; x++)
			{
				const size_t block =
					(size_t)(y >> 3) * blocks_per_row + (x >> 3);
				const size_t offset = block * 64 +
					(size_t)(y & 7) * 8 + (x & 7);
				out[(size_t)y * out_pitch + x] = src[offset];
			}
		}
	}

	if (temporary)
	{
		for (y = 0; y < height; y++)
			memcpy(dst + (size_t)y * dst_pitch,
				temporary + (size_t)y * width,
				(size_t)width * sizeof(uint16_t));
		free(temporary);
	}

	return true;
}

#endif /* COMMON_UI_TEXTURE_LAYOUT_H */
