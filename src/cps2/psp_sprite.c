/******************************************************************************

	psp_sprite.c

	CPS2 Sprite Manager - PSP Platform

	This file contains PSP-specific sprite rendering using the video_driver
	abstraction layer. Platform-agnostic code is in sprite_common.c.

	Key features:
	- Swizzled texture formats for optimal GU performance
	- video_driver API calls for hardware-accelerated rendering
	- CLUT-based palettized textures (8-bit indexed)
	- Vertex arrays for batched sprite drawing
	- Z-buffer support for CPS2 priority masking

******************************************************************************/

#include "cps2.h"
#include "sprite_common.h"


/******************************************************************************
	Constants/Macros
******************************************************************************/

#define PSP_UNCACHE_PTR(p)	(((uint32_t)(p)) | 0x40000000)


/******************************************************************************
	Prototypes
******************************************************************************/

void (*blit_finish_object)(int start_pri, int end_pri);
static void blit_render_object(int start_pri, int end_pri);
static void blit_render_object_zb(int start_pri, int end_pri);

void (*blit_draw_scroll2)(int16_t x, int16_t y, uint32_t code, uint16_t attr);
static void blit_draw_scroll2_software(int16_t x, int16_t y, uint32_t code, uint16_t attr);
static void blit_draw_scroll2_hardware(int16_t x, int16_t y, uint32_t code, uint16_t attr);


/******************************************************************************
	Local Structures/Variables
******************************************************************************/

typedef struct object_t OBJECT;

struct object_t
{
	uint32_t clut;
	struct Vertex vertices[2];
	OBJECT *next;
};

static RECT cps_src_clip = { 64, 16, 64 + 384, 16 + 224 };

static RECT cps_clip[6] =
{
	{ 48, 24, 48 + 384, 24 + 224 },	// option_stretch = 0  (384x224)
	{ 60,  1, 60 + 360,  1 + 270 },	// option_stretch = 1  (360x270  4:3)
	{ 48,  1, 48 + 384,  1 + 270 },	// option_stretch = 2  (384x270 24:17)
	{ 7,   0,   7+ 466,      272 },	// option_stretch = 3  (466x272 12:7)
	{ 0,   1, 480,       1 + 270 },	// option_stretch = 4  (480x270 16:9)
	{ 138, 0, 138 + 204,     272 }	    // option_stretch = 5  (204x272 3:4 vertical)
};


/*------------------------------------------------------------------------
	Vertex Data
------------------------------------------------------------------------*/

/* OBJECT priority-based linked lists */
static OBJECT *vertices_object_head[8];
static OBJECT *vertices_object_tail[8];
static OBJECT ALIGN16_DATA vertices_object[OBJECT_MAX_SPRITES];

static uint16_t object_num[8];
static uint16_t object_index;

/* Flattened object vertex buffer for rendering (replaces sceGuGetMemory) */
static struct Vertex ALIGN16_DATA vertices_object_flat[OBJECT_MAX_SPRITES * 2];

/* Scroll vertex arrays (shared between scroll layers - reused sequentially) */
static struct Vertex ALIGN16_DATA vertices_scroll[2][SCROLL1_MAX_SPRITES * 2];


/*------------------------------------------------------------------------
	CLUT
------------------------------------------------------------------------*/

static uint16_t *clut;
static uint16_t clut0_num;
static uint16_t clut1_num;


/******************************************************************************
	Sprite Drawing Interface Functions
******************************************************************************/

/*------------------------------------------------------------------------
	Reset sprite processing
------------------------------------------------------------------------*/

void blit_reset(void)
{
	int i;

	scrbitmap   = (uint16_t *)video_driver->workFrame(video_data);
	tex_object  = (uint8_t *)video_driver->textureLayer(video_data, TEXTURE_LAYER_OBJECT);
	tex_scroll1 = (uint8_t *)video_driver->textureLayer(video_data, TEXTURE_LAYER_SCROLL1);
	tex_scroll2 = (uint8_t *)video_driver->textureLayer(video_data, TEXTURE_LAYER_SCROLL2);
	tex_scroll3 = (uint8_t *)video_driver->textureLayer(video_data, TEXTURE_LAYER_SCROLL3);

	for (i = 0; i < OBJECT_TEXTURE_SIZE; i++) object_data[i].index = i;
	for (i = 0; i < SCROLL1_TEXTURE_SIZE; i++) scroll1_data[i].index = i;
	for (i = 0; i < SCROLL2_TEXTURE_SIZE; i++) scroll2_data[i].index = i;
	for (i = 0; i < SCROLL3_TEXTURE_SIZE; i++) scroll3_data[i].index = i;

	clip_min_y = FIRST_VISIBLE_LINE;
	clip_max_y = LAST_VISIBLE_LINE;

	pen_usage = gfx_pen_usage[TILE16];
	clut = (uint16_t *)PSP_UNCACHE_PTR(&video_palette);

	blit_finish_object = blit_render_object;

	blit_clear_all_sprite();
}


/*------------------------------------------------------------------------
	Begin screen update
------------------------------------------------------------------------*/

void blit_start(int start, int end)
{
	int i;

	clip_min_y = start;
	clip_max_y = end + 1;

	object_min_y = start - 16;

	clut0_num = 0;
	clut1_num = 0;

	object_index = 0;
	for (i = 0; i < 8; i++)
	{
		object_num[i] = 0;
		vertices_object_head[i] = NULL;
	}

	if (start == FIRST_VISIBLE_LINE)
	{
		if (cps2_has_mask)
			blit_finish_object = blit_render_object_zb;
		else
			blit_finish_object = blit_render_object;

		video_driver->beginFrame(video_data);
		video_driver->startWorkFrame(video_data, 0);
		video_driver->scissor(video_data, 0, 0, SCR_WIDTH, SCR_HEIGHT);

		if (cps2_has_mask)
			video_driver->clearDepthBuffer(video_data);
	}
}


/*------------------------------------------------------------------------
	End screen update
------------------------------------------------------------------------*/

void blit_finish(void)
{
	if (cps2_has_mask) video_driver->clearFrame(video_data, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER);

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

void blit_draw_object(int16_t x, int16_t y, uint16_t z, int16_t pri, uint32_t code, uint16_t attr)
{
	if ((x > 48 && x < 448) && (y > object_min_y && y < clip_max_y))
	{
		int16_t idx;
		OBJECT *object;
		struct Vertex *vertices;
		uint32_t key = MAKE_KEY(code, attr);

		if ((idx = object_get_sprite(key)) < 0)
		{
			uint32_t col, tile;
			uint8_t *src, *dst, lines = 16;

			if (object_texture_num == OBJECT_TEXTURE_SIZE - 1)
			{
				cps2_scan_object_callback();
				object_delete_sprite();
			}

			idx = object_insert_sprite(key);
			dst = SWIZZLED8_16x16(tex_object, idx);
#if USE_CACHE
			src = &memory_region_gfx1[(*read_cache)(code << 7)];
#else
			src = &memory_region_gfx1[code << 7];
#endif
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

		object = &vertices_object[object_index++];
		object->clut = attr & 0x10;
		object->next = NULL;

		if (!vertices_object_head[pri])
			vertices_object_head[pri] = object;
		else
			vertices_object_tail[pri]->next = object;

		vertices_object_tail[pri] = object;

		vertices = object->vertices;

		vertices[0].x = vertices[1].x = x;
		vertices[0].y = vertices[1].y = y;
		vertices[0].z = vertices[1].z = z;
		vertices[0].u = vertices[1].u = (idx & 0x001f) << 4;
		vertices[0].v = vertices[1].v = (idx & 0x03e0) >> 1;

		attr ^= 0x60;
		vertices[(attr & 0x20) >> 5].u += 16;
		vertices[(attr & 0x40) >> 6].v += 16;

		vertices[1].x += 16;
		vertices[1].y += 16;

		object_num[pri] += 2;
	}
}


/*------------------------------------------------------------------------
	OBJECT rendering (no Z-buffer)
------------------------------------------------------------------------*/

static void blit_render_object(int start_pri, int end_pri)
{
	int i, total_sprites = 0;
	uint8_t color = 0;
	struct Vertex *vertices, *vertices_tmp;
	OBJECT *object;
	int size = 0;

	for (i = start_pri; i <= end_pri; i++)
		size += object_num[i];

	if (!size) return;

	video_driver->uploadMem(video_data, TEXTURE_LAYER_OBJECT);
	video_driver->scissor(video_data, 64, clip_min_y, 448, clip_max_y);

	/* Use pre-allocated flat vertex buffer */
	vertices_tmp = vertices = vertices_object_flat;

	for (i = start_pri; i <= end_pri; i++)
	{
		object = vertices_object_head[i];

		while (object)
		{
			if (color != object->clut)
			{
				color = object->clut;

				if (total_sprites)
				{
					video_driver->uploadClut(video_data, &clut[(color ? 0 : 16) << 4], 0);
					video_driver->flushCache(video_data, vertices, total_sprites * sizeof(struct Vertex));
					video_driver->blitTexture(video_data, TEXTURE_LAYER_OBJECT, &clut[(color ? 0 : 16) << 4], 0, total_sprites, vertices);
					total_sprites = 0;
					vertices = vertices_tmp;
				}

				video_driver->uploadClut(video_data, &clut[color << 4], 0);
			}

			vertices_tmp[0] = object->vertices[0];
			vertices_tmp[1] = object->vertices[1];

			total_sprites += 2;
			vertices_tmp += 2;
			object = object->next;
		}
	}

	if (total_sprites)
	{
		video_driver->flushCache(video_data, vertices, total_sprites * sizeof(struct Vertex));
		video_driver->blitTexture(video_data, TEXTURE_LAYER_OBJECT, &clut[color << 4], 0, total_sprites, vertices);
	}
}


/*------------------------------------------------------------------------
	OBJECT rendering (Z-buffer / priority 0)
------------------------------------------------------------------------*/

static void blit_render_object_zb0(void)
{
	int size = object_num[0], total_sprites = 0;
	struct Vertex *vertices, *vertices_tmp;
	OBJECT *object;

	video_driver->scissor(video_data, 64, clip_min_y, 448, clip_max_y);
	video_driver->uploadMem(video_data, TEXTURE_LAYER_OBJECT);
	video_driver->uploadClut(video_data, clut, 0);
	video_driver->enableDepthTest(video_data);

	vertices_tmp = vertices = vertices_object_flat;

	object = vertices_object_head[0];

	while (object)
	{
		vertices_tmp[0] = object->vertices[0];
		vertices_tmp[1] = object->vertices[1];

		total_sprites += 2;
		vertices_tmp += 2;
		object = object->next;
	}

	video_driver->flushCache(video_data, vertices, total_sprites * sizeof(struct Vertex));
	video_driver->blitTexture(video_data, TEXTURE_LAYER_OBJECT, clut, 0, total_sprites, vertices);

	video_driver->disableDepthTest(video_data);
	video_driver->clearColorBuffer(video_data);
}


/*------------------------------------------------------------------------
	OBJECT rendering (Z-buffer)
------------------------------------------------------------------------*/

static void blit_render_object_zb(int start_pri, int end_pri)
{
	int i, size = 0, total_sprites = 0;
	uint8_t color = 0;
	struct Vertex *vertices, *vertices_tmp;
	OBJECT *object;

	if (start_pri == 0 && object_num[0] != 0)
	{
		blit_render_object_zb0();
		start_pri = 1;
	}

	for (i = start_pri; i <= end_pri; i++)
		size += object_num[i];

	if (!size) return;

	video_driver->scissor(video_data, 64, clip_min_y, 448, clip_max_y);
	video_driver->uploadMem(video_data, TEXTURE_LAYER_OBJECT);
	video_driver->uploadClut(video_data, clut, 0);
	video_driver->enableDepthTest(video_data);

	vertices_tmp = vertices = vertices_object_flat;

	for (i = start_pri; i <= end_pri; i++)
	{
		object = vertices_object_head[i];

		while (object)
		{
			if (color != object->clut)
			{
				color = object->clut;

				if (total_sprites)
				{
					video_driver->flushCache(video_data, vertices, total_sprites * sizeof(struct Vertex));
					video_driver->blitTexture(video_data, TEXTURE_LAYER_OBJECT, &clut[(color ? 0 : 16) << 4], 0, total_sprites, vertices);
					total_sprites = 0;
					vertices = vertices_tmp;
				}

				video_driver->uploadClut(video_data, &clut[color << 4], 0);
			}

			vertices_tmp[0] = object->vertices[0];
			vertices_tmp[1] = object->vertices[1];

			total_sprites += 2;
			vertices_tmp += 2;
			object = object->next;
		}
	}

	if (total_sprites)
	{
		video_driver->flushCache(video_data, vertices, total_sprites * sizeof(struct Vertex));
		video_driver->blitTexture(video_data, TEXTURE_LAYER_OBJECT, &clut[color << 4], 0, total_sprites, vertices);
	}

	video_driver->disableDepthTest(video_data);
}


/*------------------------------------------------------------------------
	Register SCROLL1 to draw list
------------------------------------------------------------------------*/

void blit_draw_scroll1(int16_t x, int16_t y, uint32_t code, uint16_t attr)
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
			cps2_scan_scroll1_callback();
			scroll1_delete_sprite();
		}

		idx = scroll1_insert_sprite(key);
		dst = SWIZZLED8_8x8(tex_scroll1, idx);
#if USE_CACHE
		src = &memory_region_gfx1[(*read_cache)(code << 6)];
#else
		src = &memory_region_gfx1[code << 6];
#endif
		col = color_table[attr & 0x0f];

		while (lines--)
		{
			tile = *(uint32_t *)(src + 4);
			*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;
			src += 8;
			dst += 16;
		}
	}

	if (attr & 0x10)
	{
		vertices = &vertices_scroll[1][clut1_num];
		clut1_num += 2;
	}
	else
	{
		vertices = &vertices_scroll[0][clut0_num];
		clut0_num += 2;
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

	if (clut0_num + clut1_num == 0) return;

	video_driver->uploadMem(video_data, TEXTURE_LAYER_SCROLL1);
	video_driver->scissor(video_data, 64, clip_min_y, 448, clip_max_y);

	if (clut0_num)
	{
		current_clut = &clut[32 << 4];
		video_driver->uploadClut(video_data, current_clut, 0);
		video_driver->flushCache(video_data, vertices_scroll[0], clut0_num * sizeof(struct Vertex));
		video_driver->blitTexture(video_data, TEXTURE_LAYER_SCROLL1, current_clut, 0, clut0_num, vertices_scroll[0]);
	}
	if (clut1_num)
	{
		current_clut = &clut[48 << 4];
		video_driver->uploadClut(video_data, current_clut, 0);
		video_driver->flushCache(video_data, vertices_scroll[1], clut1_num * sizeof(struct Vertex));
		video_driver->blitTexture(video_data, TEXTURE_LAYER_SCROLL1, current_clut, 0, clut1_num, vertices_scroll[1]);
	}

	clut0_num = 0;
	clut1_num = 0;
}


/*------------------------------------------------------------------------
	Set SCROLL2 clip range and select rendering method
------------------------------------------------------------------------*/

void blit_set_clip_scroll2(int16_t min_y, int16_t max_y)
{
	scroll2_min_y = min_y;
	scroll2_max_y = max_y + 1;

	if (scroll2_max_y - scroll2_min_y >= 16)
		blit_draw_scroll2 = blit_draw_scroll2_hardware;
	else
		blit_draw_scroll2 = blit_draw_scroll2_software;
}


/*------------------------------------------------------------------------
	Draw SCROLL2 (line scroll) directly to VRAM
------------------------------------------------------------------------*/

static void blit_draw_scroll2_software(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	uint32_t src, dst;
	uint8_t func;

#if USE_CACHE
	src = (*read_cache)(code << 7);
#else
	src = code << 7;
#endif

	if (attr & 0x40)
	{
		src += ((y + 16) - scroll2_ey) << 3;
		dst = ((scroll2_ey - 1) << 9) + x;
	}
	else
	{
		src += (scroll2_sy - y) << 3;
		dst = (scroll2_sy << 9) + x;
	}

	func = (pen_usage[code] & 1) | ((attr & 0x60) >> 4);

	(*drawgfx16[func])((uint32_t *)&memory_region_gfx1[src],
					&scrbitmap[dst],
					&video_palette[((attr & 0x1f) + 64) << 4],
					scroll2_ey - scroll2_sy);
}


/*------------------------------------------------------------------------
	Register SCROLL2 to draw list
------------------------------------------------------------------------*/

static void blit_draw_scroll2_hardware(int16_t x, int16_t y, uint32_t code, uint16_t attr)
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
			cps2_scan_scroll2_callback();
			scroll2_delete_sprite();
		}

		idx = scroll2_insert_sprite(key);
		dst = SWIZZLED8_16x16(tex_scroll2, idx);
#if USE_CACHE
		src = &memory_region_gfx1[(*read_cache)(code << 7)];
#else
		src = &memory_region_gfx1[code << 7];
#endif
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
		vertices = &vertices_scroll[1][clut1_num];
		clut1_num += 2;
	}
	else
	{
		vertices = &vertices_scroll[0][clut0_num];
		clut0_num += 2;
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

	if (clut0_num + clut1_num == 0) return;

	video_driver->scissor(video_data, 64, scroll2_min_y, 448, scroll2_max_y);
	video_driver->uploadMem(video_data, TEXTURE_LAYER_SCROLL2);

	if (clut0_num)
	{
		current_clut = &clut[64 << 4];
		video_driver->uploadClut(video_data, current_clut, 0);
		video_driver->flushCache(video_data, vertices_scroll[0], clut0_num * sizeof(struct Vertex));
		video_driver->blitTexture(video_data, TEXTURE_LAYER_SCROLL2, current_clut, 0, clut0_num, vertices_scroll[0]);
	}
	if (clut1_num)
	{
		current_clut = &clut[80 << 4];
		video_driver->uploadClut(video_data, current_clut, 0);
		video_driver->flushCache(video_data, vertices_scroll[1], clut1_num * sizeof(struct Vertex));
		video_driver->blitTexture(video_data, TEXTURE_LAYER_SCROLL2, current_clut, 0, clut1_num, vertices_scroll[1]);
	}

	clut0_num = 0;
	clut1_num = 0;
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
			cps2_scan_scroll3_callback();
			scroll3_delete_sprite();
		}

		idx = scroll3_insert_sprite(key);
		dst = SWIZZLED8_32x32(tex_scroll3, idx);
#if USE_CACHE
		src = &memory_region_gfx1[(*read_cache)(code << 9)];
#else
		src = &memory_region_gfx1[code << 9];
#endif
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
		vertices = &vertices_scroll[1][clut1_num];
		clut1_num += 2;
	}
	else
	{
		vertices = &vertices_scroll[0][clut0_num];
		clut0_num += 2;
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

	if (clut0_num + clut1_num == 0) return;

	video_driver->uploadMem(video_data, TEXTURE_LAYER_SCROLL3);
	video_driver->scissor(video_data, 64, clip_min_y, 448, clip_max_y);

	if (clut0_num)
	{
		current_clut = &clut[96 << 4];
		video_driver->uploadClut(video_data, current_clut, 0);
		video_driver->flushCache(video_data, vertices_scroll[0], clut0_num * sizeof(struct Vertex));
		video_driver->blitTexture(video_data, TEXTURE_LAYER_SCROLL3, current_clut, 0, clut0_num, vertices_scroll[0]);
	}
	if (clut1_num)
	{
		current_clut = &clut[112 << 4];
		video_driver->uploadClut(video_data, current_clut, 0);
		video_driver->flushCache(video_data, vertices_scroll[1], clut1_num * sizeof(struct Vertex));
		video_driver->blitTexture(video_data, TEXTURE_LAYER_SCROLL3, current_clut, 0, clut1_num, vertices_scroll[1]);
	}

	clut0_num = 0;
	clut1_num = 0;
}
