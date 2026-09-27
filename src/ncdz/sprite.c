/******************************************************************************

	sprite.c

	NEOGEO CDZ platform-neutral sprite renderer

******************************************************************************/

#include "ncdz.h"
#include "sprite_common.h"
#include "common/emulator_options.h"
#include "common/video_driver.h"
#include "common/video_geometry.h"

/******************************************************************************
	Renderer state
******************************************************************************/

typedef struct ncdz_presentation_size
{
	int16_t width;
	int16_t height;
} ncdz_presentation_size_t;

static const RECT ncdz_src_clip = { 24, 16, 24 + 304, 16 + 224 };

/* option_stretch semantics shared with common/menu/ncdz.c.  The backend owns
 * physical output size; non-OFF presets scale from NJEMU's 480x272 logical
 * presentation space and are centered in that output. */
static const ncdz_presentation_size_t ncdz_presentation_sizes[6] =
{
	{ 304, 224 },	/* OFF/native 19:14 */
	{ 320, 240 },	/* 4:3 */
	{ 360, 270 },	/* 4:3 */
	{ 366, 270 },	/* 19:14 */
	{ 420, 270 },	/* 14:9 */
	{ 480, 270 }	/* 16:9 */
};

static bool tex_fix_changed;
static video_sprite_vertex_t __attribute__((aligned(64)))
	vertices_fix[FIX_MAX_SPRITES * 2];
static video_sprite_vertex_t __attribute__((aligned(64)))
	vertices_spr[SPR_MAX_SPRITES * 2];
static uint16_t ALIGN16_DATA spr_flags[SPR_MAX_SPRITES];
static uint16_t fix_vertex_count;
static uint16_t spr_vertex_count;
static uint16_t spr_count;
static uint16_t *clut;

static RECT ncdz_presentation_rect(void)
{
	int option = option_stretch;
	int output_width = SCR_WIDTH;
	int output_height = SCR_HEIGHT;
	int width;
	int height;
	RECT rect;

	if (option < 0 || option >= (int)(sizeof(ncdz_presentation_sizes) /
			sizeof(ncdz_presentation_sizes[0])))
		option = 0;
	if (video_driver->getOutputSize)
		video_driver->getOutputSize(video_data, &output_width, &output_height);

	width = ncdz_presentation_sizes[option].width;
	height = ncdz_presentation_sizes[option].height;
	if (option != 0)
		video_scale_logical_size(output_width, output_height,
			width, height, &width, &height);

	rect.left = (int16_t)((output_width - width) / 2);
	rect.top = (int16_t)((output_height - height) / 2);
	rect.right = (int16_t)(rect.left + width);
	rect.bottom = (int16_t)(rect.top + height);
	return rect;
}

static void ncdz_decode_fix_tile(uint8_t pixels[8 * 8], uint32_t code,
	uint32_t attr)
{
	const uint8_t *src = &memory_region_gfx1[code << 5];
	uint32_t palette = sprite_color_table[attr];
	int line;

	for (line = 0; line < 8; line++) {
		uint32_t tile = *(const uint32_t *)src;
		uint32_t *dst = (uint32_t *)&pixels[line * 8];
		dst[0] = ((tile & 0x0000000f) >>  0) |
			((tile & 0x000000f0) <<  4) |
			((tile & 0x00000f00) <<  8) |
			((tile & 0x0000f000) << 12) | palette;
		dst[1] = ((tile & 0x000f0000) >> 16) |
			((tile & 0x00f00000) >> 12) |
			((tile & 0x0f000000) >>  8) |
			((tile & 0xf0000000) >>  4) | palette;
		src += 4;
	}
}

static void ncdz_decode_sprite_tile(uint8_t pixels[16 * 16], uint32_t code,
	uint32_t attr)
{
	const uint8_t *src = &memory_region_gfx2[code << 7];
	uint32_t palette = sprite_color_table[(attr >> 8) & 0x0f];
	int line;

	for (line = 0; line < 16; line++) {
		uint32_t *dst = (uint32_t *)&pixels[line * 16];
		uint32_t tile = *(const uint32_t *)(src + 0);
		dst[0] = ((tile >> 0) & 0x0f0f0f0f) | palette;
		dst[1] = ((tile >> 4) & 0x0f0f0f0f) | palette;
		tile = *(const uint32_t *)(src + 4);
		dst[2] = ((tile >> 0) & 0x0f0f0f0f) | palette;
		dst[3] = ((tile >> 4) & 0x0f0f0f0f) | palette;
		src += 8;
	}
}

/******************************************************************************
	Sprite drawing interface
******************************************************************************/

void blit_reset(void)
{
	int i;

	for (i = 0; i < FIX_TEXTURE_SIZE; i++) fix_data[i].index = i;
	for (i = 0; i < SPR_TEXTURE_SIZE; i++) spr_data[i].index = i;

	clip_min_y = FIRST_VISIBLE_LINE;
	clip_max_y = LAST_VISIBLE_LINE;
	clut = (uint16_t *)&video_palettebank[palette_bank];
	video_driver->uploadClut(video_data, clut, palette_bank);
	tex_fix_changed = false;
	blit_clear_all_sprite();
}

void blit_start(int start, int end)
{
	clip_min_y = start;
	clip_max_y = end + 1;
	spr_vertex_count = 0;
	spr_count = 0;

	if (start == FIRST_VISIBLE_LINE) {
		clut = (uint16_t *)&video_palettebank[palette_bank];
		video_driver->uploadClut(video_data, clut, palette_bank);
		fix_vertex_count = 0;

		if (clear_spr_texture) blit_clear_spr_sprite();
		if (clear_fix_texture) blit_clear_fix_sprite();

		video_driver->beginFrame(video_data);
		video_driver->startWorkFrame(video_data,
			CNVCOL15TO32(video_palette[4095]));
		video_driver->scissor(video_data, 24, 16, 336, 240);
	}
}

void blit_finish(void)
{
	RECT dst_clip = ncdz_presentation_rect();
	video_driver->transferWorkFrame(video_data, (RECT *)&ncdz_src_clip, &dst_clip);
	video_driver->endFrame(video_data);
}

void blit_draw_fix(int x, int y, uint32_t code, uint32_t attr)
{
	int16_t idx = (int16_t)fix_get_sprite(MAKE_FIX_KEY(code, attr));
	video_sprite_vertex_t *vertices;

	if (idx < 0) {
		uint8_t pixels[8 * 8] __attribute__((aligned(4)));
		int atlas_x, atlas_y;

		if (fix_texture_num == FIX_TEXTURE_SIZE - 1)
			fix_delete_sprite();
		idx = (int16_t)fix_insert_sprite(MAKE_FIX_KEY(code, attr));
		if (idx < 0) return;

		atlas_x = (idx % TILE_8x8_PER_LINE) * 8;
		atlas_y = (idx / TILE_8x8_PER_LINE) * 8;
		ncdz_decode_fix_tile(pixels, code, attr);
		video_driver->writeIndexedTextureRect(video_data, TEXTURE_LAYER_FIX,
			atlas_x, atlas_y, 8, 8, pixels, 8);
		tex_fix_changed = true;
	}

	if (fix_vertex_count + 2 > FIX_MAX_SPRITES * 2)
		return;
	vertices = &vertices_fix[fix_vertex_count];
	fix_vertex_count += 2;
	vertices[0].x = vertices[1].x = (int16_t)x;
	vertices[0].y = vertices[1].y = (int16_t)y;
	vertices[0].u = vertices[1].u = (uint16_t)((idx & 0x003f) << 3);
	vertices[0].v = vertices[1].v = (uint16_t)((idx & 0x0fc0) >> 3);
	vertices[0].color = vertices[1].color = 0;
	vertices[0].z = vertices[1].z = 0;
	vertices[1].x += 8;
	vertices[1].y += 8;
	vertices[1].u += 8;
	vertices[1].v += 8;
}

void blit_finish_fix(void)
{
	if (!fix_vertex_count) return;
	if (tex_fix_changed) {
		video_driver->uploadMem(video_data, TEXTURE_LAYER_FIX);
		tex_fix_changed = false;
	}
	video_driver->blitSpriteVertices(video_data, TEXTURE_LAYER_FIX,
		clut, palette_bank, fix_vertex_count, vertices_fix);
}

void blit_draw_spr(int x, int y, int w, int h, uint32_t code, uint32_t attr)
{
	int16_t idx = (int16_t)spr_get_sprite(MAKE_SPR_KEY(code, attr));
	video_sprite_vertex_t *vertices;
	uint16_t local_idx;

	if (idx < 0) {
		uint8_t pixels[16 * 16] __attribute__((aligned(4)));
		uint8_t layer;
		int atlas_x, atlas_y;

		if (spr_texture_num == SPR_TEXTURE_SIZE - 1)
			spr_delete_sprite();
		idx = (int16_t)spr_insert_sprite(MAKE_SPR_KEY(code, attr));
		if (idx < 0) return;

		layer = (uint8_t)(TEXTURE_LAYER_SPR0 + (idx >> 10));
		local_idx = (uint16_t)(idx & 0x03ff);
		atlas_x = (local_idx % TILE_16x16_PER_LINE) * 16;
		atlas_y = (local_idx / TILE_16x16_PER_LINE) * 16;
		ncdz_decode_sprite_tile(pixels, code, attr);
		video_driver->writeIndexedTextureRect(video_data, layer,
			atlas_x, atlas_y, 16, 16, pixels, 16);
	}

	if (spr_vertex_count + 2 > SPR_MAX_SPRITES * 2 || spr_count >= SPR_MAX_SPRITES)
		return;
	vertices = &vertices_spr[spr_vertex_count];
	spr_vertex_count += 2;
	spr_flags[spr_count++] = (uint16_t)((idx >> 10) | ((attr & 0xf000) >> 4));

	local_idx = (uint16_t)(idx & 0x03ff);
	vertices[0].x = vertices[1].x = (int16_t)x;
	vertices[0].y = vertices[1].y = (int16_t)y;
	vertices[0].u = vertices[1].u = (uint16_t)((local_idx & 0x001f) << 4);
	vertices[0].v = vertices[1].v = (uint16_t)((local_idx & 0x03e0) >> 1);
	vertices[0].color = vertices[1].color = 0;
	vertices[0].z = vertices[1].z = 0;

	attr ^= 0x03;
	vertices[(attr & 0x01) >> 0].u += 16;
	vertices[(attr & 0x02) >> 1].v += 16;
	vertices[1].x += (int16_t)w;
	vertices[1].y += (int16_t)h;
}

void blit_finish_spr(void)
{
	uint16_t flags;
	uint16_t *pflags = spr_flags;
	video_sprite_vertex_t *vertices = vertices_spr;
	video_sprite_vertex_t *vertices_tmp = vertices_spr;
	uint16_t *clut_tmp;
	uint8_t texture_layer;
	uint16_t total_vertices = 0;
	bool mem_uploaded[TEXTURE_LAYER_COUNT] = { false };
	uint16_t sprite;

	if (!spr_count) return;
	flags = *pflags;
	texture_layer = (uint8_t)(TEXTURE_LAYER_SPR0 + (flags & 3));
	clut_tmp = &clut[flags & 0xf00];
	mem_uploaded[texture_layer] = true;
	video_driver->uploadMem(video_data, texture_layer);

	for (sprite = 0; sprite < spr_count; sprite++) {
		if (flags != *pflags) {
			if (total_vertices) {
				video_driver->blitSpriteVertices(video_data, texture_layer,
					clut_tmp, palette_bank, total_vertices, vertices);
				total_vertices = 0;
				vertices = vertices_tmp;
			}

			flags = *pflags;
			texture_layer = (uint8_t)(TEXTURE_LAYER_SPR0 + (flags & 3));
			clut_tmp = &clut[flags & 0xf00];
			if (!mem_uploaded[texture_layer]) {
				mem_uploaded[texture_layer] = true;
				video_driver->uploadMem(video_data, texture_layer);
			}
		}

		vertices_tmp += 2;
		total_vertices += 2;
		pflags++;
	}

	if (total_vertices)
		video_driver->blitSpriteVertices(video_data, texture_layer,
			clut_tmp, palette_bank, total_vertices, vertices);
}
