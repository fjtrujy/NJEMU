/******************************************************************************

	sprite.c

	CPS2 platform-neutral sprite renderer

	This file owns CPS2 sprite decoding, cache policy, batching, priority and
	presentation semantics. Platform backends own texture layout, vertex
	materialization and GPU submission.

******************************************************************************/

#include "cps2.h"
#include "sprite_common.h"
#include "common/cache.h"
#include "common/emulator_options.h"
#include "common/video_driver.h"
#include "common/video_geometry.h"


/******************************************************************************
	Prototypes
******************************************************************************/

void (*blit_finish_object)(int start_pri, int end_pri);
static void blit_render_object(int start_pri, int end_pri);
static void blit_render_object_zb(int start_pri, int end_pri);


/******************************************************************************
	Renderer state and portable helpers
******************************************************************************/

typedef struct cps_presentation_size
{
	int16_t width;
	int16_t height;
} cps_presentation_size_t;

static RECT cps_src_clip = { 64, 16, 64 + 384, 16 + 224 };

/* option_stretch semantics shared with common/menu/cps.c. The final entry is
 * reserved for the rotated CPS presentation path. */
static const cps_presentation_size_t cps_presentation_sizes[6] =
{
	{ 384, 224 },	/* OFF/native */
	{ 360, 270 },	/* 4:3 */
	{ 384, 270 },	/* 24:17 */
	{ 466, 272 },	/* 12:7 */
	{ 480, 270 },	/* 16:9 */
	{ 204, 272 }	/* rotated 3:4 */
};

static RECT cps_presentation_rect(int option, bool scale_logical)
{
	int output_width = SCR_WIDTH;
	int output_height = SCR_HEIGHT;
	int width;
	int height;
	RECT rect;

	if (option < 0 || option >= (int)(sizeof(cps_presentation_sizes) /
			sizeof(cps_presentation_sizes[0])))
		option = 0;
	if (video_driver->getOutputSize)
		video_driver->getOutputSize(video_data, &output_width, &output_height);

	width = cps_presentation_sizes[option].width;
	height = cps_presentation_sizes[option].height;
	if (scale_logical)
		video_scale_logical_size(output_width, output_height,
			width, height, &width, &height);

	rect.left = (int16_t)((output_width - width) / 2);
	rect.top = (int16_t)((output_height - height) / 2);
	rect.right = (int16_t)(rect.left + width);
	rect.bottom = (int16_t)(rect.top + height);
	return rect;
}

static void cps_atlas_position(int16_t index, int tile_size, int *x, int *y)
{
	int tiles_per_line = BUF_WIDTH / tile_size;
	*x = (index % tiles_per_line) * tile_size;
	*y = (index / tiles_per_line) * tile_size;
}

static void cps_decode_indexed_tile(uint8_t *pixels, int tile_size,
	const uint8_t *src, int src_stride, uint32_t palette)
{
	int y;
	for (y = 0; y < tile_size; y++) {
		int group;
		for (group = 0; group < tile_size / 8; group++) {
			uint32_t tile = *(const uint32_t *)(src + group * 4);
			uint32_t *dst = (uint32_t *)&pixels[y * tile_size + group * 8];
			dst[0] = ((tile >> 0) & 0x0f0f0f0f) | palette;
			dst[1] = ((tile >> 4) & 0x0f0f0f0f) | palette;
		}
		src += src_stride;
	}
}

static void cps_cache_indexed_tile(uint8_t layer, int16_t index, int tile_size,
	const uint8_t *src, int src_stride, uint32_t palette)
{
	uint8_t pixels[32 * 32] __attribute__((aligned(4)));
	int x;
	int y;

	cps_decode_indexed_tile(pixels, tile_size, src, src_stride, palette);
	cps_atlas_position(index, tile_size, &x, &y);
	video_driver->writeIndexedTextureRect(video_data, layer,
		x, y, tile_size, tile_size, pixels, tile_size);
}

static void cps_set_sprite_vertices(video_sprite_vertex_t *vertices,
	int16_t x, int16_t y, uint16_t z, int16_t index, int tile_size, uint16_t attr)
{
	int atlas_x;
	int atlas_y;

	cps_atlas_position(index, tile_size, &atlas_x, &atlas_y);
	vertices[0].x = vertices[1].x = x;
	vertices[0].y = vertices[1].y = y;
	vertices[0].z = vertices[1].z = (int16_t)z;
	vertices[0].u = vertices[1].u = (uint16_t)atlas_x;
	vertices[0].v = vertices[1].v = (uint16_t)atlas_y;
	vertices[0].color = vertices[1].color = 0;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += (uint16_t)tile_size;
	vertices[(attr & 0x40) >> 6].v += (uint16_t)tile_size;
	vertices[1].x += (int16_t)tile_size;
	vertices[1].y += (int16_t)tile_size;
}


/*------------------------------------------------------------------------
	Vertex Data
------------------------------------------------------------------------*/

typedef struct object_t OBJECT;

struct object_t
{
	uint32_t clut;
	video_sprite_vertex_t vertices[2];
	OBJECT *next;
};

/* CLUT */
static uint16_t *clut;

/* OBJECT priority-based linked lists */
static OBJECT *vertices_object_head[8];
static OBJECT *vertices_object_tail[8];
static OBJECT ALIGN16_DATA vertices_object[OBJECT_MAX_SPRITES];

static uint16_t object_num[8];
static uint16_t object_index;

/* Flattened object vertex buffer for rendering */
static video_sprite_vertex_t ALIGN16_DATA vertices_object_flat[OBJECT_MAX_SPRITES * 2];

/* Scroll layers are submitted sequentially. Reuse the two largest portable
 * buffers just as the original PSP renderer did; PS2 materializes each batch
 * directly into its final gsKit queue and Desktop submits synchronously. */
static video_sprite_vertex_t ALIGN16_DATA
	vertices_scroll[2][SCROLL1_MAX_SPRITES * 2];

_Static_assert(SCROLL1_MAX_SPRITES >= SCROLL2_MAX_SPRITES,
	"shared CPS2 scroll vertex buffer must fit SCROLL2");
_Static_assert(SCROLL1_MAX_SPRITES >= SCROLL3_MAX_SPRITES,
	"shared CPS2 scroll vertex buffer must fit SCROLL3");

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

	for (i = 0; i < OBJECT_TEXTURE_SIZE; i++) object_data[i].index = i;
	for (i = 0; i < SCROLL1_TEXTURE_SIZE; i++) scroll1_data[i].index = i;
	for (i = 0; i < SCROLL2_TEXTURE_SIZE; i++) scroll2_data[i].index = i;
	for (i = 0; i < SCROLL3_TEXTURE_SIZE; i++) scroll3_data[i].index = i;

	clip_min_y = FIRST_VISIBLE_LINE;
	clip_max_y = LAST_VISIBLE_LINE;

	pen_usage = gfx_pen_usage[TILE16];
	clut = (uint16_t *)&video_palette;

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

		/* PS2 uploads the complete CPS2 palette to GS VRAM here. PSP/Desktop
		 * keep their backend-specific CLUT coherency/materialization semantics. */
		video_driver->uploadClut(video_data, clut, 0);
	}
}


/*------------------------------------------------------------------------
	End screen update
------------------------------------------------------------------------*/

void blit_finish(void)
{
	RECT dst_clip;

	if (cps2_has_mask)
		video_driver->clearFrame(video_data, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER);

	if (cps_rotate_screen) {
		if (cps_flip_screen) {
			video_driver->copyRectFlip(video_data,
				COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP,
				COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER,
				&cps_src_clip, &cps_src_clip);
			video_driver->copyRect(video_data,
				COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER,
				COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP,
				&cps_src_clip, &cps_src_clip);
			video_driver->clearFrame(video_data,
				COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER);
		}
		dst_clip = cps_presentation_rect(5, true);
		video_driver->copyRectRotate(video_data,
			COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP,
			COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER,
			&cps_src_clip, &dst_clip);
	} else {
		dst_clip = cps_presentation_rect(option_stretch, option_stretch != 0);
		if (cps_flip_screen)
			video_driver->copyRectFlip(video_data,
				COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP,
				COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER,
				&cps_src_clip, &dst_clip);
		else
			video_driver->transferWorkFrame(video_data,
				&cps_src_clip, &dst_clip);
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
		video_sprite_vertex_t *vertices;
		uint32_t key = MAKE_KEY(code, attr);

		if ((idx = object_get_sprite(key)) < 0)
		{
			const uint8_t *src;
			uint32_t palette = sprite_color_table[attr & 0x0f];

			if (object_texture_num == OBJECT_TEXTURE_SIZE - 1) {
				cps2_scan_object_callback();
				object_delete_sprite();
			}
			idx = object_insert_sprite(key);
			if (idx < 0) return;
#if USE_CACHE
			src = &memory_region_gfx1[(*read_cache)(code << 7)];
#else
			src = &memory_region_gfx1[code << 7];
#endif
			cps_cache_indexed_tile(TEXTURE_LAYER_OBJECT, idx, 16,
				src, 8, palette);
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

		cps_set_sprite_vertices(vertices, x, y, z, idx, 16, attr);

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
	video_sprite_vertex_t *vertices, *vertices_tmp;
	OBJECT *object;
	int size = 0;

	for (i = start_pri; i <= end_pri; i++)
		size += object_num[i];

	if (!size) return;

	video_driver->uploadMem(video_data, TEXTURE_LAYER_OBJECT);
	video_driver->scissor(video_data, 64, clip_min_y, 448, clip_max_y);

	vertices_tmp = vertices = vertices_object_flat;

	for (i = start_pri; i <= end_pri; i++)
	{
		object = vertices_object_head[i];

		while (object)
		{
			if (color != object->clut)
			{
				if (total_sprites)
				{
					video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_OBJECT, &clut[color << 4], 0, total_sprites, vertices);
					total_sprites = 0;
					vertices = vertices_tmp;
				}
				color = object->clut;
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
		video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_OBJECT, &clut[color << 4], 0, total_sprites, vertices);
	}
}


/*------------------------------------------------------------------------
	OBJECT rendering (Z-buffer / priority 0)
------------------------------------------------------------------------*/

static void blit_render_object_zb0(void)
{
	int total_sprites = 0;
	video_sprite_vertex_t *vertices, *vertices_tmp;
	OBJECT *object;

	video_driver->scissor(video_data, 64, clip_min_y, 448, clip_max_y);
	video_driver->uploadMem(video_data, TEXTURE_LAYER_OBJECT);
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
	video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_OBJECT, clut, 0, total_sprites, vertices);

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
	video_sprite_vertex_t *vertices, *vertices_tmp;
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
	video_driver->enableDepthTest(video_data);

	vertices_tmp = vertices = vertices_object_flat;

	for (i = start_pri; i <= end_pri; i++)
	{
		object = vertices_object_head[i];

		while (object)
		{
			if (color != object->clut)
			{
				if (total_sprites)
				{
					video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_OBJECT, &clut[color << 4], 0, total_sprites, vertices);
					total_sprites = 0;
					vertices = vertices_tmp;
				}
				color = object->clut;
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
		video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_OBJECT, &clut[color << 4], 0, total_sprites, vertices);
	}

	video_driver->disableDepthTest(video_data);
}


/*------------------------------------------------------------------------
	Register SCROLL1 to draw list
------------------------------------------------------------------------*/

void blit_draw_scroll1(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	int16_t idx;
	video_sprite_vertex_t *vertices;
	uint32_t key = MAKE_KEY(code, attr);

	if ((idx = scroll1_get_sprite(key)) < 0)
	{
		const uint8_t *src;
		uint32_t palette = sprite_color_table[attr & 0x0f];

		if (scroll1_texture_num == SCROLL1_TEXTURE_SIZE - 1) {
			cps2_scan_scroll1_callback();
			scroll1_delete_sprite();
		}
		idx = scroll1_insert_sprite(key);
		if (idx < 0) return;
#if USE_CACHE
		src = &memory_region_gfx1[(*read_cache)(code << 6)];
#else
		src = &memory_region_gfx1[code << 6];
#endif
		cps_cache_indexed_tile(TEXTURE_LAYER_SCROLL1, idx, 8,
			src + 4, 8, palette);
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

	cps_set_sprite_vertices(vertices, x, y, 0, idx, 8, attr);
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
		video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_SCROLL1, current_clut, 0, clut0_num, vertices_scroll[0]);
	}
	if (clut1_num)
	{
		current_clut = &clut[48 << 4];
		video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_SCROLL1, current_clut, 0, clut1_num, vertices_scroll[1]);
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
}


/*------------------------------------------------------------------------
	Register SCROLL2 to draw list
------------------------------------------------------------------------*/

void blit_draw_scroll2(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	int16_t idx;
	video_sprite_vertex_t *vertices;
	uint32_t key = MAKE_KEY(code, attr);

	if ((idx = scroll2_get_sprite(key)) < 0)
	{
		const uint8_t *src;
		uint32_t palette = sprite_color_table[attr & 0x0f];

		if (scroll2_texture_num == SCROLL2_TEXTURE_SIZE - 1) {
			cps2_scan_scroll2_callback();
			scroll2_delete_sprite();
		}
		idx = scroll2_insert_sprite(key);
		if (idx < 0) return;
#if USE_CACHE
		src = &memory_region_gfx1[(*read_cache)(code << 7)];
#else
		src = &memory_region_gfx1[code << 7];
#endif
		cps_cache_indexed_tile(TEXTURE_LAYER_SCROLL2, idx, 16,
			src, 8, palette);
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

	cps_set_sprite_vertices(vertices, x, y, 0, idx, 16, attr);
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
		video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_SCROLL2, current_clut, 0, clut0_num, vertices_scroll[0]);
	}
	if (clut1_num)
	{
		current_clut = &clut[80 << 4];
		video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_SCROLL2, current_clut, 0, clut1_num, vertices_scroll[1]);
	}

	/* Restore full screen scissor */
	video_driver->scissor(video_data, 64, 16, 448, 240);

	clut0_num = 0;
	clut1_num = 0;
}


/*------------------------------------------------------------------------
	Register SCROLL3 to draw list
------------------------------------------------------------------------*/

void blit_draw_scroll3(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	int16_t idx;
	video_sprite_vertex_t *vertices;
	uint32_t key = MAKE_KEY(code, attr);

	if ((idx = scroll3_get_sprite(key)) < 0)
	{
		const uint8_t *src;
		uint32_t palette = sprite_color_table[attr & 0x0f];

		if (scroll3_texture_num == SCROLL3_TEXTURE_SIZE - 1) {
			cps2_scan_scroll3_callback();
			scroll3_delete_sprite();
		}
		idx = scroll3_insert_sprite(key);
		if (idx < 0) return;
#if USE_CACHE
		src = &memory_region_gfx1[(*read_cache)(code << 9)];
#else
		src = &memory_region_gfx1[code << 9];
#endif
		cps_cache_indexed_tile(TEXTURE_LAYER_SCROLL3, idx, 32,
			src, 16, palette);
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

	cps_set_sprite_vertices(vertices, x, y, 0, idx, 32, attr);
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
		video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_SCROLL3, current_clut, 0, clut0_num, vertices_scroll[0]);
	}
	if (clut1_num)
	{
		current_clut = &clut[112 << 4];
		video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_SCROLL3, current_clut, 0, clut1_num, vertices_scroll[1]);
	}

	clut0_num = 0;
	clut1_num = 0;
}
