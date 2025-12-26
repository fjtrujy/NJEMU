/******************************************************************************

	sprite_common.c

	MVS Common sprite management - shared across all platforms

******************************************************************************/

#include "sprite_common.h"

/******************************************************************************
	Shared variable definitions
******************************************************************************/

SPRITE ALIGN_DATA *fix_head[FIX_HASH_SIZE];
SPRITE ALIGN_DATA fix_data[FIX_TEXTURE_SIZE];
SPRITE *fix_free_head;
uint16_t fix_num;
uint16_t fix_texture_num;

SPRITE ALIGN_DATA *spr_head[SPR_HASH_SIZE];
SPRITE ALIGN_DATA spr_data[SPR_TEXTURE_SIZE];
SPRITE *spr_free_head;
uint16_t spr_num;
uint16_t spr_texture_num;
uint16_t spr_index;
uint8_t spr_disable;

int clip_min_y;
int clip_max_y;
int clear_spr_texture;
int clear_fix_texture;

uint16_t *scrbitmap;
uint8_t *tex_fix;
uint8_t *tex_spr[3];
uint16_t *clut;

/*
 * Color table for palette index encoding
 * Each entry broadcasts a 4-bit palette offset to all bytes of a 32-bit word
 * Used when rendering tiles to texture cache
 */
const uint32_t ALIGN_DATA color_table[16] =
{
	0x00000000, 0x10101010, 0x20202020, 0x30303030,
	0x40404040, 0x50505050, 0x60606060, 0x70707070,
	0x80808080, 0x90909090, 0xa0a0a0a0, 0xb0b0b0b0,
	0xc0c0c0c0, 0xd0d0d0d0, 0xe0e0e0e0, 0xf0f0f0f0
};

/*
 * Horizontal shrink pixel-skip tables
 * Reference: https://wiki.neogeodev.org/index.php?title=Sprite_shrinking
 *
 * Neo Geo sprites can only SHRINK (not zoom). The hardware uses pixel-skipping
 * with no interpolation. Each table entry indicates which of the 16 pixels
 * in a sprite row should be displayed (1) or skipped (0).
 *
 * Hardware SCB2 upper nibble values:
 *   $F = full size (16 pixels) - uses fixed rendering path, not this table
 *   $E = 15 pixels
 *   ...
 *   $1 = 2 pixels
 *   $0 = 1 pixel (at position 8)
 *
 * Table indexing: hardware_value + 1, so:
 *   - Index 0 is unused (hardware $0 maps to index 1)
 *   - Index 1 = 1 pixel (hardware $0)
 *   - Index 15 = 15 pixels (hardware $E)
 *   - Index 16 would be full size, but that uses drawgfxline_fixed() instead
 *
 * The pixel positions in each table are optimized for even visual distribution.
 */
const uint8_t zoom_x_tables[][16] =
{
	{ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },  /* Index 0: unused (never accessed) */
	{ 0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0 },  /* Index 1: 1 pixel at center (hardware $0) */
	{ 0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0 },  /* Index 2: 2 pixels */
	{ 0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0 },  /* Index 3: 3 pixels */
	{ 0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0 },  /* Index 4: 4 pixels */
	{ 0,0,1,0,1,0,0,0,1,0,0,0,1,0,1,0 },  /* Index 5: 5 pixels */
	{ 0,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0 },  /* Index 6: 6 pixels */
	{ 0,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0 },  /* Index 7: 7 pixels (alternating) */
	{ 1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0 },  /* Index 8: 8 pixels (every other) */
	{ 1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0 },  /* Index 9: 9 pixels */
	{ 1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0 },  /* Index 10: 10 pixels */
	{ 1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1 },  /* Index 11: 11 pixels */
	{ 1,0,1,1,1,0,1,1,1,1,1,0,1,0,1,1 },  /* Index 12: 12 pixels */
	{ 1,0,1,1,1,0,1,1,1,1,1,0,1,1,1,1 },  /* Index 13: 13 pixels */
	{ 1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1 },  /* Index 14: 14 pixels */
	{ 1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1 },  /* Index 15: 15 pixels (hardware $E) */
	/* Index 16 (hardware $F = full) uses drawgfxline_fixed() instead */
};

void ALIGN_DATA (*drawgfxline[8])(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom) =
{
	drawgfxline_zoom,					// 0000
	drawgfxline_zoom_flip,				// 0001
	drawgfxline_zoom_opaque,			// 0010
	drawgfxline_zoom_flip_opaque,		// 0011
	drawgfxline_fixed,					// 0100
	drawgfxline_fixed_flip,				// 0101
	drawgfxline_fixed_opaque,			// 0110
	drawgfxline_fixed_flip_opaque		// 0111
};


/******************************************************************************
	FIX Sprite Management
******************************************************************************/

/*------------------------------------------------------------------------
	Get sprite number from FIX texture
------------------------------------------------------------------------*/

int fix_get_sprite(uint32_t key)
{
	SPRITE *p = fix_head[key & FIX_HASH_MASK];

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
	Register sprite in FIX texture
------------------------------------------------------------------------*/

int fix_insert_sprite(uint32_t key)
{
	uint16_t hash = key & FIX_HASH_MASK;
	SPRITE *p = fix_head[hash];
	SPRITE *q = fix_free_head;

	if (!q) return -1;

	fix_free_head = fix_free_head->next;

	q->next = NULL;
	q->key  = key;
	q->used = frames_displayed;

	if (!p)
	{
		fix_head[hash] = q;
	}
	else
	{
		while (p->next) p = p->next;
		p->next = q;
	}

	fix_texture_num++;

	return q->index;
}


/*------------------------------------------------------------------------
	Delete expired sprites from FIX texture
------------------------------------------------------------------------*/

void fix_delete_sprite(void)
{
	int i;
	SPRITE *p, *prev_p;

	for (i = 0; i < FIX_HASH_SIZE; i++)
	{
		prev_p = NULL;
		p = fix_head[i];

		while (p)
		{
			if (frames_displayed != p->used)
			{
				fix_texture_num--;

				if (!prev_p)
				{
					fix_head[i] = p->next;
					p->next = fix_free_head;
					fix_free_head = p;
					p = fix_head[i];
				}
				else
				{
					prev_p->next = p->next;
					p->next = fix_free_head;
					fix_free_head = p;
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
	SPR Sprite Management
******************************************************************************/

/*------------------------------------------------------------------------
	Get sprite number from SPR texture
------------------------------------------------------------------------*/

int spr_get_sprite(uint32_t key)
{
	SPRITE *p = spr_head[key & SPR_HASH_MASK];

	while (p)
	{
		if (p->key == key)
		{
			if (p->used != frames_displayed)
			{
				if (update_cache) update_cache(key << 7);
				p->used = frames_displayed;
			}
			return p->index;
		}
		p = p->next;
	}
	return -1;
}


/*------------------------------------------------------------------------
	Register sprite in SPR texture
------------------------------------------------------------------------*/

int spr_insert_sprite(uint32_t key)
{
	uint16_t hash = key & SPR_HASH_MASK;
	SPRITE *p = spr_head[hash];
	SPRITE *q = spr_free_head;

	if (!q) return -1;

	spr_free_head = spr_free_head->next;

	q->next = NULL;
	q->key  = key;
	q->used = frames_displayed;

	if (!p)
	{
		spr_head[hash] = q;
	}
	else
	{
		while (p->next) p = p->next;
		p->next = q;
	}

	spr_texture_num++;

	return q->index;
}


/*------------------------------------------------------------------------
	Delete expired sprites from SPR texture
------------------------------------------------------------------------*/

void spr_delete_sprite(void)
{
	int i;
	SPRITE *p, *prev_p;

	for (i = 0; i < SPR_HASH_SIZE; i++)
	{
		prev_p = NULL;
		p = spr_head[i];

		while (p)
		{
			if (frames_displayed != p->used)
			{
				spr_texture_num--;

				if (!prev_p)
				{
					spr_head[i] = p->next;
					p->next = spr_free_head;
					spr_free_head = p;
					p = spr_head[i];
				}
				else
				{
					prev_p->next = p->next;
					p->next = spr_free_head;
					spr_free_head = p;
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
	Sprite clear functions
******************************************************************************/

/*------------------------------------------------------------------------
	Clear FIX sprites immediately
------------------------------------------------------------------------*/

void blit_clear_fix_sprite(void)
{
	int i;

	for (i = 0; i < FIX_TEXTURE_SIZE - 1; i++)
		fix_data[i].next = &fix_data[i + 1];

	fix_data[i].next = NULL;
	fix_free_head = &fix_data[0];

	memset(fix_head, 0, sizeof(SPRITE *) * FIX_HASH_SIZE);

	fix_texture_num = 0;
	clear_fix_texture = 0;
}


/*------------------------------------------------------------------------
	Clear SPR sprites immediately
------------------------------------------------------------------------*/

void blit_clear_spr_sprite(void)
{
	int i;

	for (i = 0; i < SPR_TEXTURE_SIZE - 1; i++)
		spr_data[i].next = &spr_data[i + 1];

	spr_data[i].next = NULL;
	spr_free_head = &spr_data[0];

	memset(spr_head, 0, sizeof(SPRITE *) * SPR_HASH_SIZE);

	spr_texture_num = 0;
	clear_spr_texture = 0;
}


/******************************************************************************
	SPR Software rendering
******************************************************************************/

void drawgfxline_zoom(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;
	const uint8_t *zoom_x_table = zoom_x_tables[zoom];

	tile = src[0];
	if (zoom_x_table[ 0]) { if (tile & 0x0000000f) *dst = pal[(tile >>  0) & 0x0f]; dst++; }
	if (zoom_x_table[ 1]) { if (tile & 0x00000f00) *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[ 2]) { if (tile & 0x000f0000) *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[ 3]) { if (tile & 0x0f000000) *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[ 4]) { if (tile & 0x000000f0) *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[ 5]) { if (tile & 0x0000f000) *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[ 6]) { if (tile & 0x00f00000) *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[ 7]) { if (tile & 0xf0000000) *dst = pal[(tile >> 28) & 0x0f]; dst++; }

	tile = src[1];
	if (zoom_x_table[ 8]) { if (tile & 0x0000000f) *dst = pal[(tile >>  0) & 0x0f]; dst++; }
	if (zoom_x_table[ 9]) { if (tile & 0x00000f00) *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[10]) { if (tile & 0x000f0000) *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[11]) { if (tile & 0x0f000000) *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[12]) { if (tile & 0x000000f0) *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[13]) { if (tile & 0x0000f000) *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[14]) { if (tile & 0x00f00000) *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[15]) { if (tile & 0xf0000000) *dst = pal[(tile >> 28) & 0x0f]; }
}

void drawgfxline_zoom_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;
	const uint8_t *zoom_x_table = zoom_x_tables[zoom];

	tile = src[0];
	if (zoom_x_table[ 0]) { *dst = pal[(tile >>  0) & 0x0f]; dst++; }
	if (zoom_x_table[ 1]) { *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[ 2]) { *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[ 3]) { *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[ 4]) { *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[ 5]) { *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[ 6]) { *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[ 7]) { *dst = pal[(tile >> 28) & 0x0f]; dst++; }

	tile = src[1];
	if (zoom_x_table[ 8]) { *dst = pal[(tile >>  0) & 0x0f]; dst++; }
	if (zoom_x_table[ 9]) { *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[10]) { *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[11]) { *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[12]) { *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[13]) { *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[14]) { *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[15]) { *dst = pal[(tile >> 28) & 0x0f]; }
}

void drawgfxline_zoom_flip(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;
	const uint8_t *zoom_x_table = zoom_x_tables[zoom];

	tile = src[1];
	if (zoom_x_table[ 0]) { if (tile & 0xf0000000) *dst = pal[(tile >> 28) & 0x0f]; dst++; }
	if (zoom_x_table[ 1]) { if (tile & 0x00f00000) *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[ 2]) { if (tile & 0x0000f000) *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[ 3]) { if (tile & 0x000000f0) *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[ 4]) { if (tile & 0x0f000000) *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[ 5]) { if (tile & 0x000f0000) *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[ 6]) { if (tile & 0x00000f00) *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[ 7]) { if (tile & 0x0000000f) *dst = pal[(tile >>  0) & 0x0f]; dst++; }

	tile = src[0];
	if (zoom_x_table[ 8]) { if (tile & 0xf0000000) *dst = pal[(tile >> 28) & 0x0f]; dst++; }
	if (zoom_x_table[ 9]) { if (tile & 0x00f00000) *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[10]) { if (tile & 0x0000f000) *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[11]) { if (tile & 0x000000f0) *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[12]) { if (tile & 0x0f000000) *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[13]) { if (tile & 0x000f0000) *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[14]) { if (tile & 0x00000f00) *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[15]) { if (tile & 0x0000000f) *dst = pal[(tile >>  0) & 0x0f]; }
}

void drawgfxline_zoom_flip_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;
	const uint8_t *zoom_x_table = zoom_x_tables[zoom];

	tile = src[1];
	if (zoom_x_table[ 0]) { *dst = pal[(tile >> 28) & 0x0f]; dst++; }
	if (zoom_x_table[ 1]) { *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[ 2]) { *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[ 3]) { *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[ 4]) { *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[ 5]) { *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[ 6]) { *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[ 7]) { *dst = pal[(tile >>  0) & 0x0f]; dst++; }

	tile = src[0];
	if (zoom_x_table[ 8]) { *dst = pal[(tile >> 28) & 0x0f]; dst++; }
	if (zoom_x_table[ 9]) { *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[10]) { *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[11]) { *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[12]) { *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[13]) { *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[14]) { *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[15]) { *dst = pal[(tile >>  0) & 0x0f]; }
}

void drawgfxline_fixed(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;

	tile = src[0];
	if (tile)
	{
		if (tile & 0x000f) dst[ 0] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[ 4] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[ 1] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[ 5] = pal[(tile >> 12) & 0x0f];
		tile >>= 16;
		if (tile & 0x000f) dst[ 2] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[ 6] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[ 3] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[ 7] = pal[(tile >> 12) & 0x0f];
	}
	tile = src[1];
	if (tile)
	{
		if (tile & 0x000f) dst[ 8] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[12] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[ 9] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[13] = pal[(tile >> 12) & 0x0f];
		tile >>= 16;
		if (tile & 0x000f) dst[10] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[14] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[11] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[15] = pal[(tile >> 12) & 0x0f];
	}
}

void drawgfxline_fixed_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;

	tile = src[0];
	dst[ 0] = pal[(tile >>  0) & 0x0f];
	dst[ 4] = pal[(tile >>  4) & 0x0f];
	dst[ 1] = pal[(tile >>  8) & 0x0f];
	dst[ 5] = pal[(tile >> 12) & 0x0f];
	dst[ 2] = pal[(tile >> 16) & 0x0f];
	dst[ 6] = pal[(tile >> 20) & 0x0f];
	dst[ 3] = pal[(tile >> 24) & 0x0f];
	dst[ 7] = pal[(tile >> 28) & 0x0f];

	tile = src[1];
	dst[ 8] = pal[(tile >>  0) & 0x0f];
	dst[12] = pal[(tile >>  4) & 0x0f];
	dst[ 9] = pal[(tile >>  8) & 0x0f];
	dst[13] = pal[(tile >> 12) & 0x0f];
	dst[10] = pal[(tile >> 16) & 0x0f];
	dst[14] = pal[(tile >> 20) & 0x0f];
	dst[11] = pal[(tile >> 24) & 0x0f];
	dst[15] = pal[(tile >> 28) & 0x0f];
}

void drawgfxline_fixed_flip(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;

	tile = src[0];
	if (tile)
	{
		if (tile & 0x000f) dst[15] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[11] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[14] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[10] = pal[(tile >> 12) & 0x0f];
		tile >>= 16;
		if (tile & 0x000f) dst[13] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[ 9] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[12] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[ 8] = pal[(tile >> 12) & 0x0f];
	}
	tile = src[1];
	if (tile)
	{
		if (tile & 0x000f) dst[ 7] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[ 3] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[ 6] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[ 2] = pal[(tile >> 12) & 0x0f];
		tile >>= 16;
		if (tile & 0x000f) dst[ 5] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[ 1] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[ 4] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[ 0] = pal[(tile >> 12) & 0x0f];
	}
}

void drawgfxline_fixed_flip_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;

	tile = src[1];
	dst[ 0] = pal[(tile >> 28) & 0x0f];
	dst[ 1] = pal[(tile >> 20) & 0x0f];
	dst[ 2] = pal[(tile >> 12) & 0x0f];
	dst[ 3] = pal[(tile >>  4) & 0x0f];
	dst[ 4] = pal[(tile >> 24) & 0x0f];
	dst[ 5] = pal[(tile >> 16) & 0x0f];
	dst[ 6] = pal[(tile >>  8) & 0x0f];
	dst[ 7] = pal[(tile >>  0) & 0x0f];

	tile = src[0];
	dst[ 8] = pal[(tile >> 28) & 0x0f];
	dst[ 9] = pal[(tile >> 20) & 0x0f];
	dst[10] = pal[(tile >> 12) & 0x0f];
	dst[11] = pal[(tile >>  4) & 0x0f];
	dst[12] = pal[(tile >> 24) & 0x0f];
	dst[13] = pal[(tile >> 16) & 0x0f];
	dst[14] = pal[(tile >>  8) & 0x0f];
	dst[15] = pal[(tile >>  0) & 0x0f];
}

