/******************************************************************************

	ps2_ui_draw.c

	PS2 implementation of ui_draw_driver_t.
	Manages GS VRAM texture slots and delegates rendering to video_driver.

	Architecture:
	- Allocate 4 textures (512x512 each @ 16-bit) in GS VRAM via gsKit
	- Store pixel data in CPU RAM as staging buffers
	- Upload to VRAM on demand when texture changes
	- All rendering delegates to video_driver UI functions

******************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gsKit.h>
#include <dmaKit.h>
#include "ps2/ps2.h"
#include "common/ui_draw_driver.h"
#include "common/video_driver.h"


/******************************************************************************
	Texture management
******************************************************************************/

typedef struct ps2_ui_texture {
	GSTEXTURE texture;          /* GS texture object */
	uint16_t *buffer;           /* CPU-side staging buffer (16-bit) */
	int width, height;
	int pitch;                  /* Row stride in pixels */
	int format;                 /* UI_PIXFMT_4444 or UI_PIXFMT_5551 */
	int buffer_valid;           /* 1 if buffer has valid data */
	int vram_valid;             /* 1 if VRAM texture is up-to-date */
} ps2_ui_texture_t;

typedef struct ps2_ui_data {
	void *video_data;
	GSGLOBAL *gsGlobal;
	ps2_ui_texture_t textures[UI_TEXTURE_MAX];
} ps2_ui_data_t;

static ps2_ui_data_t ps2_ui;


/******************************************************************************
	Helpers
******************************************************************************/

/*
 * Convert 16-bit ARGB values between different formats
 */
static inline uint32_t rgba4444_to_gs(uint16_t c)
{
	/* RGBA4444: R in bits 12-15, G in 8-11, B in 4-7, A in 0-3 */
	int r = (c >> 12) & 0xF;
	int g = (c >> 8) & 0xF;
	int b = (c >> 4) & 0xF;
	int a = (c >> 0) & 0xF;
	/* Expand to 8-bit: shift left by 4 */
	return GS_SETREG_RGBA(r << 4, g << 4, b << 4, a << 4);
}

static inline uint32_t rgba5551_to_gs(uint16_t c)
{
	/* RGBA5551: R in bits 11-15, G in 6-10, B in 1-5, A in 0 */
	int r = (c >> 11) & 0x1F;
	int g = (c >> 6) & 0x1F;
	int b = (c >> 1) & 0x1F;
	int a = (c >> 0) & 0x01 ? 0xFF : 0x00;
	/* Expand to 8-bit: shift left by 3 */
	return GS_SETREG_RGBA(r << 3, g << 3, b << 3, a);
}

static inline uint32_t color_argb_to_gs(uint32_t argb)
{
	/* Input: 0xRRGGBBAA */
	int r = (argb >> 24) & 0xFF;
	int g = (argb >> 16) & 0xFF;
	int b = (argb >> 8) & 0xFF;
	int a = (argb >> 0) & 0xFF;
	return GS_SETREG_RGBA(r, g, b, a);
}


/******************************************************************************
	Driver interface implementation
******************************************************************************/

/*------------------------------------------------------
	Init / Term
------------------------------------------------------*/

static void *ps2_ui_draw_init(void *video_data)
{
	int i;

	memset(&ps2_ui, 0, sizeof(ps2_ui_data_t));
	ps2_ui.video_data = video_data;
	/* Note: gsGlobal will be set when needed via video_driver calls */
	ps2_ui.gsGlobal = NULL;  /* Set to NULL; get from video_driver context */

	/* Initialize texture slots */
	for (i = 0; i < UI_TEXTURE_MAX; i++)
	{
		ps2_ui_texture_t *tex = &ps2_ui.textures[i];
		tex->width = 512;
		tex->height = 512;
		tex->pitch = 512;
		tex->format = UI_PIXFMT_4444;
		tex->buffer_valid = 0;
		tex->vram_valid = 0;

		/* Allocate CPU-side staging buffer */
		tex->buffer = (uint16_t *)memalign(64, 512 * 512 * 2);
		if (!tex->buffer)
			return NULL;

		/* Initialize GSTEXTURE structure (VRAM allocation will happen on first upload) */
		memset(&tex->texture, 0, sizeof(GSTEXTURE));
		tex->texture.Width = 512;
		tex->texture.Height = 512;
		tex->texture.PSM = GS_PSM_T8;  /* Start with T8, will adjust for pixel data */
		tex->texture.TBW = 512 / 64;   /* Texture buffer width in 64-byte blocks */
	}

	return &ps2_ui;
}

static void ps2_ui_draw_term(void *data)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	int i;

	for (i = 0; i < UI_TEXTURE_MAX; i++)
	{
		if (d->textures[i].buffer)
			free(d->textures[i].buffer);
		/* VRAM cleanup is handled by gsKit */
	}
}

/*------------------------------------------------------
	Texture management
------------------------------------------------------*/

static void ps2_ui_draw_uploadTexture(void *data, int slot,
	const uint16_t *pixels, int w, int h, int pitch, int format, int swizzle)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	ps2_ui_texture_t *tex;
	int x, y;
	uint16_t *src, *dst;

	if (slot >= UI_TEXTURE_MAX)
		return;

	tex = &d->textures[slot];

	/* Record format info */
	tex->format = format;
	tex->width = w;
	tex->height = h;
	tex->pitch = pitch;

	/* Copy pixel data to staging buffer */
	if (pixels)
	{
		for (y = 0; y < h; y++)
		{
			src = (uint16_t *)pixels + y * pitch;
			dst = tex->buffer + y * pitch;
			memcpy(dst, src, w * sizeof(uint16_t));
		}
		tex->buffer_valid = 1;
		tex->vram_valid = 0;  /* Buffer modified, VRAM is stale */
	}
}

static void ps2_ui_draw_clearTexture(void *data, int slot, int w, int h, int pitch)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	ps2_ui_texture_t *tex;
	int x, y;

	if (slot >= UI_TEXTURE_MAX)
		return;

	tex = &d->textures[slot];

	/* Zero out texture buffer */
	for (y = 0; y < h; y++)
		memset(tex->buffer + y * pitch, 0, w * sizeof(uint16_t));

	tex->buffer_valid = 1;
	tex->vram_valid = 0;
}

static uint16_t *ps2_ui_draw_getTextureBasePtr(void *data, int slot)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;

	if (slot >= UI_TEXTURE_MAX)
		return NULL;

	return d->textures[slot].buffer;
}

/*------------------------------------------------------
	Drawing primitives (delegate to video_driver)
------------------------------------------------------*/

static void ps2_ui_draw_drawSprite(void *data, int slot,
	int su, int sv, int sw, int sh,
	int dx, int dy, int dw, int dh,
	uint32_t color, int blend)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;

	if (slot >= UI_TEXTURE_MAX)
		return;

	/* Ensure VRAM is up-to-date by uploading buffer if dirty */
	ps2_ui_texture_t *tex = &d->textures[slot];
	if (tex->buffer_valid && !tex->vram_valid)
	{
		/* TODO: Upload tex->buffer to GS VRAM via DMA */
		/* For now, just mark as valid to avoid repeated uploads */
		tex->vram_valid = 1;
	}

	/* Delegate to video_driver for actual rendering */
	video_driver->drawUISprite(d->video_data, tex->buffer,
		tex->format, 0, /* swizzled flag */
		su, sv, sw, sh,
		dx, dy, dw, dh,
		blend);
}

static void ps2_ui_draw_drawLine(void *data,
	int x1, int y1, int x2, int y2,
	uint32_t color)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	uint32_t gs_color = color_argb_to_gs(color);

	/* Delegate to video_driver */
	video_driver->drawUILine(d->video_data, x1, y1, x2, y2, gs_color);
}

static void ps2_ui_draw_drawLineGradient(void *data,
	int x1, int y1, int x2, int y2,
	uint32_t color1, uint32_t color2)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	uint32_t gs_color1 = color_argb_to_gs(color1);
	uint32_t gs_color2 = color_argb_to_gs(color2);

	/* Delegate to video_driver */
	video_driver->drawUILineGradient(d->video_data, x1, y1, x2, y2, gs_color1, gs_color2);
}

static void ps2_ui_draw_drawRect(void *data,
	int x, int y, int w, int h,
	uint32_t color)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	uint32_t gs_color = color_argb_to_gs(color);

	/* Delegate to video_driver */
	video_driver->drawUIRect(d->video_data, x, y, w, h, gs_color);
}

static void ps2_ui_draw_fillRect(void *data,
	int x, int y, int w, int h,
	uint32_t color)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	uint32_t gs_color = color_argb_to_gs(color);

	/* Delegate to video_driver */
	video_driver->fillUIRect(d->video_data, x, y, w, h, gs_color);
}

static void ps2_ui_draw_fillRectGradient(void *data,
	int x, int y, int w, int h,
	uint32_t color1, uint32_t color2,
	int direction)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	uint32_t gs_color1 = color_argb_to_gs(color1);
	uint32_t gs_color2 = color_argb_to_gs(color2);

	/* Delegate to video_driver */
	video_driver->fillUIRectGradient(d->video_data, x, y, w, h, gs_color1, gs_color2, direction);
}

static void ps2_ui_draw_setScissor(void *data, int x, int y, int w, int h)
{
	(void)data; (void)x; (void)y; (void)w; (void)h;
	/* TODO: Implement scissor rectangle via GS SCISSOR register */
}


/******************************************************************************
	Driver instance
******************************************************************************/

const ui_draw_driver_t ps2_ui_draw_driver = {
	ps2_ui_draw_init,
	ps2_ui_draw_term,
	ps2_ui_draw_uploadTexture,
	ps2_ui_draw_clearTexture,
	ps2_ui_draw_getTextureBasePtr,
	ps2_ui_draw_drawSprite,
	ps2_ui_draw_drawLine,
	ps2_ui_draw_drawLineGradient,
	ps2_ui_draw_drawRect,
	ps2_ui_draw_fillRect,
	ps2_ui_draw_fillRectGradient,
	ps2_ui_draw_setScissor,
};
