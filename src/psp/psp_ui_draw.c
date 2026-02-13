/******************************************************************************

	psp_ui_draw.c

	PSP implementation of ui_draw_driver_t.
	Manages VRAM texture slots and delegates all rendering to video_driver.

******************************************************************************/

#include "psp/psp.h"
#include "common/ui_draw_driver.h"


/******************************************************************************
	PSP driver data
******************************************************************************/

typedef struct psp_ui_data
{
	void *video_data;

	/* VRAM texture pointers */
	uint16_t *tex_font;
	uint16_t *tex_volicon;
	uint16_t *tex_smallfont;
	uint16_t *tex_boxshadow;
} psp_ui_data_t;

static psp_ui_data_t psp_ui;


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
	/* Allocate VRAM regions for UI textures */
	psp_ui.tex_font     = texture16_addr(0, 2000);
	psp_ui.tex_volicon  = texture16_addr(BUF_WIDTH - 112, 2000);
	psp_ui.tex_smallfont = texture16_addr(0, 2032);
	psp_ui.tex_boxshadow = NULL;  /* Set later during upload */

	return &psp_ui;
}

static void psp_ui_draw_term(void *data)
{
	(void)data;
}

static void psp_ui_draw_uploadTexture(void *data, int slot,
	const uint16_t *pixels, int w, int h, int pitch, int format, int swizzle)
{
	psp_ui_data_t *d = (psp_ui_data_t *)data;
	(void)pixels; (void)w; (void)h; (void)pitch; (void)format; (void)swizzle;

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
	case UI_TEXTURE_VOLICON:  dst = d->tex_volicon;  break;
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
	case UI_TEXTURE_VOLICON:   return d->tex_volicon;
	case UI_TEXTURE_BOXSHADOW: return d->tex_boxshadow;
	}
	return NULL;
}

static void psp_ui_draw_drawSprite(void *data, int slot,
	int su, int sv, int sw, int sh,
	int dx, int dy, int dw, int dh,
	uint32_t color, int blend)
{
	psp_ui_data_t *d = (psp_ui_data_t *)data;
	uint16_t *tex = NULL;
	int tex_format = GU_PSM_4444;
	int swizzled = GU_FALSE;

	(void)color;

	switch (slot)
	{
	case UI_TEXTURE_FONT:
		tex = d->tex_font;
		tex_format = GU_PSM_4444;
		swizzled = GU_FALSE;
		break;
	case UI_TEXTURE_SMALLFONT:
		tex = d->tex_smallfont;
		tex_format = GU_PSM_5551;
		swizzled = GU_TRUE;
		break;
	case UI_TEXTURE_BOXSHADOW:
		tex = d->tex_boxshadow;
		tex_format = GU_PSM_4444;
		swizzled = GU_TRUE;
		break;
	case UI_TEXTURE_VOLICON:
		tex = d->tex_volicon;
		tex_format = GU_PSM_4444;
		swizzled = GU_FALSE;
		break;
	}

	video_driver->drawUISprite(d->video_data, tex, tex_format, swizzled,
	                           su, sv, sw, sh, dx, dy, dw, dh, blend);
}

static void psp_ui_draw_drawLine(void *data,
	int x1, int y1, int x2, int y2,
	uint32_t color)
{
	psp_ui_data_t *d = (psp_ui_data_t *)data;
	video_driver->drawUILine(d->video_data, x1, y1, x2, y2, color);
}

static void psp_ui_draw_drawLineGradient(void *data,
	int x1, int y1, int x2, int y2,
	uint32_t color1, uint32_t color2)
{
	psp_ui_data_t *d = (psp_ui_data_t *)data;
	video_driver->drawUILineGradient(d->video_data, x1, y1, x2, y2, color1, color2);
}

static void psp_ui_draw_drawRect(void *data,
	int x, int y, int w, int h,
	uint32_t color)
{
	psp_ui_data_t *d = (psp_ui_data_t *)data;
	video_driver->drawUIRect(d->video_data, x, y, w, h, color);
}

static void psp_ui_draw_fillRect(void *data,
	int x, int y, int w, int h,
	uint32_t color)
{
	psp_ui_data_t *d = (psp_ui_data_t *)data;
	video_driver->fillUIRect(d->video_data, x, y, w, h, color);
}

static void psp_ui_draw_fillRectGradient(void *data,
	int x, int y, int w, int h,
	uint32_t color1, uint32_t color2,
	int direction)
{
	psp_ui_data_t *d = (psp_ui_data_t *)data;
	video_driver->fillUIRectGradient(d->video_data, x, y, w, h, color1, color2, direction);
}

static void psp_ui_draw_setScissor(void *data, int x, int y, int w, int h)
{
	(void)data;

	/*
	 * On PSP, scissor is set per-draw-call in the sceGu command list.
	 * This driver-level setScissor is a hint; the actual sceGuScissor
	 * calls happen inside the individual draw functions. For now this
	 * is a no-op since the PSP draw functions already manage scissor.
	 *
	 * TODO: If we want tight clipping (like small_font_print and
	 * draw_boxshadow), we need to cache the scissor rect and apply
	 * it in drawSprite. For now, drawSprite wraps its own sceGuScissor.
	 */
	(void)x; (void)y; (void)w; (void)h;
}


/******************************************************************************
	PSP driver instance
******************************************************************************/

const ui_draw_driver_t psp_ui_draw_driver = {
	psp_ui_draw_init,
	psp_ui_draw_term,
	psp_ui_draw_uploadTexture,
	psp_ui_draw_clearTexture,
	psp_ui_draw_getTextureBasePtr,
	psp_ui_draw_drawSprite,
	psp_ui_draw_drawLine,
	psp_ui_draw_drawLineGradient,
	psp_ui_draw_drawRect,
	psp_ui_draw_fillRect,
	psp_ui_draw_fillRectGradient,
	psp_ui_draw_setScissor,
};
