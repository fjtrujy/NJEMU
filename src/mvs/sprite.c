/******************************************************************************

	sprite.c

	MVS platform-neutral sprite renderer

******************************************************************************/

#include "mvs.h"
#include "common/cache.h"
#include "sprite_common.h"
#include "common/emulator_options.h"
#include "common/video_driver.h"
#include "common/video_geometry.h"
#include "mvs/wide_debug.h"
#if defined(DESKTOP)
#include "common/runtime_paths.h"
#include "mvs/wide_profile.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

/******************************************************************************
	Renderer state
******************************************************************************/

typedef struct mvs_presentation_size
{
	int16_t width;
	int16_t height;
} mvs_presentation_size_t;

#define MVS_STRETCH_16_9_OPTION	5

static bool mvs_wide_active;
#if defined(DESKTOP)
static bool mvs_wide_supported;
static bool mvs_wide_warning_emitted;
static mvs_wide_profile_t *mvs_wide_profile;
static char mvs_wide_error[192];
#endif

/* option_stretch semantics shared with common/menu/mvs.c.  The backend owns
 * physical output size; non-OFF presets scale from NJEMU's 480x272 logical
 * presentation space and are centered in that output. */
static const mvs_presentation_size_t mvs_presentation_sizes[6] =
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
static bool spr_disabled;
static uint16_t *clut;

static int mvs_true_wide_forced(void)
{
#if defined(DESKTOP)
	static int forced = -1;

	if (forced < 0)
	{
		const char *value = getenv("NJEMU_MVS_TRUE_WIDE");
		forced = value != NULL && value[0] != '\0' && value[0] != '0';
	}
	return forced;
#else
	return 0;
#endif
}

int mvs_true_wide_enabled(void)
{
	return mvs_wide_active;
}

void mvs_wide_exit(void)
{
#if defined(DESKTOP)
	free(mvs_wide_profile);
	mvs_wide_profile = NULL;
	mvs_wide_supported = false;
#endif
	mvs_wide_active = false;
}

void mvs_wide_init(void)
{
	mvs_wide_exit();
#if defined(DESKTOP)
	char path[PATH_MAX];
	const char *directory = getenv("NJEMU_MVS_PROFILE_DIR");
	const char *experimental = getenv("NJEMU_MVS_ALLOW_EXPERIMENTAL");
	int length, program_mode;
	mvs_wide_warning_emitted = false;
#if !RELEASE
	mvs_wide_debug_init();
#endif
	if (strspn(game_name, "abcdefghijklmnopqrstuvwxyz0123456789_-") != strlen(game_name))
	{
		snprintf(mvs_wide_error, sizeof(mvs_wide_error), "invalid game identifier");
		return;
	}
	length = directory && *directory ? snprintf(path, sizeof(path), "%s/%s.ini", directory, game_name) :
		snprintf(path, sizeof(path), "%swidescreen/%s.ini", launchDir, game_name);
	if (length < 0 || (size_t)length >= sizeof(path))
	{
		snprintf(mvs_wide_error, sizeof(mvs_wide_error), "profile path too long");
		return;
	}
	mvs_wide_profile = mvs_wide_profile_load(path, mvs_wide_error, sizeof(mvs_wide_error));
	if (!mvs_wide_profile) return;
	if (strcmp(mvs_wide_profile->game, game_name) || mvs_wide_profile->ngh != neogeo_ngh)
	{
		snprintf(mvs_wide_error, sizeof(mvs_wide_error), "profile belongs to a different game/NGH");
		return;
	}
	if (mvs_wide_profile->status == MVS_WIDE_PROFILE_DRAFT ||
		(mvs_wide_profile->status == MVS_WIDE_PROFILE_EXPERIMENTAL &&
		(!experimental || strcmp(experimental, "1"))))
	{
		snprintf(mvs_wide_error, sizeof(mvs_wide_error), "draft or experimental profile not authorized");
		return;
	}
	program_mode = mvs_wide_profile_check(mvs_wide_profile,
		(const uint16_t *)memory_region_cpu1, memory_length_cpu1,
		mvs_wide_error, sizeof(mvs_wide_error));
	mvs_wide_supported = program_mode == 0;
	if (program_mode == 1)
		snprintf(mvs_wide_error, sizeof(mvs_wide_error), "program was already patched before profile initialization");
#endif
}

void mvs_wide_update(void)
{
#if defined(DESKTOP)
	bool requested;

	requested = option_stretch == MVS_STRETCH_16_9_OPTION ||
		mvs_true_wide_forced();
	if (requested == mvs_wide_active || mvs_wide_warning_emitted)
		return;
	if (!mvs_wide_supported ||
		!mvs_wide_profile_apply(mvs_wide_profile, (uint16_t *)memory_region_cpu1,
			memory_length_cpu1, requested, mvs_wide_error, sizeof(mvs_wide_error)))
	{
		printf("[MVS_WIDE] %s: %s; mode change not applied.\n", game_name, mvs_wide_error);
		mvs_wide_warning_emitted = true;
		return;
	}
	mvs_wide_active = requested;
	printf("[MVS_WIDE] %s: %s; profile %s%s.\n", game_name,
		requested ? "400x225 viewport" : "Native viewport",
		requested ? "enabled" : "restored",
		mvs_wide_profile->viewport_only ? " (viewport only)" : "");
#endif
}

const mvs_view_geometry_t *mvs_get_view_geometry(void)
{
	return mvs_view_geometry_for_mode(mvs_true_wide_enabled());
}

static RECT mvs_presentation_rect(void)
{
	int option = mvs_true_wide_enabled() ?
		MVS_STRETCH_16_9_OPTION : option_stretch;
	int output_width = SCR_WIDTH;
	int output_height = SCR_HEIGHT;
	int width;
	int height;
	RECT rect;

	if (option < 0 || option >= (int)(sizeof(mvs_presentation_sizes) /
			sizeof(mvs_presentation_sizes[0])))
		option = 0;
	if (video_driver->getOutputSize)
		video_driver->getOutputSize(video_data, &output_width, &output_height);

	width = mvs_presentation_sizes[option].width;
	height = mvs_presentation_sizes[option].height;
	if (mvs_true_wide_enabled())
		mvs_wide_fit_output(output_width, output_height, &width, &height);
	else if (option != 0)
		video_scale_logical_size(output_width, output_height,
			width, height, &width, &height);

	rect.left = (int16_t)((output_width - width) / 2);
	rect.top = (int16_t)((output_height - height) / 2);
	rect.right = (int16_t)(rect.left + width);
	rect.bottom = (int16_t)(rect.top + height);
	return rect;
}

static void mvs_decode_fix_tile(uint8_t pixels[8 * 8], uint32_t code,
	uint16_t attr)
{
	const uint8_t *src = &fix_memory[code << 5];
	uint32_t palette = sprite_color_table[attr];
	int line;

	for (line = 0; line < 8; line++) {
		uint32_t tile = *(const uint32_t *)src;
		uint32_t *dst = (uint32_t *)&pixels[line * 8];
		dst[0] = ((tile >> 0) & 0x0f0f0f0f) | palette;
		dst[1] = ((tile >> 4) & 0x0f0f0f0f) | palette;
		src += 4;
	}
}

static void mvs_decode_sprite_tile(uint8_t pixels[16 * 16], uint32_t code,
	uint16_t attr)
{
	uint32_t gfx3_offset = read_cache ? read_cache(code << 7) : code << 7;
	const uint8_t *src = &memory_region_gfx3[gfx3_offset];
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

	clut = (uint16_t *)&video_palettebank[palette_bank];
	video_driver->uploadClut(video_data, clut, palette_bank);
	tex_fix_changed = false;
	blit_clear_all_sprite();
}

void blit_start(int start, int end)
{
	const mvs_view_geometry_t *view = mvs_get_view_geometry();

	spr_vertex_count = 0;
	spr_count = 0;

	if (start == FIRST_VISIBLE_LINE) {
		clut = (uint16_t *)&video_palettebank[palette_bank];
		video_driver->uploadClut(video_data, clut, palette_bank);
		fix_vertex_count = 0;
		spr_disabled = false;

		if (clear_spr_texture) blit_clear_spr_sprite();
		if (clear_fix_texture) blit_clear_fix_sprite();

		video_driver->beginFrame(video_data);
		video_driver->startWorkFrame(video_data,
			CNVCOL15TO32(video_palette[4095]));
		video_driver->scissor(video_data,
			(uint16_t)view->render_left, (uint16_t)view->render_top,
			(uint16_t)view->render_right, (uint16_t)view->render_bottom);
	}
}

void blit_finish(void)
{
	const mvs_view_geometry_t *view = mvs_get_view_geometry();
	RECT src_clip = {
		(int16_t)view->source_left,
		(int16_t)view->source_top,
		(int16_t)view->source_right,
		(int16_t)view->source_bottom
	};
	RECT dst_clip = mvs_presentation_rect();
	video_driver->transferWorkFrame(video_data, &src_clip, &dst_clip);
	video_driver->endFrame(video_data);
#if defined(DESKTOP) && !RELEASE
	mvs_wide_debug_frame();
#endif
}

void blit_draw_fix(int x, int y, uint32_t code, uint16_t attr)
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
		mvs_decode_fix_tile(pixels, code, attr);
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

void blit_draw_spr(int x, int y, int w, int h, uint32_t code, uint16_t attr)
{
	int16_t idx = (int16_t)spr_get_sprite(MAKE_SPR_KEY(code, attr));
	video_sprite_vertex_t *vertices;
	uint16_t local_idx;

	if (spr_disabled) return;

	if (idx < 0) {
		uint8_t pixels[16 * 16] __attribute__((aligned(4)));
		uint8_t layer;
		int atlas_x, atlas_y;

		if (spr_texture_num == SPR_TEXTURE_SIZE - 1) {
			spr_delete_sprite();
			if (spr_texture_num == SPR_TEXTURE_SIZE - 1) {
				spr_disabled = true;
				return;
			}
		}
		idx = (int16_t)spr_insert_sprite(MAKE_SPR_KEY(code, attr));
		if (idx < 0) return;

		layer = (uint8_t)(TEXTURE_LAYER_SPR0 + (idx >> 10));
		local_idx = (uint16_t)(idx & 0x03ff);
		atlas_x = (local_idx % TILE_16x16_PER_LINE) * 16;
		atlas_y = (local_idx / TILE_16x16_PER_LINE) * 16;
		mvs_decode_sprite_tile(pixels, code, attr);
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
