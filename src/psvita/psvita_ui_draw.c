/******************************************************************************

	psvita_ui_draw.c

	PS Vita UI texture storage, shared by the GXM and vitaGL video backends.

	UI textures are plain linear CPU buffers of PSP-layout texels (4444 and
	5551 with alpha in the top bits). prepareTextureDraw() hands them to the
	video backend, which takes the texels a draw uses from its UI atlas (see
	psvita_video_common.h): the font slot is rewritten for every glyph, long
	before the GPU draws the previous ones.

******************************************************************************/

#include <stdlib.h>
#include <string.h>
#include "common/ui_draw_driver.h"
#include "common/ui_layout.h"
#include "common/video_geometry.h"

#define FONT_HEIGHT			64		/* glyphs, shadows and the logo: up to 48 rows */
#define SMALLFONT_HEIGHT	16
#define BOXSHADOW_WIDTH		72
#define BOXSHADOW_HEIGHT	8

typedef struct psvita_ui_texture {
	uint16_t *pixels;			/* linear, `pitch` texels per row */
	int width;
	int height;
	int pitch;
	int format;
} psvita_ui_texture_t;

typedef struct psvita_ui_draw {
	psvita_ui_texture_t slots[UI_TEXTURE_MAX];
	/* SMALLFONT/BOXSHADOW are written tile by tile, then uploaded swizzled. */
	uint16_t *staging;
} psvita_ui_draw_t;

static bool alloc_slot(psvita_ui_texture_t *slot, int width, int height, int format)
{
	slot->pixels = calloc((size_t)width * height, sizeof(uint16_t));
	slot->width = width;
	slot->height = height;
	slot->pitch = width;
	slot->format = format;
	return slot->pixels != NULL;
}

static void psvita_ui_draw_term(void *data)
{
	psvita_ui_draw_t *ui = data;

	if (ui == NULL)
		return;
	for (int i = 0; i < UI_TEXTURE_MAX; i++)
		free(ui->slots[i].pixels);
	free(ui->staging);
	free(ui);
}

static void *psvita_ui_draw_init(void *video_data)
{
	psvita_ui_draw_t *ui = calloc(1, sizeof(*ui));
	(void)video_data;

	if (ui == NULL)
		return NULL;
	if (!alloc_slot(&ui->slots[UI_TEXTURE_FONT], BUF_WIDTH, FONT_HEIGHT, UI_PIXFMT_4444)
		|| !alloc_slot(&ui->slots[UI_TEXTURE_SMALLFONT], BUF_WIDTH, SMALLFONT_HEIGHT, UI_PIXFMT_5551)
		|| !alloc_slot(&ui->slots[UI_TEXTURE_BOXSHADOW], BOXSHADOW_WIDTH, BOXSHADOW_HEIGHT, UI_PIXFMT_4444)
		|| (ui->staging = calloc((size_t)BUF_WIDTH * SMALLFONT_HEIGHT, sizeof(uint16_t))) == NULL) {
		psvita_ui_draw_term(ui);
		return NULL;
	}
	return ui;
}

/* 480x272 scaled to the display, as on the PSP. */
static void psvita_ui_draw_getLogicalSize(void *data, int output_width, int output_height,
	int *logical_width, int *logical_height)
{
	(void)data;
	ui_layout_compute_responsive_size(output_width, output_height, logical_width, logical_height);
}

/*
 * Swizzled data uses the PSP 16-bit layout: 8x8-texel blocks (16 bytes by 8
 * rows) stored one after another, `w / 8` blocks per block row.
 */
static void psvita_ui_draw_uploadTexture(void *data, int slot, const uint16_t *pixels,
	int w, int h, int pitch, int format, int swizzle)
{
	psvita_ui_draw_t *ui = data;
	psvita_ui_texture_t *dst;

	if (slot < 0 || slot >= UI_TEXTURE_MAX || pixels == NULL || w <= 0 || h <= 0)
		return;
	dst = &ui->slots[slot];
	if (w > dst->width)
		w = dst->width;
	if (h > dst->height)
		h = dst->height;
	dst->format = format;

	if (!swizzle) {
		for (int y = 0; y < h; y++)
			memcpy(dst->pixels + (size_t)y * dst->pitch, pixels + (size_t)y * pitch,
				(size_t)w * sizeof(uint16_t));
		return;
	}

	const int blocks_per_row = pitch / 8;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const int block = (y / 8) * blocks_per_row + x / 8;
			dst->pixels[(size_t)y * dst->pitch + x] = pixels[block * 64 + (y % 8) * 8 + x % 8];
		}
	}
}

static void psvita_ui_draw_clearTexture(void *data, int slot, int w, int h, int pitch)
{
	psvita_ui_draw_t *ui = data;
	psvita_ui_texture_t *dst;
	(void)pitch;

	if (slot < 0 || slot >= UI_TEXTURE_MAX)
		return;
	dst = &ui->slots[slot];
	if (w > dst->width)
		w = dst->width;
	if (h > dst->height)
		h = dst->height;
	for (int y = 0; y < h; y++)
		memset(dst->pixels + (size_t)y * dst->pitch, 0, (size_t)w * sizeof(uint16_t));
}

static uint16_t *psvita_ui_draw_getTextureBasePtr(void *data, int slot)
{
	psvita_ui_draw_t *ui = data;

	/* The font is drawn from where glyphs are built; the others go through uploads. */
	if (slot == UI_TEXTURE_FONT)
		return ui->slots[UI_TEXTURE_FONT].pixels;
	if (slot == UI_TEXTURE_SMALLFONT || slot == UI_TEXTURE_BOXSHADOW)
		return ui->staging;
	return NULL;
}

static bool psvita_ui_draw_prepareTextureDraw(void *data, int slot,
	int su, int sv, int sw, int sh, ui_texture_draw_t *draw)
{
	psvita_ui_draw_t *ui = data;
	const psvita_ui_texture_t *tex;

	if (slot < 0 || slot >= UI_TEXTURE_MAX || draw == NULL)
		return false;
	tex = &ui->slots[slot];
	if (su < 0 || sv < 0 || su + sw > tex->width || sv + sh > tex->height)
		return false;

	draw->texture = tex->pixels;
	draw->format = tex->format;
	draw->swizzled = 0;
	draw->width = tex->width;
	draw->height = tex->height;
	draw->stride = tex->pitch;
	return true;
}

static void psvita_ui_draw_finishTextureDraw(void *data, int slot)
{
	/* The video backend already put the texels it needs in its UI atlas. */
	(void)data;
	(void)slot;
}

const ui_draw_driver_t psvita_ui_draw_driver = {
	psvita_ui_draw_init,
	psvita_ui_draw_term,
	psvita_ui_draw_getLogicalSize,
	UI_DRAW_CAP_TRANSLUCENT_CHROME |
	UI_DRAW_CAP_FILTERED_SHADOWS |
	UI_DRAW_CAP_ANIMATED_GLOW,
	psvita_ui_draw_uploadTexture,
	psvita_ui_draw_clearTexture,
	psvita_ui_draw_getTextureBasePtr,
	psvita_ui_draw_prepareTextureDraw,
	psvita_ui_draw_finishTextureDraw,
};
