/******************************************************************************

	sprite_common.c

	CPS1 Common sprite management - shared across all platforms

	Hardware Reference:
	- https://fabiensanglard.net/cps1_gfx/index.html
	- https://arcadehacker.blogspot.com/2015/04/capcom-cps1-part-1.html

	This file contains platform-agnostic sprite management code:
	- Hash table based texture caching (avoids re-decoding sprites each frame)
	- Software rendering functions for SCROLL2 (used for line scroll effects)
	- Sprite cache management for OBJECT, SCROLL1/2/3, and SCROLLH layers

	CPS1 Graphics Data Format:
	Graphics ROMs use an interleaved planar format. When decoding a 32-bit
	word, pixels are NOT sequential but follow pattern: 0,4,1,5,2,6,3,7
	This is inherent to CPS1 hardware and must be preserved on all platforms.

******************************************************************************/

#include "sprite_common.h"

/******************************************************************************
	Shared variable definitions
******************************************************************************/

/* Palette */
uint8_t ALIGN_DATA palette_dirty_marks[256];

/* OBJECT */
SPRITE ALIGN_DATA *object_head[OBJECT_HASH_SIZE];
SPRITE ALIGN_DATA object_data[OBJECT_TEXTURE_SIZE];
SPRITE *object_free_head;
uint8_t *gfx_object;
uint8_t *tex_object;
uint16_t object_texture_num;

/* SCROLL1 */
SPRITE ALIGN_DATA *scroll1_head[SCROLL1_HASH_SIZE];
SPRITE ALIGN_DATA scroll1_data[SCROLL1_TEXTURE_SIZE];
SPRITE *scroll1_free_head;
uint8_t *gfx_scroll1;
uint8_t *tex_scroll1;
uint16_t scroll1_texture_num;

/* SCROLL2 */
SPRITE ALIGN_DATA *scroll2_head[SCROLL2_HASH_SIZE];
SPRITE ALIGN_DATA scroll2_data[SCROLL2_TEXTURE_SIZE];
SPRITE *scroll2_free_head;
uint8_t *gfx_scroll2;
uint8_t *tex_scroll2;
uint16_t scroll2_texture_num;

/* SCROLL3 */
SPRITE ALIGN_DATA *scroll3_head[SCROLL3_HASH_SIZE];
SPRITE ALIGN_DATA scroll3_data[SCROLL3_TEXTURE_SIZE];
SPRITE *scroll3_free_head;
uint8_t *gfx_scroll3;
uint8_t *tex_scroll3;
uint16_t scroll3_texture_num;

/* SCROLLH */
SPRITE ALIGN_DATA *scrollh_head[SCROLLH_HASH_SIZE];
SPRITE ALIGN_DATA scrollh_data[SCROLLH_TEXTURE_SIZE];
SPRITE *scrollh_free_head;
uint16_t *tex_scrollh;
uint16_t scrollh_num;
uint16_t scrollh_texture_num;
uint8_t scrollh_texture_clear;
uint8_t scrollh_layer_number;
uint8_t scroll1_palette_is_dirty;
uint8_t scroll2_palette_is_dirty;
uint8_t scroll3_palette_is_dirty;

/* Scroll2 clipping */
int16_t scroll2_min_y;
int16_t scroll2_max_y;
int16_t scroll2_sy;
int16_t scroll2_ey;

/* Pen usage */
uint8_t *pen_usage;

/* Screen bitmap */
uint16_t *scrbitmap;

/* CLUT */
uint16_t *clut;
uint16_t clut0_num;
uint16_t clut1_num;

/* Color table for palette index encoding
   Used to encode 4-bit palette indices into 8-bit texture format */
const uint32_t ALIGN_DATA color_table[16] =
{
	0x00000000, 0x10101010, 0x20202020, 0x30303030,
	0x40404040, 0x50505050, 0x60606060, 0x70707070,
	0x80808080, 0x90909090, 0xa0a0a0a0, 0xb0b0b0b0,
	0xc0c0c0c0, 0xd0d0d0d0, 0xe0e0e0e0, 0xf0f0f0f0
};

/* Function pointer arrays for software rendering */
void ALIGN_DATA (*drawgfx16[8])(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines) =
{
	drawgfx16_16x16,
	drawgfx16_16x16_opaque,
	drawgfx16_16x16_flipx,
	drawgfx16_16x16_flipx_opaque,
	drawgfx16_16x16_flipy,
	drawgfx16_16x16_flipy_opaque,
	drawgfx16_16x16_flipxy,
	drawgfx16_16x16_flipxy_opaque
};

void ALIGN_DATA (*drawgfx16h[4])(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines, uint16_t tpens) =
{
	drawgfx16h_16x16,
	drawgfx16h_16x16_flipx,
	drawgfx16h_16x16_flipy,
	drawgfx16h_16x16_flipxy
};


/******************************************************************************
	SCROLL2 Software Rendering

	Used when SCROLL2 clip region is too small for hardware rendering
	(less than 16 scanlines). This enables per-line parallax scrolling
	effects as seen in Street Fighter II stages.

	Note: The interleaved pixel pattern (0,4,1,5,2,6,3,7) is inherent to
	CPS1's graphics ROM format, NOT a platform optimization.
******************************************************************************/

/*------------------------------------------------------------------------
	16bpp 16x16 - Transparent pixels (color 0) are skipped
------------------------------------------------------------------------*/

void drawgfx16_16x16(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile, mask;

	while (lines--)
	{
		tile = src[0];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[ 0] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[ 4] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[ 1] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[ 5] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[ 2] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[ 6] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[ 3] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[ 7] = pal[(tile >> 28) & 0x0f];
		}
		tile = src[1];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[ 8] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[12] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[ 9] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[13] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[10] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[14] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[11] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[15] = pal[(tile >> 28) & 0x0f];
		}
		src += 2;
		dst += BUF_WIDTH;
	}
}

void drawgfx16_16x16_flipx(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile, mask;

	while (lines--)
	{
		tile = src[0];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[15] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[11] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[14] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[10] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[13] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[ 9] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[12] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[ 8] = pal[(tile >> 28) & 0x0f];
		}
		tile = src[1];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[ 7] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[ 3] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[ 6] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[ 2] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[ 5] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[ 1] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[ 4] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[ 0] = pal[(tile >> 28) & 0x0f];
		}
		src += 2;
		dst += BUF_WIDTH;
	}
}

void drawgfx16_16x16_flipy(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile, mask;

	while (lines--)
	{
		tile = src[0];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[ 0] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[ 4] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[ 1] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[ 5] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[ 2] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[ 6] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[ 3] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[ 7] = pal[(tile >> 28) & 0x0f];
		}
		tile = src[1];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[ 8] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[12] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[ 9] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[13] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[10] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[14] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[11] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[15] = pal[(tile >> 28) & 0x0f];
		}
		src += 2;
		dst -= BUF_WIDTH;
	}
}

void drawgfx16_16x16_flipxy(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile, mask;

	while (lines--)
	{
		tile = src[0];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[15] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[11] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[14] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[10] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[13] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[ 9] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[12] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[ 8] = pal[(tile >> 28) & 0x0f];
		}
		tile = src[1];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[ 7] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[ 3] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[ 6] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[ 2] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[ 5] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[ 1] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[ 4] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[ 0] = pal[(tile >> 28) & 0x0f];
		}
		src += 2;
		dst -= BUF_WIDTH;
	}
}


/*------------------------------------------------------------------------
	16bpp 16x16 (opaque)
------------------------------------------------------------------------*/

void drawgfx16_16x16_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile;

	while (lines--)
	{
		tile = src[0];
		dst[ 0] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 4] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 1] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 5] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 2] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 6] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 3] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 7] = pal[tile & 0x0f];
		tile = src[1];
		dst[ 8] = pal[tile & 0x0f]; tile >>= 4;
		dst[12] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 9] = pal[tile & 0x0f]; tile >>= 4;
		dst[13] = pal[tile & 0x0f]; tile >>= 4;
		dst[10] = pal[tile & 0x0f]; tile >>= 4;
		dst[14] = pal[tile & 0x0f]; tile >>= 4;
		dst[11] = pal[tile & 0x0f]; tile >>= 4;
		dst[15] = pal[tile & 0x0f];
		src += 2;
		dst += BUF_WIDTH;
	}
}

void drawgfx16_16x16_flipx_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile;

	while (lines--)
	{
		tile = src[0];
		dst[15] = pal[tile & 0x0f]; tile >>= 4;
		dst[11] = pal[tile & 0x0f]; tile >>= 4;
		dst[14] = pal[tile & 0x0f]; tile >>= 4;
		dst[10] = pal[tile & 0x0f]; tile >>= 4;
		dst[13] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 9] = pal[tile & 0x0f]; tile >>= 4;
		dst[12] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 8] = pal[tile & 0x0f];
		tile = src[1];
		dst[ 7] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 3] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 6] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 2] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 5] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 1] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 4] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 0] = pal[tile & 0x0f];
		src += 2;
		dst += BUF_WIDTH;
	}
}

void drawgfx16_16x16_flipy_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile;

	while (lines--)
	{
		tile = src[0];
		dst[ 0] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 4] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 1] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 5] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 2] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 6] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 3] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 7] = pal[tile & 0x0f];
		tile = src[1];
		dst[ 8] = pal[tile & 0x0f]; tile >>= 4;
		dst[12] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 9] = pal[tile & 0x0f]; tile >>= 4;
		dst[13] = pal[tile & 0x0f]; tile >>= 4;
		dst[10] = pal[tile & 0x0f]; tile >>= 4;
		dst[14] = pal[tile & 0x0f]; tile >>= 4;
		dst[11] = pal[tile & 0x0f]; tile >>= 4;
		dst[15] = pal[tile & 0x0f];
		src += 2;
		dst -= BUF_WIDTH;
	}
}

void drawgfx16_16x16_flipxy_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile;

	while (lines--)
	{
		tile = src[0];
		dst[15] = pal[tile & 0x0f]; tile >>= 4;
		dst[11] = pal[tile & 0x0f]; tile >>= 4;
		dst[14] = pal[tile & 0x0f]; tile >>= 4;
		dst[10] = pal[tile & 0x0f]; tile >>= 4;
		dst[13] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 9] = pal[tile & 0x0f]; tile >>= 4;
		dst[12] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 8] = pal[tile & 0x0f];
		tile = src[1];
		dst[ 7] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 3] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 6] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 2] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 5] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 1] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 4] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 0] = pal[tile & 0x0f];
		src += 2;
		dst -= BUF_WIDTH;
	}
}


/*------------------------------------------------------------------------
	16bpp 16x16 (high layer)
------------------------------------------------------------------------*/

void drawgfx16h_16x16(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines, uint16_t tpens)
{
	uint32_t tile;
	uint16_t col;

	while (lines--)
	{
		tile = src[0];
		if (~tile)
		{
			col = (tile >>  0) & 0x0f; if (tpens & (1 << col)) dst[ 0] = pal[col];
			col = (tile >>  4) & 0x0f; if (tpens & (1 << col)) dst[ 4] = pal[col];
			col = (tile >>  8) & 0x0f; if (tpens & (1 << col)) dst[ 1] = pal[col];
			col = (tile >> 12) & 0x0f; if (tpens & (1 << col)) dst[ 5] = pal[col];
			col = (tile >> 16) & 0x0f; if (tpens & (1 << col)) dst[ 2] = pal[col];
			col = (tile >> 20) & 0x0f; if (tpens & (1 << col)) dst[ 6] = pal[col];
			col = (tile >> 24) & 0x0f; if (tpens & (1 << col)) dst[ 3] = pal[col];
			col = (tile >> 28) & 0x0f; if (tpens & (1 << col)) dst[ 7] = pal[col];
		}
		tile = src[1];
		if (~tile)
		{
			col = (tile >>  0) & 0x0f; if (tpens & (1 << col)) dst[ 8] = pal[col];
			col = (tile >>  4) & 0x0f; if (tpens & (1 << col)) dst[12] = pal[col];
			col = (tile >>  8) & 0x0f; if (tpens & (1 << col)) dst[ 9] = pal[col];
			col = (tile >> 12) & 0x0f; if (tpens & (1 << col)) dst[13] = pal[col];
			col = (tile >> 16) & 0x0f; if (tpens & (1 << col)) dst[10] = pal[col];
			col = (tile >> 20) & 0x0f; if (tpens & (1 << col)) dst[14] = pal[col];
			col = (tile >> 24) & 0x0f; if (tpens & (1 << col)) dst[11] = pal[col];
			col = (tile >> 28) & 0x0f; if (tpens & (1 << col)) dst[15] = pal[col];
		}
		src += 2;
		dst += BUF_WIDTH;
	}
}

void drawgfx16h_16x16_flipx(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines, uint16_t tpens)
{
	uint32_t tile;
	uint16_t col;

	while (lines--)
	{
		tile = src[0];
		if (~tile)
		{
			col = (tile >>  0) & 0x0f; if (tpens & (1 << col)) dst[15] = pal[col];
			col = (tile >>  4) & 0x0f; if (tpens & (1 << col)) dst[11] = pal[col];
			col = (tile >>  8) & 0x0f; if (tpens & (1 << col)) dst[14] = pal[col];
			col = (tile >> 12) & 0x0f; if (tpens & (1 << col)) dst[10] = pal[col];
			col = (tile >> 16) & 0x0f; if (tpens & (1 << col)) dst[13] = pal[col];
			col = (tile >> 20) & 0x0f; if (tpens & (1 << col)) dst[ 9] = pal[col];
			col = (tile >> 24) & 0x0f; if (tpens & (1 << col)) dst[12] = pal[col];
			col = (tile >> 28) & 0x0f; if (tpens & (1 << col)) dst[ 8] = pal[col];
		}
		tile = src[1];
		if (~tile)
		{
			col = (tile >>  0) & 0x0f; if (tpens & (1 << col)) dst[ 7] = pal[col];
			col = (tile >>  4) & 0x0f; if (tpens & (1 << col)) dst[ 3] = pal[col];
			col = (tile >>  8) & 0x0f; if (tpens & (1 << col)) dst[ 6] = pal[col];
			col = (tile >> 12) & 0x0f; if (tpens & (1 << col)) dst[ 2] = pal[col];
			col = (tile >> 16) & 0x0f; if (tpens & (1 << col)) dst[ 5] = pal[col];
			col = (tile >> 20) & 0x0f; if (tpens & (1 << col)) dst[ 1] = pal[col];
			col = (tile >> 24) & 0x0f; if (tpens & (1 << col)) dst[ 4] = pal[col];
			col = (tile >> 28) & 0x0f; if (tpens & (1 << col)) dst[ 0] = pal[col];
		}
		src += 2;
		dst += BUF_WIDTH;
	}
}

void drawgfx16h_16x16_flipy(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines, uint16_t tpens)
{
	uint32_t tile;
	uint16_t col;

	while (lines--)
	{
		tile = src[0];
		if (~tile)
		{
			col = (tile >>  0) & 0x0f; if (tpens & (1 << col)) dst[ 0] = pal[col];
			col = (tile >>  4) & 0x0f; if (tpens & (1 << col)) dst[ 4] = pal[col];
			col = (tile >>  8) & 0x0f; if (tpens & (1 << col)) dst[ 1] = pal[col];
			col = (tile >> 12) & 0x0f; if (tpens & (1 << col)) dst[ 5] = pal[col];
			col = (tile >> 16) & 0x0f; if (tpens & (1 << col)) dst[ 2] = pal[col];
			col = (tile >> 20) & 0x0f; if (tpens & (1 << col)) dst[ 6] = pal[col];
			col = (tile >> 24) & 0x0f; if (tpens & (1 << col)) dst[ 3] = pal[col];
			col = (tile >> 28) & 0x0f; if (tpens & (1 << col)) dst[ 7] = pal[col];
		}
		tile = src[1];
		if (~tile)
		{
			col = (tile >>  0) & 0x0f; if (tpens & (1 << col)) dst[ 8] = pal[col];
			col = (tile >>  4) & 0x0f; if (tpens & (1 << col)) dst[12] = pal[col];
			col = (tile >>  8) & 0x0f; if (tpens & (1 << col)) dst[ 9] = pal[col];
			col = (tile >> 12) & 0x0f; if (tpens & (1 << col)) dst[13] = pal[col];
			col = (tile >> 16) & 0x0f; if (tpens & (1 << col)) dst[10] = pal[col];
			col = (tile >> 20) & 0x0f; if (tpens & (1 << col)) dst[14] = pal[col];
			col = (tile >> 24) & 0x0f; if (tpens & (1 << col)) dst[11] = pal[col];
			col = (tile >> 28) & 0x0f; if (tpens & (1 << col)) dst[15] = pal[col];
		}
		src += 2;
		dst -= BUF_WIDTH;
	}
}

void drawgfx16h_16x16_flipxy(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines, uint16_t tpens)
{
	uint32_t tile;
	uint16_t col;

	while (lines--)
	{
		tile = src[0];
		if (~tile)
		{
			col = (tile >>  0) & 0x0f; if (tpens & (1 << col)) dst[15] = pal[col];
			col = (tile >>  4) & 0x0f; if (tpens & (1 << col)) dst[11] = pal[col];
			col = (tile >>  8) & 0x0f; if (tpens & (1 << col)) dst[14] = pal[col];
			col = (tile >> 12) & 0x0f; if (tpens & (1 << col)) dst[10] = pal[col];
			col = (tile >> 16) & 0x0f; if (tpens & (1 << col)) dst[13] = pal[col];
			col = (tile >> 20) & 0x0f; if (tpens & (1 << col)) dst[ 9] = pal[col];
			col = (tile >> 24) & 0x0f; if (tpens & (1 << col)) dst[12] = pal[col];
			col = (tile >> 28) & 0x0f; if (tpens & (1 << col)) dst[ 8] = pal[col];
		}
		tile = src[1];
		if (~tile)
		{
			col = (tile >>  0) & 0x0f; if (tpens & (1 << col)) dst[ 7] = pal[col];
			col = (tile >>  4) & 0x0f; if (tpens & (1 << col)) dst[ 3] = pal[col];
			col = (tile >>  8) & 0x0f; if (tpens & (1 << col)) dst[ 6] = pal[col];
			col = (tile >> 12) & 0x0f; if (tpens & (1 << col)) dst[ 2] = pal[col];
			col = (tile >> 16) & 0x0f; if (tpens & (1 << col)) dst[ 5] = pal[col];
			col = (tile >> 20) & 0x0f; if (tpens & (1 << col)) dst[ 1] = pal[col];
			col = (tile >> 24) & 0x0f; if (tpens & (1 << col)) dst[ 4] = pal[col];
			col = (tile >> 28) & 0x0f; if (tpens & (1 << col)) dst[ 0] = pal[col];
		}
		src += 2;
		dst -= BUF_WIDTH;
	}
}


/******************************************************************************
	OBJECT Sprite Management
******************************************************************************/

/*------------------------------------------------------------------------
	Get sprite number from OBJECT texture
------------------------------------------------------------------------*/

int16_t object_get_sprite(uint32_t key)
{
	SPRITE *p = object_head[key & OBJECT_HASH_MASK];

	while (p)
	{
		if (p->key == key)
		{
			p->used = frames_displayed;
			return p->index;
	 	}
		p = p->next;
	}
	return -1;
}


/*------------------------------------------------------------------------
	Register sprite in OBJECT texture
------------------------------------------------------------------------*/

int16_t object_insert_sprite(uint32_t key)
{
	uint16_t hash = key & OBJECT_HASH_MASK;
	SPRITE *p = object_head[hash];
	SPRITE *q = object_free_head;

	if (!q) return -1;

	object_free_head = object_free_head->next;

	q->next = NULL;
	q->key  = key;
	q->used = frames_displayed;

	if (!p)
	{
		object_head[hash] = q;
	}
	else
	{
		while (p->next) p = p->next;
		p->next = q;
	}

	object_texture_num++;

	return q->index;
}


/*------------------------------------------------------------------------
	Delete expired sprites from OBJECT texture
------------------------------------------------------------------------*/

void object_delete_sprite(void)
{
	int i;
	SPRITE *p, *prev_p;

	for (i = 0; i < OBJECT_HASH_SIZE; i++)
	{
		prev_p = NULL;
		p = object_head[i];

		while (p)
		{
			if (frames_displayed != p->used)
			{
				object_texture_num--;

				if (!prev_p)
				{
					object_head[i] = p->next;
					p->next = object_free_head;
					object_free_head = p;
					p = object_head[i];
				}
				else
				{
					prev_p->next = p->next;
					p->next = object_free_head;
					object_free_head = p;
					p = prev_p->next;
				}
			}
			else
			{
				prev_p = p;
				p = p->next;
			}
		}
	}
}


/******************************************************************************
	SCROLL1 Sprite Management
******************************************************************************/

/*------------------------------------------------------------------------
	Get sprite number from SCROLL1 texture
------------------------------------------------------------------------*/

int16_t scroll1_get_sprite(uint32_t key)
{
	SPRITE *p = scroll1_head[key & SCROLL1_HASH_MASK];

	while (p)
	{
		if (p->key == key)
		{
			p->used = frames_displayed;
			return p->index;
	 	}
		p = p->next;
	}
	return -1;
}


/*------------------------------------------------------------------------
	Register sprite in SCROLL1 texture
------------------------------------------------------------------------*/

int16_t scroll1_insert_sprite(uint32_t key)
{
	uint16_t hash = key & SCROLL1_HASH_MASK;
	SPRITE *p = scroll1_head[hash];
	SPRITE *q = scroll1_free_head;

	if (!q) return -1;

	scroll1_free_head = scroll1_free_head->next;

	q->next = NULL;
	q->key  = key;
	q->used = frames_displayed;

	if (!p)
	{
		scroll1_head[hash] = q;
	}
	else
	{
		while (p->next) p = p->next;
		p->next = q;
	}

	scroll1_texture_num++;

	return q->index;
}


/*------------------------------------------------------------------------
	Delete expired sprites from SCROLL1 texture
------------------------------------------------------------------------*/

void scroll1_delete_sprite(void)
{
	int i;
	SPRITE *p, *prev_p;

	for (i = 0; i < SCROLL1_HASH_SIZE; i++)
	{
		prev_p = NULL;
		p = scroll1_head[i];

		while (p)
		{
			if (frames_displayed != p->used)
			{
				scroll1_texture_num--;

				if (!prev_p)
				{
					scroll1_head[i] = p->next;
					p->next = scroll1_free_head;
					scroll1_free_head = p;
					p = scroll1_head[i];
				}
				else
				{
					prev_p->next = p->next;
					p->next = scroll1_free_head;
					scroll1_free_head = p;
					p = prev_p->next;
				}
			}
			else
			{
				prev_p = p;
				p = p->next;
			}
		}
	}
}


/******************************************************************************
	SCROLL2 Sprite Management
******************************************************************************/

/*------------------------------------------------------------------------
	Get sprite number from SCROLL2 texture
------------------------------------------------------------------------*/

int16_t scroll2_get_sprite(uint32_t key)
{
	SPRITE *p = scroll2_head[key & SCROLL2_HASH_MASK];

	while (p)
	{
		if (p->key == key)
		{
			p->used = frames_displayed;
			return p->index;
	 	}
		p = p->next;
	}
	return -1;
}


/*------------------------------------------------------------------------
	Register sprite in SCROLL2 texture
------------------------------------------------------------------------*/

int16_t scroll2_insert_sprite(uint32_t key)
{
	uint16_t hash = key & SCROLL2_HASH_MASK;
	SPRITE *p = scroll2_head[hash];
	SPRITE *q = scroll2_free_head;

	if (!q) return -1;

	scroll2_free_head = scroll2_free_head->next;

	q->next = NULL;
	q->key  = key;
	q->used = frames_displayed;

	if (!p)
	{
		scroll2_head[hash] = q;
	}
	else
	{
		while (p->next) p = p->next;
		p->next = q;
	}

	scroll2_texture_num++;

	return q->index;
}


/*------------------------------------------------------------------------
	Delete expired sprites from SCROLL2 texture
------------------------------------------------------------------------*/

void scroll2_delete_sprite(void)
{
	int i;
	SPRITE *p, *prev_p;

	for (i = 0; i < SCROLL2_HASH_SIZE; i++)
	{
		prev_p = NULL;
		p = scroll2_head[i];

		while (p)
		{
			if (frames_displayed != p->used)
			{
				scroll2_texture_num--;

				if (!prev_p)
				{
					scroll2_head[i] = p->next;
					p->next = scroll2_free_head;
					scroll2_free_head = p;
					p = scroll2_head[i];
				}
				else
				{
					prev_p->next = p->next;
					p->next = scroll2_free_head;
					scroll2_free_head = p;
					p = prev_p->next;
				}
			}
			else
			{
				prev_p = p;
				p = p->next;
			}
		}
	}
}


/******************************************************************************
	SCROLL3 Sprite Management
******************************************************************************/

/*------------------------------------------------------------------------
	Get sprite number from SCROLL3 texture
------------------------------------------------------------------------*/

int16_t scroll3_get_sprite(uint32_t key)
{
	SPRITE *p = scroll3_head[key & SCROLL3_HASH_MASK];

	while (p)
	{
		if (p->key == key)
		{
			p->used = frames_displayed;
			return p->index;
	 	}
		p = p->next;
	}
	return -1;
}


/*------------------------------------------------------------------------
	Register sprite in SCROLL3 texture
------------------------------------------------------------------------*/

int16_t scroll3_insert_sprite(uint32_t key)
{
	uint16_t hash = key & SCROLL3_HASH_MASK;
	SPRITE *p = scroll3_head[hash];
	SPRITE *q = scroll3_free_head;

	if (!q) return -1;

	scroll3_free_head = scroll3_free_head->next;

	q->next = NULL;
	q->key  = key;
	q->used = frames_displayed;

	if (!p)
	{
		scroll3_head[hash] = q;
	}
	else
	{
		while (p->next) p = p->next;
		p->next = q;
	}

	scroll3_texture_num++;

	return q->index;
}


/*------------------------------------------------------------------------
	Delete expired sprites from SCROLL3 texture
------------------------------------------------------------------------*/

void scroll3_delete_sprite(void)
{
	int i;
	SPRITE *p, *prev_p;

	for (i = 0; i < SCROLL3_HASH_SIZE; i++)
	{
		prev_p = NULL;
		p = scroll3_head[i];

		while (p)
		{
			if (frames_displayed != p->used)
			{
				scroll3_texture_num--;

				if (!prev_p)
				{
					scroll3_head[i] = p->next;
					p->next = scroll3_free_head;
					scroll3_free_head = p;
					p = scroll3_head[i];
				}
				else
				{
					prev_p->next = p->next;
					p->next = scroll3_free_head;
					scroll3_free_head = p;
					p = prev_p->next;
				}
			}
			else
			{
				prev_p = p;
				p = p->next;
			}
		}
	}
}


/******************************************************************************
	SCROLLH Sprite Management
******************************************************************************/

/*------------------------------------------------------------------------
	Reset SCROLLH texture
------------------------------------------------------------------------*/

void scrollh_reset_sprite(void)
{
	int i;

	for (i = 0; i < SCROLLH_TEXTURE_SIZE - 1; i++)
		scrollh_data[i].next = &scrollh_data[i + 1];

	scrollh_data[i].next = NULL;
	scrollh_free_head = &scrollh_data[0];

	memset(scrollh_head, 0, sizeof(SPRITE *) * SCROLLH_HASH_SIZE);

	scrollh_texture_num = 0;
	scrollh_texture_clear = 0;
	scrollh_layer_number = 0;

	scroll1_palette_is_dirty = 0;
	scroll2_palette_is_dirty = 0;
	scroll3_palette_is_dirty = 0;
}


/*------------------------------------------------------------------------
	Get sprite number from SCROLLH texture
------------------------------------------------------------------------*/

int16_t scrollh_get_sprite(uint32_t key)
{
	SPRITE *p = scrollh_head[key & SCROLLH_HASH_MASK];

	while (p)
	{
		if (p->key == key)
		{
			p->used = frames_displayed;
			return p->index;
	 	}
		p = p->next;
	}
	return -1;
}


/*------------------------------------------------------------------------
	Register sprite in SCROLLH texture
------------------------------------------------------------------------*/

int16_t scrollh_insert_sprite(uint32_t key)
{
	uint16_t hash = key & SCROLLH_HASH_MASK;
	SPRITE *p = scrollh_head[hash];
	SPRITE *q = scrollh_free_head;

	if (!q) return -1;

	scrollh_free_head = scrollh_free_head->next;

	q->next = NULL;
	q->key  = key;
	q->pal  = (key >> 16) & 0x1f;
	q->used = frames_displayed;

	if (!p)
	{
		scrollh_head[hash] = q;
	}
	else
	{
		while (p->next) p = p->next;
		p->next = q;
	}

	scrollh_texture_num++;

	return q->index;
}


/*------------------------------------------------------------------------
	Delete expired sprites from SCROLLH texture
------------------------------------------------------------------------*/

void scrollh_delete_sprite(void)
{
	int i;
	SPRITE *p, *prev_p;

	for (i = 0; i < SCROLLH_HASH_SIZE; i++)
	{
		prev_p = NULL;
		p = scrollh_head[i];

		while (p)
		{
			if (frames_displayed != p->used)
			{
				scrollh_texture_num--;

				if (!prev_p)
				{
					scrollh_head[i] = p->next;
					p->next = scrollh_free_head;
					scrollh_free_head = p;
					p = scrollh_head[i];
				}
				else
				{
					prev_p->next = p->next;
					p->next = scrollh_free_head;
					scrollh_free_head = p;
					p = prev_p->next;
				}
			}
			else
			{
				prev_p = p;
				p = p->next;
			}
		}
	}
}


/*------------------------------------------------------------------------
	Delete sprites with specified transparent pens from SCROLLH cache
------------------------------------------------------------------------*/

void scrollh_delete_sprite_tpens(uint16_t tpens)
{
	int i;
	SPRITE *p, *prev_p;

	for (i = 0; i < SCROLLH_HASH_SIZE; i++)
	{
		prev_p = NULL;
		p = scrollh_head[i];

		while (p)
		{
			if ((p->key >> 23) == tpens)
			{
				scrollh_texture_num--;

				if (!prev_p)
				{
					scrollh_head[i] = p->next;
					p->next = scrollh_free_head;
					scrollh_free_head = p;
					p = scrollh_head[i];
				}
				else
				{
					prev_p->next = p->next;
					p->next = scrollh_free_head;
					scrollh_free_head = p;
					p = prev_p->next;
				}
			}
			else
			{
				prev_p = p;
				p = p->next;
			}
		}
	}
}


/*------------------------------------------------------------------------
	Delete sprites with dirty palette from SCROLLH texture
------------------------------------------------------------------------*/

void scrollh_delete_dirty_palette(void)
{
	int palno;
	uint8_t *dirty_flags = NULL;

	switch (scrollh_layer_number)
	{
	case LAYER_SCROLL1: if (scroll1_palette_is_dirty) dirty_flags = &palette_dirty_marks[32]; break;
	case LAYER_SCROLL2: if (scroll2_palette_is_dirty) dirty_flags = &palette_dirty_marks[64]; break;
	case LAYER_SCROLL3: if (scroll3_palette_is_dirty) dirty_flags = &palette_dirty_marks[96]; break;
	}

	scroll1_palette_is_dirty = 0;
	scroll2_palette_is_dirty = 0;
	scroll3_palette_is_dirty = 0;

	if (!dirty_flags || !scrollh_texture_num) return;

	for (palno = 0; palno < 32; palno++)
	{
		int i;
		SPRITE *p, *prev_p;

		if (!dirty_flags[palno]) continue;

		for (i = 0; i < SCROLLH_HASH_SIZE; i++)
		{
			prev_p = NULL;
			p = scrollh_head[i];

			while (p)
			{
				if (p->pal == palno)
				{
					scrollh_texture_num--;

					if (!prev_p)
					{
						scrollh_head[i] = p->next;
						p->next = scrollh_free_head;
						scrollh_free_head = p;
						p = scrollh_head[i];
					}
					else
					{
						prev_p->next = p->next;
						p->next = scrollh_free_head;
						scrollh_free_head = p;
						p = prev_p->next;
					}
				}
				else
				{
					prev_p = p;
					p = p->next;
				}
			}
		}
	}
}

