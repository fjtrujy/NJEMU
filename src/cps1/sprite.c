/******************************************************************************

	sprite.c

	CPS1 platform-neutral sprite renderer

******************************************************************************/

#include "cps1.h"
#include "sprite_common.h"
#include "common/emulator_options.h"
#include "common/display_mode.h"
#include "common/video_driver.h"
#include "common/video_geometry.h"

#include <stdlib.h>
#include <string.h>

/******************************************************************************
	Renderer state
******************************************************************************/

typedef struct object_batch
{
	uint16_t start;
	uint16_t count;
	uint16_t *clut;
} object_batch_t;

#define OBJECT_MAX_BATCHES 256

static RECT cps_src_clip = { 64, 16, 64 + 384, 16 + 224 };

static uint16_t *clut;

static video_sprite_vertex_t __attribute__((aligned(64)))
	vertices_object[OBJECT_MAX_SPRITES * 2];
static object_batch_t object_batches[OBJECT_MAX_BATCHES];
static uint16_t object_num;
static uint16_t object_batch_count;
static uint16_t *object_current_clut;

static video_sprite_vertex_t __attribute__((aligned(64)))
	vertices_scroll1_clut0[SCROLL1_MAX_SPRITES * 2];
static video_sprite_vertex_t __attribute__((aligned(64)))
	vertices_scroll1_clut1[SCROLL1_MAX_SPRITES * 2];
static uint16_t scroll1_clut0_num;
static uint16_t scroll1_clut1_num;

static video_sprite_vertex_t __attribute__((aligned(64)))
	vertices_scroll2_clut0[SCROLL2_MAX_SPRITES * 2];
static video_sprite_vertex_t __attribute__((aligned(64)))
	vertices_scroll2_clut1[SCROLL2_MAX_SPRITES * 2];
static uint16_t scroll2_clut0_num;
static uint16_t scroll2_clut1_num;

static video_sprite_vertex_t __attribute__((aligned(64)))
	vertices_scroll3_clut0[SCROLL3_MAX_SPRITES * 2];
static video_sprite_vertex_t __attribute__((aligned(64)))
	vertices_scroll3_clut1[SCROLL3_MAX_SPRITES * 2];
static uint16_t scroll3_clut0_num;
static uint16_t scroll3_clut1_num;

static video_sprite_vertex_t __attribute__((aligned(64)))
	vertices_scrollh[SCROLLH_MAX_SPRITES * 2];
static video_point_vertex_t *vertices_stars;

/******************************************************************************
	Portable helpers
******************************************************************************/

static RECT cps_presentation_rect(int native_width, int native_height)
{
	return display_mode_presentation_rect_aspect(option_display_mode,
		native_width, native_height,
		native_width < native_height ? 3 : 4,
		native_width < native_height ? 4 : 3);
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

static void cps_decode_direct_tile(uint16_t *pixels, int tile_size,
	const uint8_t *src, int src_stride, const uint16_t *palette)
{
	static const uint8_t order[8] = { 0, 4, 1, 5, 2, 6, 3, 7 };
	int y;

	for (y = 0; y < tile_size; y++) {
		int group;
		for (group = 0; group < tile_size / 8; group++) {
			uint32_t tile = *(const uint32_t *)(src + group * 4);
			uint16_t *dst = &pixels[y * tile_size + group * 8];
			int pixel;
			for (pixel = 0; pixel < 8; pixel++) {
				dst[order[pixel]] = palette[tile & 0x0f];
				tile >>= 4;
			}
		}
		src += src_stride;
	}
}

static void cps_cache_direct_tile(int16_t index, int tile_size,
	const uint8_t *src, int src_stride, const uint16_t *palette)
{
	uint16_t pixels[32 * 32] __attribute__((aligned(4)));
	int x;
	int y;

	cps_decode_direct_tile(pixels, tile_size, src, src_stride, palette);
	cps_atlas_position(index, tile_size, &x, &y);
	video_driver->writeDirectTextureRect(video_data, TEXTURE_LAYER_SCROLLH,
		x, y, tile_size, tile_size, pixels, tile_size);
}

static void cps_set_sprite_vertices(video_sprite_vertex_t *vertices,
	int16_t x, int16_t y, int16_t index, int tile_size, uint16_t attr)
{
	int atlas_x;
	int atlas_y;

	cps_atlas_position(index, tile_size, &atlas_x, &atlas_y);
	vertices[0].x = vertices[1].x = x;
	vertices[0].y = vertices[1].y = y;
	vertices[0].u = vertices[1].u = (uint16_t)atlas_x;
	vertices[0].v = vertices[1].v = (uint16_t)atlas_y;
	vertices[0].color = vertices[1].color = 0;
	vertices[0].z = vertices[1].z = 0;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += (uint16_t)tile_size;
	vertices[(attr & 0x40) >> 6].v += (uint16_t)tile_size;
	vertices[1].x += (int16_t)tile_size;
	vertices[1].y += (int16_t)tile_size;
}

static const uint16_t *cps_high_palette(uint16_t attr, uint16_t base_palette,
	uint16_t tpens, uint16_t filtered[16])
{
	const uint16_t *palette = &video_palette[((attr & 0x1f) + base_palette) << 4];
	int i;

	if (tpens == 0x7fff)
		return palette;
	for (i = 0; i < 15; i++)
		filtered[i] = (tpens & (1 << i)) ? palette[i] : 0x8000;
	filtered[15] = 0x8000;
	return filtered;
}

/******************************************************************************
	Sprite drawing interface
******************************************************************************/

int blit_stars_init(int enabled)
{
	free(vertices_stars);
	vertices_stars = NULL;
	if (!enabled)
		return 1;
	vertices_stars = (video_point_vertex_t *)malloc(
		STARS_MAX_POINTS * sizeof(*vertices_stars));
	return vertices_stars != NULL;
}

void blit_stars_exit(void)
{
	free(vertices_stars);
	vertices_stars = NULL;
}

void blit_reset(int bank_scroll1, int bank_scroll2, int bank_scroll3,
	uint8_t *pen_usage16)
{
	int i;

	for (i = 0; i < OBJECT_TEXTURE_SIZE; i++) object_data[i].index = i;
	for (i = 0; i < SCROLL1_TEXTURE_SIZE; i++) scroll1_data[i].index = i;
	for (i = 0; i < SCROLL2_TEXTURE_SIZE; i++) scroll2_data[i].index = i;
	for (i = 0; i < SCROLL3_TEXTURE_SIZE; i++) scroll3_data[i].index = i;
	for (i = 0; i < SCROLLH_TEXTURE_SIZE; i++) scrollh_data[i].index = i;

	gfx_object = memory_region_gfx1;
	gfx_scroll1 = &memory_region_gfx1[bank_scroll1 << 21];
	gfx_scroll2 = &memory_region_gfx1[bank_scroll2 << 21];
	gfx_scroll3 = &memory_region_gfx1[bank_scroll3 << 21];
	pen_usage = pen_usage16;
	clut = (uint16_t *)&video_palette;
	blit_clear_all_sprite();
}

void blit_start(int high_layer)
{
	if (scrollh_texture_clear || high_layer != scrollh_layer_number) {
		scrollh_reset_sprite();
		scrollh_layer_number = (uint8_t)high_layer;
	}

	scrollh_delete_dirty_palette();
	memset(palette_dirty_marks, 0, sizeof(palette_dirty_marks));
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

	video_driver->beginFrame(video_data);
	video_driver->startWorkFrame(video_data, 0);
	video_driver->scissor(video_data, 64, 16, 448, 240);
	video_driver->uploadClut(video_data, clut, 0);
}

void blit_finish(void)
{
	RECT dst_clip;

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
		dst_clip = cps_presentation_rect(224, 384);
		video_driver->copyRectRotate(video_data,
			COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP,
			COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER,
			&cps_src_clip, &dst_clip);
	} else {
		dst_clip = cps_presentation_rect(384, 224);
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

void blit_draw_object(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	int16_t index;
	video_sprite_vertex_t *vertices;
	uint32_t key;
	uint16_t *sprite_clut;

	if (x <= 47 || x >= 448 || y <= 0 || y >= 239)
		return;
	key = MAKE_KEY(code, attr);
	index = object_get_sprite(key);
	if (index < 0) {
		if (object_texture_num == OBJECT_TEXTURE_SIZE - 1) {
			cps1_scan_object();
			object_delete_sprite();
		}
		index = object_insert_sprite(key);
		if (index < 0) return;
		cps_cache_indexed_tile(TEXTURE_LAYER_OBJECT, index, 16,
			&gfx_object[code << 7], 8, sprite_color_table[attr & 0x0f]);
	}

	sprite_clut = (attr & 0x10) ? &clut[16 << 4] : clut;
	if (sprite_clut != object_current_clut) {
		if (object_current_clut != NULL && object_batch_count > 0)
			object_batches[object_batch_count - 1].count =
				object_num - object_batches[object_batch_count - 1].start;
		if (object_batch_count < OBJECT_MAX_BATCHES) {
			object_batches[object_batch_count].start = object_num;
			object_batches[object_batch_count].count = 0;
			object_batches[object_batch_count].clut = sprite_clut;
			object_batch_count++;
		}
		object_current_clut = sprite_clut;
	}

	if (object_num + 2 > OBJECT_MAX_SPRITES * 2)
		return;
	vertices = &vertices_object[object_num];
	object_num += 2;
	cps_set_sprite_vertices(vertices, x, y, index, 16, attr);
}

void blit_finish_object(void)
{
	uint16_t i;
	if (!object_num) return;
	if (object_batch_count > 0)
		object_batches[object_batch_count - 1].count =
			object_num - object_batches[object_batch_count - 1].start;
	video_driver->uploadMem(video_data, TEXTURE_LAYER_OBJECT);
	for (i = 0; i < object_batch_count; i++) {
		if (object_batches[i].count)
			video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_OBJECT,
				object_batches[i].clut, 0, object_batches[i].count,
				&vertices_object[object_batches[i].start]);
	}
}

void blit_draw_scroll1(int16_t x, int16_t y, uint32_t code, uint16_t attr,
	uint16_t gfxset)
{
	int16_t index = scroll1_get_sprite(MAKE_KEY(code, attr));
	video_sprite_vertex_t *vertices;

	if (index < 0) {
		if (scroll1_texture_num == SCROLL1_TEXTURE_SIZE - 1) {
			cps1_scan_scroll1();
			scroll1_delete_sprite();
		}
		index = scroll1_insert_sprite(MAKE_KEY(code, attr));
		if (index < 0) return;
		cps_cache_indexed_tile(TEXTURE_LAYER_SCROLL1, index, 8,
			&gfx_scroll1[(code << 6) + (gfxset << 2)], 8,
			sprite_color_table[attr & 0x0f]);
	}

	if (attr & 0x10) {
		if (scroll1_clut1_num + 2 > SCROLL1_MAX_SPRITES * 2) return;
		vertices = &vertices_scroll1_clut1[scroll1_clut1_num];
		scroll1_clut1_num += 2;
	} else {
		if (scroll1_clut0_num + 2 > SCROLL1_MAX_SPRITES * 2) return;
		vertices = &vertices_scroll1_clut0[scroll1_clut0_num];
		scroll1_clut0_num += 2;
	}
	cps_set_sprite_vertices(vertices, x, y, index, 8, attr);
}

void blit_finish_scroll1(void)
{
	if (!scroll1_clut0_num && !scroll1_clut1_num) return;
	video_driver->uploadMem(video_data, TEXTURE_LAYER_SCROLL1);
	if (scroll1_clut0_num)
		video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_SCROLL1,
			&clut[32 << 4], 0, scroll1_clut0_num, vertices_scroll1_clut0);
	if (scroll1_clut1_num)
		video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_SCROLL1,
			&clut[48 << 4], 0, scroll1_clut1_num, vertices_scroll1_clut1);
}

void blit_set_clip_scroll2(int16_t min_y, int16_t max_y)
{
	scroll2_min_y = min_y;
	scroll2_max_y = max_y + 1;
}

void blit_draw_scroll2(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	int16_t index = scroll2_get_sprite(MAKE_KEY(code, attr));
	video_sprite_vertex_t *vertices;

	if (index < 0) {
		if (scroll2_texture_num == SCROLL2_TEXTURE_SIZE - 1) {
			cps1_scan_scroll2();
			scroll2_delete_sprite();
		}
		index = scroll2_insert_sprite(MAKE_KEY(code, attr));
		if (index < 0) return;
		cps_cache_indexed_tile(TEXTURE_LAYER_SCROLL2, index, 16,
			&gfx_scroll2[code << 7], 8, sprite_color_table[attr & 0x0f]);
	}

	if (attr & 0x10) {
		if (scroll2_clut1_num + 2 > SCROLL2_MAX_SPRITES * 2) return;
		vertices = &vertices_scroll2_clut1[scroll2_clut1_num];
		scroll2_clut1_num += 2;
	} else {
		if (scroll2_clut0_num + 2 > SCROLL2_MAX_SPRITES * 2) return;
		vertices = &vertices_scroll2_clut0[scroll2_clut0_num];
		scroll2_clut0_num += 2;
	}
	cps_set_sprite_vertices(vertices, x, y, index, 16, attr);
}

void blit_finish_scroll2(void)
{
	if (!scroll2_clut0_num && !scroll2_clut1_num) return;
	video_driver->scissor(video_data, 64, scroll2_min_y, 448, scroll2_max_y);
	video_driver->uploadMem(video_data, TEXTURE_LAYER_SCROLL2);
	if (scroll2_clut0_num)
		video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_SCROLL2,
			&clut[64 << 4], 0, scroll2_clut0_num, vertices_scroll2_clut0);
	if (scroll2_clut1_num)
		video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_SCROLL2,
			&clut[80 << 4], 0, scroll2_clut1_num, vertices_scroll2_clut1);
	video_driver->scissor(video_data, 64, 16, 448, 240);
}

void blit_draw_scroll3(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	int16_t index = scroll3_get_sprite(MAKE_KEY(code, attr));
	video_sprite_vertex_t *vertices;

	if (index < 0) {
		if (scroll3_texture_num == SCROLL3_TEXTURE_SIZE - 1) {
			cps1_scan_scroll3();
			scroll3_delete_sprite();
		}
		index = scroll3_insert_sprite(MAKE_KEY(code, attr));
		if (index < 0) return;
		cps_cache_indexed_tile(TEXTURE_LAYER_SCROLL3, index, 32,
			&gfx_scroll3[code << 9], 16, sprite_color_table[attr & 0x0f]);
	}

	if (attr & 0x10) {
		if (scroll3_clut1_num + 2 > SCROLL3_MAX_SPRITES * 2) return;
		vertices = &vertices_scroll3_clut1[scroll3_clut1_num];
		scroll3_clut1_num += 2;
	} else {
		if (scroll3_clut0_num + 2 > SCROLL3_MAX_SPRITES * 2) return;
		vertices = &vertices_scroll3_clut0[scroll3_clut0_num];
		scroll3_clut0_num += 2;
	}
	cps_set_sprite_vertices(vertices, x, y, index, 32, attr);
}

void blit_finish_scroll3(void)
{
	if (!scroll3_clut0_num && !scroll3_clut1_num) return;
	video_driver->uploadMem(video_data, TEXTURE_LAYER_SCROLL3);
	if (scroll3_clut0_num)
		video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_SCROLL3,
			&clut[96 << 4], 0, scroll3_clut0_num, vertices_scroll3_clut0);
	if (scroll3_clut1_num)
		video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_SCROLL3,
			&clut[112 << 4], 0, scroll3_clut1_num, vertices_scroll3_clut1);
}

void blit_draw_scroll1h(int16_t x, int16_t y, uint32_t code, uint16_t attr,
	uint16_t tpens, uint16_t gfxset)
{
	int16_t index = scrollh_get_sprite(MAKE_HIGH_KEY(code, attr));
	video_sprite_vertex_t *vertices;

	if (index < 0) {
		uint16_t filtered[16];
		const uint16_t *palette;
		if (scrollh_texture_num == SCROLL1H_TEXTURE_SIZE - 1) {
			cps1_scan_scroll1_foreground();
			scrollh_delete_sprite();
		}
		index = scrollh_insert_sprite(MAKE_HIGH_KEY(code, attr));
		if (index < 0) return;
		palette = cps_high_palette(attr, 32, tpens, filtered);
		cps_cache_direct_tile(index, 8,
			&gfx_scroll1[(code << 6) + (gfxset << 2)], 8, palette);
	}

	if (scrollh_num + 2 > SCROLLH_MAX_SPRITES * 2) return;
	vertices = &vertices_scrollh[scrollh_num];
	scrollh_num += 2;
	cps_set_sprite_vertices(vertices, x, y, index, 8, attr);
}

void blit_draw_scroll2h(int16_t x, int16_t y, uint32_t code, uint16_t attr,
	uint16_t tpens)
{
	int16_t index = scrollh_get_sprite(MAKE_HIGH_KEY(code, attr));
	video_sprite_vertex_t *vertices;

	if (index < 0) {
		uint16_t filtered[16];
		const uint16_t *palette;
		if (scrollh_texture_num == SCROLL2H_TEXTURE_SIZE - 1) {
			cps1_scan_scroll2_foreground();
			scrollh_delete_sprite();
		}
		index = scrollh_insert_sprite(MAKE_HIGH_KEY(code, attr));
		if (index < 0) return;
		palette = cps_high_palette(attr, 64, tpens, filtered);
		cps_cache_direct_tile(index, 16, &gfx_scroll2[code << 7], 8, palette);
	}

	if (scrollh_num + 2 > SCROLLH_MAX_SPRITES * 2) return;
	vertices = &vertices_scrollh[scrollh_num];
	scrollh_num += 2;
	cps_set_sprite_vertices(vertices, x, y, index, 16, attr);
}

void blit_finish_scroll2h(void)
{
	if (!scrollh_num) return;
	video_driver->uploadMem(video_data, TEXTURE_LAYER_SCROLLH);
	video_driver->scissor(video_data, 64, scroll2_min_y, 448, scroll2_max_y);
	video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_SCROLLH,
		NULL, 0, scrollh_num, vertices_scrollh);
	video_driver->scissor(video_data, 64, 16, 448, 240);
	scrollh_num = 0;
}

void blit_draw_scroll3h(int16_t x, int16_t y, uint32_t code, uint16_t attr,
	uint16_t tpens)
{
	int16_t index = scrollh_get_sprite(MAKE_HIGH_KEY(code, attr));
	video_sprite_vertex_t *vertices;

	if (index < 0) {
		uint16_t filtered[16];
		const uint16_t *palette;
		if (scrollh_texture_num == SCROLL3H_TEXTURE_SIZE - 1) {
			cps1_scan_scroll3_foreground();
			scrollh_delete_sprite();
		}
		index = scrollh_insert_sprite(MAKE_HIGH_KEY(code, attr));
		if (index < 0) return;
		palette = cps_high_palette(attr, 96, tpens, filtered);
		cps_cache_direct_tile(index, 32, &gfx_scroll3[code << 9], 16, palette);
	}

	if (scrollh_num + 2 > SCROLLH_MAX_SPRITES * 2) return;
	vertices = &vertices_scrollh[scrollh_num];
	scrollh_num += 2;
	cps_set_sprite_vertices(vertices, x, y, index, 32, attr);
}

void blit_finish_scrollh(void)
{
	if (!scrollh_num) return;
	video_driver->uploadMem(video_data, TEXTURE_LAYER_SCROLLH);
	video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_SCROLLH,
		NULL, 0, scrollh_num, vertices_scrollh);
}

void blit_draw_stars(uint16_t stars_x, uint16_t stars_y, uint8_t *col,
	uint16_t *pal)
{
	video_point_vertex_t *vertex = vertices_stars;
	uint16_t offs;
	uint32_t stars_num = 0;

	if (!vertices_stars)
		return;
	for (offs = 0; offs < STARS_MAX_POINTS; offs++, col += 8) {
		if (*col != 0x0f) {
			vertex->x = (int16_t)((((offs >> 8) << 5) - stars_x +
				(*col & 0x1f)) & 0x1ff);
			vertex->y = (int16_t)(((offs & 0xff) - stars_y) & 0xff);
			vertex->z = 0;
			vertex->color = pal[(*col & 0xe0) >> 1];
			vertex++;
			stars_num++;
		}
	}

	if (stars_num)
		video_driver->blitPointVertices(video_data, stars_num, vertices_stars);
}
