/******************************************************************************

	psp_ui_draw.c

	PSP implementation of ui_draw_driver_t.
	Manages VRAM texture slots and delegates all rendering to video_driver.

******************************************************************************/

#include "common/ui_draw_driver.h"
#include "common/video_driver.h"
#include "common/video_geometry.h"
#include "psp/psp_video.h"

#include <malloc.h>
#include <pspgu.h>
#include <pspkernel.h>
#include <stdint.h>
#include <stdlib.h>


/******************************************************************************
	PSP driver data
******************************************************************************/

typedef struct psp_ui_data
{
	void *video_data;

	/* The proportional font is mutable scratch storage in system RAM.  The
	 * remaining static atlases keep the compact historical EDRAM layout. */
	uint16_t *tex_font;
	uint16_t *tex_smallfont;
	uint16_t *tex_boxshadow;
} psp_ui_data_t;

static psp_ui_data_t psp_ui;


#define PSP_UI_FONT_TEXTURE_HEIGHT 64
/* UI_TEXTURE_FONT is also reused by state.c as CPU-side thumbnail scratch.
 * Vertical CPS previews need 152 rows, so keep a small safety margin without
 * restoring the old fictitious 512-row texture allocation. */
#define PSP_UI_SCRATCH_ROWS 160


/******************************************************************************
	Helpers
******************************************************************************/

static uint16_t *texture16_addr(int x, int y)
{
	return (uint16_t *)(0x44000000 + ((x + (y << 9)) << 1));
}


/******************************************************************************
	Driver interface implementation
******************************************************************************/

/*------------------------------------------------------
	Init / Term
------------------------------------------------------*/

static void *psp_ui_draw_init(void *video_data)
{
	psp_ui.video_data = video_data;
	/* The old PSP path put tex_font at EDRAM row 2000 but advertised it to the
	 * GE as a 512x512 texture.  Only 48 rows remain at that address; current
	 * PPSSPP correctly exposes the resulting out-of-range sampling.  Keep the
	 * mutable scratch in normal RAM.  The GE only binds the first 64 rows for
	 * font rendering; extra rows preserve state.c's thumbnail-scratch contract. */
	psp_ui.tex_font     = (uint16_t *)memalign(64,
		BUF_WIDTH * PSP_UI_SCRATCH_ROWS * sizeof(uint16_t));
	if (!psp_ui.tex_font)
		return NULL;
	psp_ui.tex_smallfont = texture16_addr(0, PSP_UI_STATIC_EDRAM_ROW);
	psp_ui.tex_boxshadow = NULL;  /* Set later during upload */

	return &psp_ui;
}

static void psp_ui_draw_term(void *data)
{
	psp_ui_data_t *d = (psp_ui_data_t *)data;
	if (d && d->tex_font)
	{
		free(d->tex_font);
		d->tex_font = NULL;
	}
}

static void psp_ui_draw_getLogicalSize(void *data, int output_width, int output_height,
	int *logical_width, int *logical_height)
{
	(void)data;
	if (logical_width) *logical_width = output_width;
	if (logical_height) *logical_height = output_height;
}

static void psp_ui_draw_uploadTexture(void *data, int slot,
	const uint16_t *pixels, int w, int h, int pitch, int format, int source_tiled8x8)
{
	psp_ui_data_t *d = (psp_ui_data_t *)data;
	(void)pixels; (void)w; (void)h; (void)pitch; (void)format; (void)source_tiled8x8;

	/*
	 * On PSP, texture data is written directly to VRAM via getTextureBasePtr.
	 * This upload call is used to finalize/record metadata if needed.
	 * For boxshadow, we record the pointer since it's contiguous after smallfont.
	 */
	if (slot == UI_TEXTURE_BOXSHADOW)
	{
		d->tex_boxshadow = (uint16_t *)pixels;
	}
}

static void psp_ui_draw_clearTexture(void *data, int slot, int w, int h, int pitch)
{
	psp_ui_data_t *d = (psp_ui_data_t *)data;
	uint16_t *dst = NULL;
	int x, y;

	switch (slot)
	{
	case UI_TEXTURE_FONT:     dst = d->tex_font;     break;
	case UI_TEXTURE_SMALLFONT: dst = d->tex_smallfont; break;
	case UI_TEXTURE_BOXSHADOW: dst = d->tex_boxshadow; break;
	}

	if (dst)
	{
		for (y = 0; y < h; y++)
		{
			for (x = 0; x < w; x++)
				dst[x] = 0;
			dst += pitch;
		}
	}
}

static uint16_t *psp_ui_draw_getTextureBasePtr(void *data, int slot)
{
	psp_ui_data_t *d = (psp_ui_data_t *)data;

	switch (slot)
	{
	case UI_TEXTURE_FONT:      return d->tex_font;
	case UI_TEXTURE_SMALLFONT: return d->tex_smallfont;
	case UI_TEXTURE_BOXSHADOW: return d->tex_boxshadow;
	}
	return NULL;
}

static bool psp_ui_draw_prepareTextureDraw(void *data, int slot,
	int su, int sv, int sw, int sh, ui_texture_draw_t *draw)
{
	psp_ui_data_t *d = (psp_ui_data_t *)data;
	uint16_t *tex = NULL;
	int tex_format = GU_PSM_4444;
	int swizzled = GU_FALSE;
	int tex_width = BUF_WIDTH;
	int tex_height = PSP_UI_FONT_TEXTURE_HEIGHT;
	int tex_stride = BUF_WIDTH;

	(void)su; (void)sv; (void)sw; (void)sh;
	if (!draw)
		return false;

	switch (slot)
	{
	case UI_TEXTURE_FONT:
		tex = d->tex_font;
		tex_format = GU_PSM_4444;
		swizzled = GU_FALSE;
		tex_height = PSP_UI_FONT_TEXTURE_HEIGHT;
		break;
	case UI_TEXTURE_SMALLFONT:
		tex = d->tex_smallfont;
		tex_format = GU_PSM_5551;
		swizzled = GU_TRUE;
		tex_height = 16;
		break;
	case UI_TEXTURE_BOXSHADOW:
		tex = d->tex_boxshadow;
		tex_format = GU_PSM_4444;
		swizzled = GU_TRUE;
		tex_width = 128;
		tex_height = 8;
		tex_stride = 128;
		break;
	}

	if (slot == UI_TEXTURE_FONT)
	{
		sceKernelDcacheWritebackRange(tex, BUF_WIDTH * 48 * sizeof(uint16_t));
		sceGuTexFlush();
	}

	if (!tex)
		return false;

	draw->texture = tex;
	draw->format = tex_format;
	draw->swizzled = swizzled;
	draw->width = tex_width;
	draw->height = tex_height;
	draw->stride = tex_stride;
	return true;
}

static void psp_ui_draw_finishTextureDraw(void *data, int slot)
{
	psp_ui_data_t *d = (psp_ui_data_t *)data;
	/* UI_TEXTURE_FONT is a single mutable scratch atlas.  Its pixels are
	 * overwritten immediately by the next proportional glyph, so the queued GE
	 * draw must consume them first.  Static UI atlases remain fully batched. */
	if (slot == UI_TEXTURE_FONT)
		psp_video_sync_ui_scratch(d->video_data);
}


/******************************************************************************
	PSP driver instance
******************************************************************************/

const ui_draw_driver_t psp_ui_draw_driver = {
	psp_ui_draw_init,
	psp_ui_draw_term,
	psp_ui_draw_getLogicalSize,
	UI_DRAW_CAP_CACHE_CHROME |
		UI_DRAW_CAP_TRANSLUCENT_CHROME |
		UI_DRAW_CAP_FILTERED_SHADOWS |
		UI_DRAW_CAP_ANIMATED_GLOW,
	psp_ui_draw_uploadTexture,
	psp_ui_draw_clearTexture,
	psp_ui_draw_getTextureBasePtr,
	psp_ui_draw_prepareTextureDraw,
	psp_ui_draw_finishTextureDraw,
	NULL,
};
