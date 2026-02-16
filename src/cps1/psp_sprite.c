/******************************************************************************

	psp_sprite.c

	CPS1 Sprite Manager - PSP Platform

	This file contains PSP-specific sprite rendering using the GU (Graphics
	Utility) library. Platform-agnostic code is in sprite_common.c.

	Key PSP-specific features:
	- Swizzled texture formats for optimal GU performance
	- Video driver API calls for hardware-accelerated rendering
	- CLUT-based palettized textures (8-bit indexed)
	- Vertex arrays for batched sprite drawing

******************************************************************************/

#include "cps1.h"
#include "sprite_common.h"


/******************************************************************************
	Constants/Macros
******************************************************************************/

#define PSP_UNCACHE_PTR(p)	(((uint32_t)(p)) | 0x40000000)


/******************************************************************************
	Prototypes
******************************************************************************/

/******************************************************************************
	Local Structures/Variables
******************************************************************************/


static RECT cps_src_clip = { 64, 16, 64 + 384, 16 + 224 };

static RECT cps_clip[6] =
{
	{ 48, 24, 48 + 384, 24 + 224 },	// option_stretch = 0  (384x224)
	{ 60,  1, 60 + 360,  1 + 270 },	// option_stretch = 1  (360x270  4:3)
	{ 48,  1, 48 + 384,  1 + 270 },	// option_stretch = 2  (384x270 24:17)
	{ 7,   0,   7+ 466,      272 },	// option_stretch = 3  (466x272 12:7)
	{ 0,   1, 480,       1 + 270 },	// option_stretch = 4  (480x270 16:9)
	{ 138, 0, 138 + 204,     272 }  	// option_stretch = 5  (204x272 3:4 vertical)
};


/*------------------------------------------------------------------------
	Vertex Data
------------------------------------------------------------------------*/

/* CLUT */
static uint16_t *clut;

/* OBJECT vertex arrays (16x16 tiles) - single array to preserve draw order */
static struct Vertex __attribute__((aligned(64))) vertices_object[OBJECT_MAX_SPRITES * 2];
static uint16_t object_num;

/* OBJECT CLUT batch tracking - preserves draw order when CLUT changes */
typedef struct {
	uint16_t start;      /* Start index in vertex array */
	uint16_t count;      /* Number of vertices in this batch */
	uint16_t *clut;      /* CLUT pointer for this batch */
} object_batch_t;

#define OBJECT_MAX_BATCHES 256
static object_batch_t object_batches[OBJECT_MAX_BATCHES];
static uint16_t object_batch_count;
static uint16_t *object_current_clut;

/* Separate vertex arrays for each scroll layer to avoid GPU race conditions.
   When GPU commands are queued asynchronously, shared buffers can be overwritten
   before the GPU finishes processing them. Each layer needs its own buffers. */

/* SCROLL1 vertex arrays (8x8 tiles) */
static struct Vertex __attribute__((aligned(64))) vertices_scroll1_clut0[SCROLL1_MAX_SPRITES * 2];
static struct Vertex __attribute__((aligned(64))) vertices_scroll1_clut1[SCROLL1_MAX_SPRITES * 2];
static uint16_t scroll1_clut0_num;
static uint16_t scroll1_clut1_num;

/* SCROLL2 vertex arrays (16x16 tiles) */
static struct Vertex __attribute__((aligned(64))) vertices_scroll2_clut0[SCROLL2_MAX_SPRITES * 2];
static struct Vertex __attribute__((aligned(64))) vertices_scroll2_clut1[SCROLL2_MAX_SPRITES * 2];
static uint16_t scroll2_clut0_num;
static uint16_t scroll2_clut1_num;

/* SCROLL3 vertex arrays (32x32 tiles) */
static struct Vertex __attribute__((aligned(64))) vertices_scroll3_clut0[SCROLL3_MAX_SPRITES * 2];
static struct Vertex __attribute__((aligned(64))) vertices_scroll3_clut1[SCROLL3_MAX_SPRITES * 2];
static uint16_t scroll3_clut0_num;
static uint16_t scroll3_clut1_num;

/* SCROLLH vertex array (high priority layer) */
static struct Vertex __attribute__((aligned(64))) vertices_scrollh[SCROLLH_MAX_SPRITES * 2];

/* STARS vertex array (0x1000 = 4096 potential stars) */
static struct PointVertex __attribute__((aligned(64))) vertices_stars[0x1000];


/*------------------------------------------------------------------------
	'swizzle' Texture Address Calculation Table (8-bit color)
------------------------------------------------------------------------*/

static const int ALIGN16_DATA swizzle_table_8bit[32] =
{
	   0, 16, 16, 16, 16, 16, 16, 16,
	3984, 16, 16, 16, 16, 16, 16, 16,
	3984, 16, 16, 16, 16, 16, 16, 16,
	3984, 16, 16, 16, 16, 16, 16, 16
};


/******************************************************************************
	Sprite Drawing Interface Functions
******************************************************************************/

/*------------------------------------------------------------------------
	Reset sprite processing
------------------------------------------------------------------------*/

void blit_reset(int bank_scroll1, int bank_scroll2, int bank_scroll3, uint8_t *pen_usage16)
{
	int i;

	tex_scrollh = (uint16_t *)video_driver->textureLayer(video_data, TEXTURE_LAYER_SCROLLH);
	tex_object  = (uint8_t *)video_driver->textureLayer(video_data, TEXTURE_LAYER_OBJECT);
	tex_scroll1 = (uint8_t *)video_driver->textureLayer(video_data, TEXTURE_LAYER_SCROLL1);
	tex_scroll2 = (uint8_t *)video_driver->textureLayer(video_data, TEXTURE_LAYER_SCROLL2);
	tex_scroll3 = (uint8_t *)video_driver->textureLayer(video_data, TEXTURE_LAYER_SCROLL3);

	for (i = 0; i < OBJECT_TEXTURE_SIZE; i++) object_data[i].index = i;
	for (i = 0; i < SCROLL1_TEXTURE_SIZE; i++) scroll1_data[i].index = i;
	for (i = 0; i < SCROLL2_TEXTURE_SIZE; i++) scroll2_data[i].index = i;
	for (i = 0; i < SCROLL3_TEXTURE_SIZE; i++) scroll3_data[i].index = i;
	for (i = 0; i < SCROLLH_TEXTURE_SIZE; i++) scrollh_data[i].index = i;

	gfx_object  = memory_region_gfx1;
	gfx_scroll1 = &memory_region_gfx1[bank_scroll1 << 21];
	gfx_scroll2 = &memory_region_gfx1[bank_scroll2 << 21];
	gfx_scroll3 = &memory_region_gfx1[bank_scroll3 << 21];

	pen_usage = pen_usage16;
	clut = (uint16_t *)&video_palette;

	blit_clear_all_sprite();
}


/*------------------------------------------------------------------------
	Begin sprite drawing
------------------------------------------------------------------------*/

void blit_start(int high_layer)
{
	if (scrollh_texture_clear || high_layer != scrollh_layer_number)
	{
		scrollh_reset_sprite();
		scrollh_layer_number = high_layer;
	}

	scrollh_delete_dirty_palette();
	memset(palette_dirty_marks, 0, sizeof(palette_dirty_marks));

	/* Initialize per-layer vertex counters */
	object_num = 0;
	object_batch_count = 0;
	object_current_clut = NULL;
	scroll1_clut0_num = 0;
	scroll1_clut1_num = 0;
	scroll2_clut0_num = 0;
	scroll2_clut1_num = 0;
	scroll3_clut0_num = 0;
	scroll3_clut1_num = 0;

	scrollh_num = 0;

	/* Start a new work frame via the video driver */
	video_driver->beginFrame(video_data);
	video_driver->startWorkFrame(video_data, 0);
	video_driver->scissor(video_data, 64, 16, 448, 240);

}


/*------------------------------------------------------------------------
	End sprite drawing
------------------------------------------------------------------------*/

void blit_finish(void)
{
	if (cps_rotate_screen)
	{
		if (cps_flip_screen)
		{
			video_driver->copyRectFlip(video_data, COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, &cps_src_clip, &cps_src_clip);
			video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP, &cps_src_clip, &cps_src_clip);
			video_driver->clearFrame(video_data, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER);
		}
		video_driver->copyRectRotate(video_data, COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, &cps_src_clip, &cps_clip[5]);
	}
	else
	{
		if (cps_flip_screen)
			video_driver->copyRectFlip(video_data, COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, &cps_src_clip, &cps_clip[option_stretch]);
		else
			video_driver->transferWorkFrame(video_data, &cps_src_clip, &cps_clip[option_stretch]);
	}
	video_driver->endFrame(video_data);
}


/*------------------------------------------------------------------------
	Register OBJECT to draw list
------------------------------------------------------------------------*/

void blit_draw_object(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	if ((x > 47 && x < 448) && (y > 0 && y < 239))
	{
		int16_t idx;
		struct Vertex *vertices;
		uint32_t key = MAKE_KEY(code, attr);

		if ((idx = object_get_sprite(key)) < 0)
		{
			uint32_t col, tile;
			uint8_t *src, *dst, lines = 16;

			if (object_texture_num == OBJECT_TEXTURE_SIZE - 1)
			{
				cps1_scan_object();
				object_delete_sprite();
			}

			idx = object_insert_sprite(key);
			dst = SWIZZLED8_16x16(tex_object, idx);
			src = &gfx_object[code << 7];
			col = color_table[attr & 0x0f];

			while (lines--)
			{
				tile = *(uint32_t *)(src + 0);
				*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;
				*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;
				tile = *(uint32_t *)(src + 4);
				*(uint32_t *)(dst +  8) = ((tile >> 0) & 0x0f0f0f0f) | col;
				*(uint32_t *)(dst + 12) = ((tile >> 4) & 0x0f0f0f0f) | col;
				src += 8;
				dst += swizzle_table_8bit[lines];
			}
		}

		/* Determine CLUT for this sprite */
		uint16_t *sprite_clut = (attr & 0x10) ? &clut[16 << 4] : clut;

		/* Check if CLUT changed - need to start a new batch */
		if (sprite_clut != object_current_clut)
		{
			/* Finalize current batch if any */
			if (object_current_clut != NULL && object_batch_count > 0)
			{
				object_batches[object_batch_count - 1].count = object_num - object_batches[object_batch_count - 1].start;
			}

			/* Start new batch */
			if (object_batch_count < OBJECT_MAX_BATCHES)
			{
				object_batches[object_batch_count].start = object_num;
				object_batches[object_batch_count].count = 0;
				object_batches[object_batch_count].clut = sprite_clut;
				object_batch_count++;
			}
			object_current_clut = sprite_clut;
		}

		vertices = &vertices_object[object_num];
		object_num += 2;

		vertices[0].x = vertices[1].x = x;
		vertices[0].y = vertices[1].y = y;
		vertices[0].u = vertices[1].u = (idx & 0x001f) << 4;
		vertices[0].v = vertices[1].v = (idx & 0x03e0) >> 1;

		attr ^= 0x60;
		vertices[(attr & 0x20) >> 5].u += 16;
		vertices[(attr & 0x40) >> 6].v += 16;

		vertices[1].x += 16;
		vertices[1].y += 16;
	}
}


/*------------------------------------------------------------------------
	End OBJECT drawing
------------------------------------------------------------------------*/

void blit_finish_object(void)
{
	uint16_t i;

	if (object_num == 0) return;

	/* Finalize the last batch */
	if (object_batch_count > 0)
	{
		object_batches[object_batch_count - 1].count = object_num - object_batches[object_batch_count - 1].start;
	}

	video_driver->uploadMem(video_data, TEXTURE_LAYER_OBJECT);
	video_driver->flushCache(video_data, vertices_object, object_num * sizeof(struct Vertex));

	/* Render batches in order - preserves original draw order */
	for (i = 0; i < object_batch_count; i++)
	{
		if (object_batches[i].count > 0)
		{
			video_driver->uploadClut(video_data, object_batches[i].clut, 0);
			video_driver->blitTexture(video_data, TEXTURE_LAYER_OBJECT,
				object_batches[i].clut, 0,
				object_batches[i].count,
				&vertices_object[object_batches[i].start]);
		}
	}
}


/*------------------------------------------------------------------------
	Register SCROLL1 to draw list
------------------------------------------------------------------------*/

void blit_draw_scroll1(int16_t x, int16_t y, uint32_t code, uint16_t attr, uint16_t gfxset)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_KEY(code, attr);

	if ((idx = scroll1_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
		uint8_t *src, *dst, lines = 8;

		if (scroll1_texture_num == SCROLL1_TEXTURE_SIZE - 1)
		{
			cps1_scan_scroll1();
			scroll1_delete_sprite();
		}

		idx = scroll1_insert_sprite(key);
		dst = SWIZZLED8_8x8(tex_scroll1, idx);
		src = &gfx_scroll1[(code << 6) + (gfxset << 2)];
		col = color_table[attr & 0x0f];

		while (lines--)
		{
			tile = *(uint32_t *)(src + 0);
			*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;
			src += 8;
			dst += 16;
		}
	}

	if (attr & 0x10)
	{
		vertices = &vertices_scroll1_clut1[scroll1_clut1_num];
		scroll1_clut1_num += 2;
	}
	else
	{
		vertices = &vertices_scroll1_clut0[scroll1_clut0_num];
		scroll1_clut0_num += 2;
	}

	vertices[0].x = vertices[1].x = x;
	vertices[0].y = vertices[1].y = y;
	vertices[0].u = vertices[1].u = (idx & 0x003f) << 3;
	vertices[0].v = vertices[1].v = (idx & 0x0fc0) >> 3;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 8;
	vertices[(attr & 0x40) >> 6].v += 8;

	vertices[1].x += 8;
	vertices[1].y += 8;
}


/*------------------------------------------------------------------------
	End SCROLL1 drawing
------------------------------------------------------------------------*/

void blit_finish_scroll1(void)
{
	
	uint16_t *current_clut;
	if (scroll1_clut0_num + scroll1_clut1_num == 0) return;
	video_driver->uploadMem(video_data, TEXTURE_LAYER_SCROLL1);

	if (scroll1_clut0_num)
	{
		current_clut = &clut[32 << 4];
		video_driver->uploadClut(video_data, current_clut, 0);
		video_driver->flushCache(video_data, vertices_scroll1_clut0, scroll1_clut0_num * sizeof(struct Vertex));
		video_driver->blitTexture(video_data, TEXTURE_LAYER_SCROLL1, current_clut, 0, scroll1_clut0_num, vertices_scroll1_clut0);
	}
	if (scroll1_clut1_num)
	{
		current_clut = &clut[48 << 4];
		video_driver->uploadClut(video_data, current_clut, 0);
		video_driver->flushCache(video_data, vertices_scroll1_clut1, scroll1_clut1_num * sizeof(struct Vertex));
		video_driver->blitTexture(video_data, TEXTURE_LAYER_SCROLL1, current_clut, 0, scroll1_clut1_num, vertices_scroll1_clut1);
	}
}


/*------------------------------------------------------------------------
	Set SCROLL2 clip range
------------------------------------------------------------------------*/

void blit_set_clip_scroll2(int16_t min_y, int16_t max_y)
{
	scroll2_min_y = min_y;
	scroll2_max_y = max_y + 1;
}


/*------------------------------------------------------------------------
	Register SCROLL2 to draw list
------------------------------------------------------------------------*/

void blit_draw_scroll2(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_KEY(code, attr);

	if ((idx = scroll2_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
		uint8_t *src, *dst, lines = 16;

		if (scroll2_texture_num == SCROLL2_TEXTURE_SIZE - 1)
		{
			cps1_scan_scroll2();
			scroll2_delete_sprite();
		}

		idx = scroll2_insert_sprite(key);
		dst = SWIZZLED8_16x16(tex_scroll2, idx);
		src = &gfx_scroll2[code << 7];
		col = color_table[attr & 0x0f];

		while (lines--)
		{
			tile = *(uint32_t *)(src + 0);
			*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;
			tile = *(uint32_t *)(src + 4);
			*(uint32_t *)(dst +  8) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst + 12) = ((tile >> 4) & 0x0f0f0f0f) | col;
			src += 8;
			dst += swizzle_table_8bit[lines];
		}
	}

	if (attr & 0x10)
	{
		vertices = &vertices_scroll2_clut1[scroll2_clut1_num];
		scroll2_clut1_num += 2;
	}
	else
	{
		vertices = &vertices_scroll2_clut0[scroll2_clut0_num];
		scroll2_clut0_num += 2;
	}

	vertices[0].x = vertices[1].x = x;
	vertices[0].y = vertices[1].y = y;
	vertices[0].u = vertices[1].u = (idx & 0x001f) << 4;
	vertices[0].v = vertices[1].v = (idx & 0x03e0) >> 1;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 16;
	vertices[(attr & 0x40) >> 6].v += 16;

	vertices[1].x += 16;
	vertices[1].y += 16;
}


/*------------------------------------------------------------------------
	End SCROLL2 drawing
------------------------------------------------------------------------*/

void blit_finish_scroll2(void)
{
	uint16_t *current_clut;

	if (scroll2_clut0_num + scroll2_clut1_num == 0) return;

	video_driver->scissor(video_data, 64, scroll2_min_y, 448, scroll2_max_y);
	video_driver->uploadMem(video_data, TEXTURE_LAYER_SCROLL2);

	if (scroll2_clut0_num)
	{
		current_clut = &clut[64 << 4];
		video_driver->uploadClut(video_data, current_clut, 0);
		video_driver->flushCache(video_data, vertices_scroll2_clut0, scroll2_clut0_num * sizeof(struct Vertex));
		video_driver->blitTexture(video_data, TEXTURE_LAYER_SCROLL2, current_clut, 0, scroll2_clut0_num, vertices_scroll2_clut0);
	}
	if (scroll2_clut1_num)
	{
		current_clut = &clut[80 << 4];
		video_driver->uploadClut(video_data, current_clut, 0);
		video_driver->flushCache(video_data, vertices_scroll2_clut1, scroll2_clut1_num * sizeof(struct Vertex));
		video_driver->blitTexture(video_data, TEXTURE_LAYER_SCROLL2, current_clut, 0, scroll2_clut1_num, vertices_scroll2_clut1);
	}

	// Put back to full screen scissor
	video_driver->scissor(video_data, 64, 16, 448, 240);
}


/*------------------------------------------------------------------------
	Register SCROLL3 to draw list
------------------------------------------------------------------------*/

void blit_draw_scroll3(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_KEY(code, attr);

	if ((idx = scroll3_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
		uint8_t *src, *dst, lines = 32;

		if (scroll3_texture_num == SCROLL3_TEXTURE_SIZE - 1)
		{
			cps1_scan_scroll3();
			scroll3_delete_sprite();
		}

		idx = scroll3_insert_sprite(key);
		dst = SWIZZLED8_32x32(tex_scroll3, idx);
		src = &gfx_scroll3[code << 9];
		col = color_table[attr & 0x0f];

		while (lines--)
		{
			tile = *(uint32_t *)(src + 0);
			*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;
			tile = *(uint32_t *)(src + 4);
			*(uint32_t *)(dst +  8) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst + 12) = ((tile >> 4) & 0x0f0f0f0f) | col;
			tile = *(uint32_t *)(src + 8);
			*(uint32_t *)(dst + 128) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst + 132) = ((tile >> 4) & 0x0f0f0f0f) | col;
			tile = *(uint32_t *)(src + 12);
			*(uint32_t *)(dst + 136) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst + 140) = ((tile >> 4) & 0x0f0f0f0f) | col;
			src += 16;
			dst += swizzle_table_8bit[lines];
		}
	}

	if (attr & 0x10)
	{
		vertices = &vertices_scroll3_clut1[scroll3_clut1_num];
		scroll3_clut1_num += 2;
	}
	else
	{
		vertices = &vertices_scroll3_clut0[scroll3_clut0_num];
		scroll3_clut0_num += 2;
	}

	vertices[0].x = vertices[1].x = x;
	vertices[0].y = vertices[1].y = y;
	vertices[0].u = vertices[1].u = (idx & 0x000f) << 5;
	vertices[0].v = vertices[1].v = (idx & 0x00f0) << 1;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 32;
	vertices[(attr & 0x40) >> 6].v += 32;

	vertices[1].x += 32;
	vertices[1].y += 32;
}


/*------------------------------------------------------------------------
	End SCROLL3 drawing
------------------------------------------------------------------------*/

void blit_finish_scroll3(void)
{
	uint16_t *current_clut;

	if (scroll3_clut0_num + scroll3_clut1_num == 0) return;

	video_driver->uploadMem(video_data, TEXTURE_LAYER_SCROLL3);

	if (scroll3_clut0_num)
	{
		current_clut = &clut[96 << 4];
		video_driver->flushCache(video_data, vertices_scroll3_clut0, scroll3_clut0_num * sizeof(struct Vertex));
		video_driver->uploadClut(video_data, current_clut, 0);
		video_driver->blitTexture(video_data, TEXTURE_LAYER_SCROLL3, current_clut, 0, scroll3_clut0_num, vertices_scroll3_clut0);
	}
	if (scroll3_clut1_num)
	{
		current_clut = &clut[112 << 4];
		video_driver->flushCache(video_data, vertices_scroll3_clut1, scroll3_clut1_num * sizeof(struct Vertex));
		video_driver->uploadClut(video_data, current_clut, 0);
		video_driver->blitTexture(video_data, TEXTURE_LAYER_SCROLL3, current_clut, 0, scroll3_clut1_num, vertices_scroll3_clut1);
	}
}


/*------------------------------------------------------------------------
	Register SCROLL1 (high layer) to draw list
------------------------------------------------------------------------*/

void blit_draw_scroll1h(int16_t x, int16_t y, uint32_t code, uint16_t attr, uint16_t tpens, uint16_t gfxset)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_HIGH_KEY(code, attr);

	if ((idx = scrollh_get_sprite(key)) < 0)
	{
		uint32_t *src, tile, lines = 8;
		uint16_t *dst, *pal, pal2[16];

		if (scrollh_texture_num == SCROLL1H_TEXTURE_SIZE - 1)
		{
			cps1_scan_scroll1_foreground();
			scrollh_delete_sprite();
		}

		idx = scrollh_insert_sprite(key);
		dst = NONE_SWIZZLED_8x8(tex_scrollh, idx);
		src = (uint32_t *)&gfx_scroll1[(code << 6) + (gfxset << 2)];
		pal = &video_palette[((attr & 0x1f) + 32) << 4];

		if (tpens != 0x7fff)
		{
			int i;

			for (i = 0; i < 15; i++)
				pal2[i] = (tpens & (1 << i)) ? pal[i] : 0x8000;
			pal2[15] = 0x8000;
			pal = pal2;
		}

		while (lines--)
		{
			tile = src[0];
			dst[0] = pal[tile & 0x0f]; tile >>= 4;
			dst[4] = pal[tile & 0x0f]; tile >>= 4;
			dst[1] = pal[tile & 0x0f]; tile >>= 4;
			dst[5] = pal[tile & 0x0f]; tile >>= 4;
			dst[2] = pal[tile & 0x0f]; tile >>= 4;
			dst[6] = pal[tile & 0x0f]; tile >>= 4;
			dst[3] = pal[tile & 0x0f]; tile >>= 4;
			dst[7] = pal[tile & 0x0f];
			src += 2;
			dst += BUF_WIDTH;
		}
	}

	vertices = &vertices_scrollh[scrollh_num];

	vertices[0].u = vertices[1].u = (idx & 0x003f) << 3;
	vertices[0].v = vertices[1].v = (idx & 0x0fc0) >> 3;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 8;
	vertices[(attr & 0x40) >> 6].v += 8;

	vertices[0].x = x;
	vertices[0].y = y;

	vertices[1].x = x + 8;
	vertices[1].y = y + 8;

	scrollh_num += 2;
}


/*------------------------------------------------------------------------
	Register SCROLL2 (high layer) to draw list
------------------------------------------------------------------------*/

void blit_draw_scroll2h(int16_t x, int16_t y, uint32_t code, uint16_t attr, uint16_t tpens)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_HIGH_KEY(code, attr);

	if ((idx = scrollh_get_sprite(key)) < 0)
	{
		uint32_t *src, tile, lines = 16;
		uint16_t *dst, *pal, pal2[16];

		if (scrollh_texture_num == SCROLL2H_TEXTURE_SIZE - 1)
		{
			cps1_scan_scroll2_foreground();
			scrollh_delete_sprite();
		}

		idx = scrollh_insert_sprite(key);
		dst = NONE_SWIZZLED_16x16(tex_scrollh, idx);
		src = (uint32_t *)&gfx_scroll2[code << 7];
		pal = &video_palette[((attr & 0x1f) + 64) << 4];

		if (tpens != 0x7fff)
		{
			int i;

			for (i = 0; i < 15; i++)
				pal2[i] = (tpens & (1 << i)) ? pal[i] : 0x8000;
			pal2[15] = 0x8000;
			pal = pal2;
		}

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

	vertices = &vertices_scrollh[scrollh_num];

	vertices[0].u = vertices[1].u = (idx & 0x001f) << 4;
	vertices[0].v = vertices[1].v = (idx & 0x03e0) >> 1;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 16;
	vertices[(attr & 0x40) >> 6].v += 16;

	vertices[0].x = x;
	vertices[0].y = y;

	vertices[1].x = x + 16;
	vertices[1].y = y + 16;

	scrollh_num += 2;
}


/*------------------------------------------------------------------------
	End SCROLL2 (high layer) drawing
------------------------------------------------------------------------*/

void blit_finish_scroll2h(void)
{
	struct Vertex *vertices;

	
	if (!scrollh_num) return;
	/* Use video driver to render the high (scrollh) layer */
	video_driver->uploadMem(video_data, TEXTURE_LAYER_SCROLLH);
	video_driver->flushCache(video_data, vertices_scrollh, scrollh_num * sizeof(struct Vertex));
	video_driver->scissor(video_data, 64, scroll2_min_y, 448, scroll2_max_y);
	video_driver->blitTexture(video_data, TEXTURE_LAYER_SCROLLH, NULL, 0, scrollh_num, vertices_scrollh);
	// Put back to full screen scissor
	video_driver->scissor(video_data, 64, 16, 448, 240);
	scrollh_num = 0;
}


/*------------------------------------------------------------------------
	Register SCROLL3 (high layer) to draw list
------------------------------------------------------------------------*/

void blit_draw_scroll3h(int16_t x, int16_t y, uint32_t code, uint16_t attr, uint16_t tpens)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_HIGH_KEY(code, attr);

	if ((idx = scrollh_get_sprite(key)) < 0)
	{
		uint32_t *src, tile, lines = 32;
		uint16_t *dst, *pal, pal2[16];

		if (scrollh_texture_num == SCROLL3H_TEXTURE_SIZE - 1)
		{
			cps1_scan_scroll3_foreground();
			scrollh_delete_sprite();
		}

		idx = scrollh_insert_sprite(key);
		dst = NONE_SWIZZLED_32x32(tex_scrollh, idx);
		src = (uint32_t *)&gfx_scroll3[code << 9];
		pal = &video_palette[((attr & 0x1f) + 96) << 4];

		if (tpens != 0x7fff)
		{
			int i;

			for (i = 0; i < 15; i++)
				pal2[i] = (tpens & (1 << i)) ? pal[i] : 0x8000;
			pal2[15] = 0x8000;
			pal = pal2;
		}

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
			tile = src[2];
			dst[16] = pal[tile & 0x0f]; tile >>= 4;
			dst[20] = pal[tile & 0x0f]; tile >>= 4;
			dst[17] = pal[tile & 0x0f]; tile >>= 4;
			dst[21] = pal[tile & 0x0f]; tile >>= 4;
			dst[18] = pal[tile & 0x0f]; tile >>= 4;
			dst[22] = pal[tile & 0x0f]; tile >>= 4;
			dst[19] = pal[tile & 0x0f]; tile >>= 4;
			dst[23] = pal[tile & 0x0f];
			tile = src[3];
			dst[24] = pal[tile & 0x0f]; tile >>= 4;
			dst[28] = pal[tile & 0x0f]; tile >>= 4;
			dst[25] = pal[tile & 0x0f]; tile >>= 4;
			dst[29] = pal[tile & 0x0f]; tile >>= 4;
			dst[26] = pal[tile & 0x0f]; tile >>= 4;
			dst[30] = pal[tile & 0x0f]; tile >>= 4;
			dst[27] = pal[tile & 0x0f]; tile >>= 4;
			dst[31] = pal[tile & 0x0f];
			src += 4;
			dst += BUF_WIDTH;
		}
	}

	vertices = &vertices_scrollh[scrollh_num];

	vertices[0].u = vertices[1].u = (idx & 0x000f) << 5;
	vertices[0].v = vertices[1].v = (idx & 0x00f0) << 1;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 32;
	vertices[(attr & 0x40) >> 6].v += 32;

	vertices[0].x = x;
	vertices[1].x = x + 32;

	vertices[0].y = y;
	vertices[1].y = y + 32;

	scrollh_num += 2;
}


/*------------------------------------------------------------------------
	End SCROLL1,3 (high layer) drawing
------------------------------------------------------------------------*/

void blit_finish_scrollh(void)
{
	struct Vertex *vertices;

	if (!scrollh_num) return;
	/* Use video driver to render the high (scrollh) layer */
	video_driver->uploadMem(video_data, TEXTURE_LAYER_SCROLLH);
	video_driver->flushCache(video_data, vertices_scrollh, scrollh_num * sizeof(struct Vertex));
	video_driver->blitTexture(video_data, TEXTURE_LAYER_SCROLLH, NULL, 0, scrollh_num, vertices_scrollh);
}


/*------------------------------------------------------------------------
	Draw STARS layer
------------------------------------------------------------------------*/

void blit_draw_stars(uint16_t stars_x, uint16_t stars_y, uint8_t *col, uint16_t *pal)
{
	struct PointVertex *vertices_tmp = vertices_stars;

	uint16_t offs;
	int stars_num = 0;

	for (offs = 0; offs < 0x1000; offs++, col += 8)
	{
		if (*col != 0x0f)
		{
			vertices_tmp->x     = (((offs >> 8) << 5) - stars_x + (*col & 0x1f)) & 0x1ff;
			vertices_tmp->y     = ((offs & 0xff) - stars_y) & 0xff;
			vertices_tmp->z     = 0;
			vertices_tmp->color = pal[(*col & 0xe0) >> 1];
			vertices_tmp++;
			stars_num++;
		}
	}

	if (stars_num)
	{
		video_driver->flushCache(video_data, vertices_stars, stars_num * sizeof(struct PointVertex));
		video_driver->blitPoints(video_data, stars_num, vertices_stars);
	}
}
