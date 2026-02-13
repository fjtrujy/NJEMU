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
#include "common/video_driver.h"

#define SCR_WIDTH 480
#define SCR_HEIGHT 272
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
	SDL_Texture *target_texture;  /* Temporary for rendering ops */
	
	/* 4 texture slots */
	desktop_ui_texture_t textures[UI_TEXTURE_MAX];
} desktop_ui_data_t;

/******************************************************************************
	Helpers — Color conversion
******************************************************************************/

/* Convert 16-bit RGBA4444 to SDL color (32-bit ARGB) */
static uint32_t rgba4444_to_sdl(uint16_t c)
{
	uint8_t r = ((c >> 12) & 0xF) * 17;  /* 0xF * 17 = 255 */
	uint8_t g = ((c >> 8) & 0xF) * 17;
	uint8_t b = ((c >> 4) & 0xF) * 17;
	uint8_t a = ((c & 0xF) * 17);
	return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Convert 16-bit RGBA5551 to SDL color (32-bit ARGB) */
static uint32_t rgba5551_to_sdl(uint16_t c)
{
	uint8_t r = ((c >> 11) & 0x1F) * 8;
	uint8_t g = ((c >> 6) & 0x1F) * 8;
	uint8_t b = ((c >> 1) & 0x1F) * 8;
	uint8_t a = (c & 1) ? 255 : 0;
	return (a << 24) | (r << 16) | (g << 8) | b;
}

/* Convert SDL color to 16-bit RGBA4444 */
static uint16_t sdl_to_rgba4444(uint32_t c)
{
	uint8_t a = (c >> 24) & 0xFF;
	uint8_t r = (c >> 16) & 0xFF;
	uint8_t g = (c >> 8) & 0xFF;
	uint8_t b = c & 0xFF;
	return ((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | (a >> 4);
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
	d->textures[UI_TEXTURE_VOLICON].format = UI_PIXFMT_4444;

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

/* Helper: Update SDL_Texture from buffer if needed */
static void update_sdl_texture(desktop_ui_data_t *d, desktop_ui_texture_t *tex)
{
	if (tex->sdl_tex_valid) return;

	if (tex->sdl_tex)
		SDL_DestroyTexture(tex->sdl_tex);

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

static void desktop_ui_draw_drawSprite(void *data, int slot,
	int su, int sv, int sw, int sh,
	int dx, int dy, int dw, int dh,
	uint32_t color, int blend)
{
	desktop_ui_data_t *d = (desktop_ui_data_t *)data;
	desktop_ui_texture_t *tex;
	SDL_Rect src_rect, dst_rect;
	SDL_Renderer *renderer = get_renderer(d);

	if (slot >= UI_TEXTURE_MAX || !renderer) return;
	tex = &d->textures[slot];

	update_sdl_texture(d, tex);
	if (!tex->sdl_tex) return;

	src_rect.x = su;
	src_rect.y = sv;
	src_rect.w = sw;
	src_rect.h = sh;

	dst_rect.x = dx;
	dst_rect.y = dy;
	dst_rect.w = dw;
	dst_rect.h = dh;

	if (blend) {
		SDL_SetTextureBlendMode(tex->sdl_tex, SDL_BLENDMODE_BLEND);
	} else {
		SDL_SetTextureBlendMode(tex->sdl_tex, SDL_BLENDMODE_NONE);
	}

	/* Apply color tint if not full white */
	if (color != 0xFFFFFFFF) {
		uint8_t a = (color >> 24) & 0xFF;
		uint8_t r = (color >> 16) & 0xFF;
		uint8_t g = (color >> 8) & 0xFF;
		uint8_t b = color & 0xFF;
		SDL_SetTextureColorMod(tex->sdl_tex, r, g, b);
		SDL_SetTextureAlphaMod(tex->sdl_tex, a);
	}

	SDL_RenderCopy(renderer, tex->sdl_tex, &src_rect, &dst_rect);
}

static void desktop_ui_draw_drawLine(void *data,
	int x1, int y1, int x2, int y2,
	uint32_t color)
{
	desktop_ui_data_t *d = (desktop_ui_data_t *)data;
	SDL_Renderer *renderer = get_renderer(d);
	SDL_Point points[2] = { {x1, y1}, {x2, y2} };

	if (!renderer) return;

	uint8_t r = (color >> 16) & 0xFF;
	uint8_t g = (color >> 8) & 0xFF;
	uint8_t b = color & 0xFF;
	uint8_t a = (color >> 24) & 0xFF;

	SDL_SetRenderDrawColor(renderer, r, g, b, a);
	SDL_RenderDrawLines(renderer, points, 2);
}

static void desktop_ui_draw_drawLineGradient(void *data,
	int x1, int y1, int x2, int y2,
	uint32_t color1, uint32_t color2)
{
	/* For gradient lines, draw intermediate pixels with interpolated colors */
	desktop_ui_data_t *d = (desktop_ui_data_t *)data;
	SDL_Renderer *renderer = get_renderer(d);

	if (!renderer) return;

	int dx = x2 - x1;
	int dy = y2 - y1;
	int steps = (dx > 0 ? dx : -dx) > (dy > 0 ? dy : -dy) ?
	            (dx > 0 ? dx : -dx) : (dy > 0 ? dy : -dy);

	if (steps == 0) return;

	uint8_t r1 = (color1 >> 16) & 0xFF;
	uint8_t g1 = (color1 >> 8) & 0xFF;
	uint8_t b1 = color1 & 0xFF;
	uint8_t a1 = (color1 >> 24) & 0xFF;

	uint8_t r2 = (color2 >> 16) & 0xFF;
	uint8_t g2 = (color2 >> 8) & 0xFF;
	uint8_t b2 = color2 & 0xFF;
	uint8_t a2 = (color2 >> 24) & 0xFF;

	for (int i = 0; i <= steps; i++) {
		float t = (float)i / steps;
		int x = x1 + (int)(dx * t);
		int y = y1 + (int)(dy * t);

		uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
		uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
		uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);
		uint8_t a = (uint8_t)(a1 + (a2 - a1) * t);

			SDL_SetRenderDrawColor(renderer, r, g, b, a);
			SDL_RenderDrawPoint(renderer, x, y);
	}
}

static void desktop_ui_draw_drawRect(void *data,
	int x, int y, int w, int h,
	uint32_t color)
{
	desktop_ui_data_t *d = (desktop_ui_data_t *)data;
	SDL_Renderer *renderer = get_renderer(d);
	SDL_Rect rect = {x, y, w, h};

	if (!renderer) return;

	uint8_t r = (color >> 16) & 0xFF;
	uint8_t g = (color >> 8) & 0xFF;
	uint8_t b = color & 0xFF;
	uint8_t a = (color >> 24) & 0xFF;

	SDL_SetRenderDrawColor(renderer, r, g, b, a);
	SDL_RenderDrawRect(renderer, &rect);
}

static void desktop_ui_draw_fillRect(void *data,
	int x, int y, int w, int h,
	uint32_t color)
{
	desktop_ui_data_t *d = (desktop_ui_data_t *)data;
	SDL_Renderer *renderer = get_renderer(d);
	SDL_Rect rect = {x, y, w, h};

	if (!renderer) return;

	uint8_t r = (color >> 16) & 0xFF;
	uint8_t g = (color >> 8) & 0xFF;
	uint8_t b = color & 0xFF;
	uint8_t a = (color >> 24) & 0xFF;

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, r, g, b, a);
	SDL_RenderFillRect(renderer, &rect);
}

static void desktop_ui_draw_fillRectGradient(void *data,
	int x, int y, int w, int h,
	uint32_t color1, uint32_t color2,
	int direction)
{
	desktop_ui_data_t *d = (desktop_ui_data_t *)data;
	SDL_Renderer *renderer = get_renderer(d);

	if (!renderer) return;

	uint8_t r1 = (color1 >> 16) & 0xFF;
	uint8_t g1 = (color1 >> 8) & 0xFF;
	uint8_t b1 = color1 & 0xFF;
	uint8_t a1 = (color1 >> 24) & 0xFF;

	uint8_t r2 = (color2 >> 16) & 0xFF;
	uint8_t g2 = (color2 >> 8) & 0xFF;
	uint8_t b2 = color2 & 0xFF;
	uint8_t a2 = (color2 >> 24) & 0xFF;

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	if (direction == UI_GRADIENT_HORIZONTAL) {
		for (int i = 0; i < w; i++) {
			float t = (float)i / (w - 1);
			uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
			uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
			uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);
			uint8_t a = (uint8_t)(a1 + (a2 - a1) * t);

			SDL_SetRenderDrawColor(renderer, r, g, b, a);
			SDL_RenderDrawLine(renderer, x + i, y, x + i, y + h - 1);
		}
	} else {
		for (int i = 0; i < h; i++) {
			float t = (float)i / (h - 1);
			uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
			uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
			uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);
			uint8_t a = (uint8_t)(a1 + (a2 - a1) * t);

			SDL_SetRenderDrawColor(renderer, r, g, b, a);
			SDL_RenderDrawLine(renderer, x, y + i, x + w - 1, y + i);
		}
	}
}

static void desktop_ui_draw_setScissor(void *data, int x, int y, int w, int h)
{
	desktop_ui_data_t *d = (desktop_ui_data_t *)data;
	SDL_Renderer *renderer = get_renderer(d);

	if (!renderer) return;

	SDL_Rect scissor = {x, y, w, h};
	SDL_RenderSetClipRect(renderer, &scissor);
}

/******************************************************************************
	Driver instance
******************************************************************************/

const ui_draw_driver_t desktop_ui_draw_driver = {
	desktop_ui_draw_init,
	desktop_ui_draw_term,
	desktop_ui_draw_uploadTexture,
	desktop_ui_draw_clearTexture,
	desktop_ui_draw_getTextureBasePtr,
	desktop_ui_draw_drawSprite,
	desktop_ui_draw_drawLine,
	desktop_ui_draw_drawLineGradient,
	desktop_ui_draw_drawRect,
	desktop_ui_draw_fillRect,
	desktop_ui_draw_fillRectGradient,
	desktop_ui_draw_setScissor,
};