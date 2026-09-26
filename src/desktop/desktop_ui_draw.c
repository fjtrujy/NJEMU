/******************************************************************************

	desktop_ui_draw.c

	Desktop (SDL2) implementation of ui_draw_driver_t.
	Manages CPU buffers for UI textures and renders using SDL2.

******************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "desktop/desktop.h"
#include "common/ui_draw_driver.h"
#include "common/ui_layout.h"
#include "common/video_driver.h"

#define UI_TEXTURE_SIZE 512

/*------------------------------------------------------
	Desktop UI driver data
------------------------------------------------------*/

typedef struct desktop_ui_texture {
	uint16_t *buffer;        /* CPU-side 16-bit RGBA4444/5551 buffer */
	int width, height;
	int pitch;               /* Row stride in pixels */
	int format;              /* UI_PIXFMT_* */
	SDL_Texture *sdl_tex;    /* Cached SDL texture (created on demand) */
	int sdl_tex_valid;       /* Whether sdl_tex is up-to-date */
} desktop_ui_texture_t;

typedef struct desktop_ui_data {
	desktop_video_t *video_data;  /* Cast to access SDL_Renderer */
	
	/* 4 texture slots */
	desktop_ui_texture_t textures[UI_TEXTURE_MAX];
} desktop_ui_data_t;

/******************************************************************************
	Helpers — Color conversion
******************************************************************************/

/* The codebase uses ABGR layout in 16-bit pixels (matching PSP / MAKECOL15
 * / MAKECOL32 in common/video_driver.h):
 *   4444: AAAA.BBBB.GGGG.RRRR  (alpha high, red low)
 *   5551: A.BBBBB.GGGGG.RRRRR  (alpha high, red low)
 * Earlier versions of this file mis-named it "rgba4444" and pulled red from
 * the high nibble, producing channel-swapped artifacts in the menu.
 */

/* Convert 16-bit ABGR4444 to SDL color (32-bit ARGB8888) */
static uint32_t rgba4444_to_sdl(uint16_t c)
{
	uint8_t a = ((c >> 12) & 0xF) * 17;
	uint8_t b = ((c >> 8)  & 0xF) * 17;
	uint8_t g = ((c >> 4)  & 0xF) * 17;
	uint8_t r =  (c        & 0xF) * 17;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Convert 16-bit ABGR1555 to SDL color (32-bit ARGB8888) */
static uint32_t rgba5551_to_sdl(uint16_t c)
{
	uint8_t a = (c & 0x8000) ? 255 : 0;
	uint8_t b = ((c >> 10) & 0x1F) * 8;
	uint8_t g = ((c >> 5)  & 0x1F) * 8;
	uint8_t r =  (c        & 0x1F) * 8;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

/** Get SDL_Renderer from video_data */
static SDL_Renderer *get_renderer(desktop_ui_data_t *d)
{
	if (!d || !d->video_data) return NULL;
	/* video_data is a pointer to desktop_video_t, which has renderer as second field */
	desktop_video_t *video = (desktop_video_t *)d->video_data;
	return video->renderer;
}

/******************************************************************************
	Driver interface implementation
******************************************************************************/

/*------------------------------------------------------
	Init / Term
------------------------------------------------------*/

static void *desktop_ui_draw_init(void *video_data)
{
	desktop_ui_data_t *d = (desktop_ui_data_t *)malloc(sizeof(desktop_ui_data_t));
	int i;

	if (!d) return NULL;

	memset(d, 0, sizeof(desktop_ui_data_t));
	d->video_data = (desktop_video_t *)video_data;

	/* Allocate buffers for each texture slot */
	for (i = 0; i < UI_TEXTURE_MAX; i++) {
		d->textures[i].buffer = (uint16_t *)calloc(UI_TEXTURE_SIZE * UI_TEXTURE_SIZE, sizeof(uint16_t));
		d->textures[i].sdl_tex = NULL;
		d->textures[i].sdl_tex_valid = 0;
		d->textures[i].width = UI_TEXTURE_SIZE;
		d->textures[i].height = UI_TEXTURE_SIZE;
		d->textures[i].pitch = UI_TEXTURE_SIZE;

		if (!d->textures[i].buffer) {
			/* Cleanup and fail */
			int j;
			for (j = 0; j < i; j++) {
				free(d->textures[j].buffer);
				if (d->textures[j].sdl_tex)
					SDL_DestroyTexture(d->textures[j].sdl_tex);
			}
			free(d);
			return NULL;
		}
	}

	/* Initialize format (can be overridden by uploadTexture) */
	d->textures[UI_TEXTURE_FONT].format = UI_PIXFMT_4444;
	d->textures[UI_TEXTURE_SMALLFONT].format = UI_PIXFMT_5551;
	d->textures[UI_TEXTURE_BOXSHADOW].format = UI_PIXFMT_4444;

	return d;
}

static void desktop_ui_draw_term(void *data)
{
	desktop_ui_data_t *d = (desktop_ui_data_t *)data;
	int i;

	if (!d) return;

	for (i = 0; i < UI_TEXTURE_MAX; i++) {
		if (d->textures[i].buffer)
			free(d->textures[i].buffer);
		if (d->textures[i].sdl_tex)
			SDL_DestroyTexture(d->textures[i].sdl_tex);
	}

	free(d);
}

static void desktop_ui_draw_getOutputSize(void *data, int *width, int *height)
{
	desktop_ui_data_t *d = (desktop_ui_data_t *)data;
	SDL_Renderer *renderer = get_renderer(d);
	int w = SCR_WIDTH;
	int h = SCR_HEIGHT;

	if (renderer)
		SDL_GetRendererOutputSize(renderer, &w, &h);

	if (width) *width = w;
	if (height) *height = h;
}

static void desktop_ui_draw_getLogicalSize(void *data, int output_width, int output_height,
	int *logical_width, int *logical_height)
{
	(void)data;
	ui_layout_compute_responsive_size(output_width, output_height,
		logical_width, logical_height);
}

/*------------------------------------------------------
	Texture management
------------------------------------------------------*/

static void desktop_ui_draw_uploadTexture(void *data, int slot,
	const uint16_t *pixels, int w, int h, int pitch, int format, int swizzle)
{
	desktop_ui_data_t *d = (desktop_ui_data_t *)data;
	desktop_ui_texture_t *tex;
	int y;

	if (slot >= UI_TEXTURE_MAX) return;
	tex = &d->textures[slot];

	tex->format = format;
	tex->width = w;
	tex->height = h;
	tex->pitch = pitch;

	/* Copy pixel data to CPU buffer */
	if (pixels) {
		const uint16_t *src = pixels;
		uint16_t *dst = tex->buffer;
		for (y = 0; y < h; y++) {
			memcpy(dst, src, w * sizeof(uint16_t));
			src += pitch;
			dst += tex->pitch;
		}
	}

	/* Mark SDL texture as invalid (will recreate on next draw) */
	tex->sdl_tex_valid = 0;
	(void)swizzle;  /* No swizzling needed on desktop */
}

static void desktop_ui_draw_clearTexture(void *data, int slot, int w, int h, int pitch)
{
	desktop_ui_data_t *d = (desktop_ui_data_t *)data;
	desktop_ui_texture_t *tex;
	uint16_t *dst;
	int x, y;

	if (slot >= UI_TEXTURE_MAX) return;
	tex = &d->textures[slot];

	dst = tex->buffer;
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++)
			dst[x] = 0;
		dst += pitch;
	}

	tex->sdl_tex_valid = 0;
}

static uint16_t *desktop_ui_draw_getTextureBasePtr(void *data, int slot)
{
	desktop_ui_data_t *d = (desktop_ui_data_t *)data;

	if (slot >= UI_TEXTURE_MAX) return NULL;
	return d->textures[slot].buffer;
}

/*------------------------------------------------------
	Drawing primitives
------------------------------------------------------*/

/* Helper: Update SDL_Texture from buffer.
 *
 * The buffer can be modified by callers via getTextureBasePtr (see
 * make_font_texture in common/ui_draw.c) without going through
 * uploadTexture, so we cannot trust the sdl_tex_valid flag for fonts.
 * Instead we (re)create the SDL_Texture every call. Performance is fine
 * for the menu because draws happen at UI rates, not game rates.
 */
static void update_sdl_texture(desktop_ui_data_t *d, desktop_ui_texture_t *tex)
{
	if (tex->sdl_tex)
		SDL_DestroyTexture(tex->sdl_tex);
	tex->sdl_tex = NULL;
	tex->sdl_tex_valid = 0;

	/* Create SDL surface from buffer */
	SDL_Surface *surf = SDL_CreateRGBSurface(0, tex->width, tex->height, 32,
	                                           0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
	if (!surf) return;

	/* Convert pixel data to ARGB8888 */
	uint32_t *dst = (uint32_t *)surf->pixels;
	int x, y;

	if (tex->format == UI_PIXFMT_4444) {
		for (y = 0; y < tex->height; y++) {
			uint16_t *src = tex->buffer + y * tex->pitch;
			for (x = 0; x < tex->width; x++) {
				dst[y * tex->width + x] = rgba4444_to_sdl(src[x]);
			}
		}
	} else if (tex->format == UI_PIXFMT_5551) {
		for (y = 0; y < tex->height; y++) {
			uint16_t *src = tex->buffer + y * tex->pitch;
			for (x = 0; x < tex->width; x++) {
				dst[y * tex->width + x] = rgba5551_to_sdl(src[x]);
			}
		}
	}

	/* Create SDL_Texture from surface */
	SDL_Renderer *renderer = get_renderer(d);
	if (renderer)
		tex->sdl_tex = SDL_CreateTextureFromSurface(renderer, surf);
	SDL_FreeSurface(surf);
	if (tex->sdl_tex)
		tex->sdl_tex_valid = 1;
}

static bool desktop_ui_draw_prepareTextureDraw(void *data, int slot,
	int su, int sv, int sw, int sh, ui_texture_draw_t *draw)
{
	desktop_ui_data_t *d = (desktop_ui_data_t *)data;
	desktop_ui_texture_t *tex;

	(void)su; (void)sv; (void)sw; (void)sh;
	if (!draw || slot < 0 || slot >= UI_TEXTURE_MAX)
		return false;

	tex = &d->textures[slot];
	update_sdl_texture(d, tex);
	if (!tex->sdl_tex)
		return false;

	draw->texture = tex->sdl_tex;
	draw->format = tex->format;
	draw->swizzled = 0;
	draw->width = tex->width;
	draw->height = tex->height;
	draw->stride = tex->pitch;
	return true;
}

static void desktop_ui_draw_finishTextureDraw(void *data, int slot)
{
	(void)data;
	(void)slot;
}

/******************************************************************************
	Driver instance
******************************************************************************/

const ui_draw_driver_t desktop_ui_draw_driver = {
	desktop_ui_draw_init,
	desktop_ui_draw_term,
	desktop_ui_draw_getOutputSize,
	desktop_ui_draw_getLogicalSize,
	UI_DRAW_CAP_TRANSLUCENT_CHROME |
		UI_DRAW_CAP_FILTERED_SHADOWS |
		UI_DRAW_CAP_ANIMATED_GLOW,
	desktop_ui_draw_uploadTexture,
	desktop_ui_draw_clearTexture,
	desktop_ui_draw_getTextureBasePtr,
	desktop_ui_draw_prepareTextureDraw,
	desktop_ui_draw_finishTextureDraw,
};
