/******************************************************************************

	video.c

	Desktop Video Control Functions

******************************************************************************/

#include "emucfg.h"
#include "common/video_driver.h"
#include "common/ui_draw_driver.h"
#include "common/video_geometry.h"
#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <SDL.h>
#include "desktop/desktop_frame_dump.h"
#include "desktop/desktop_video.h"

typedef struct texture_layer {
	SDL_Texture *texture;
	uint8_t *buffer;
	uint8_t bytes_per_pixel;
} texture_layer_t;

typedef struct desktop_video {
	SDL_Window *window;
	SDL_Renderer *renderer;
	bool draw_extra_info;
	SDL_BlendMode blendMode;
	uint16_t *clut_base;
	uint8_t *texturesMem;
	SDL_Texture *sdl_texture_scrbitmap;
	uint8_t *scrbitmap;
	uint16_t *ui_scratch;
	SDL_Texture *sdl_texture_ui_scratch;
	SDL_Texture *sdl_texture_ui_stream;
	uint32_t *ui_stream_pixels;
	texture_layer_t *tex_layers;
	uint8_t tex_layers_count;
	uint32_t presented_frames;	/* NJEMU_DUMP_FRAMES numbering */
	int vsync_enabled;
	int vsync_control_available;
} desktop_video_t;

#define OUTPUT_WIDTH 640
#define OUTPUT_HEIGHT 480
#define DESKTOP_UI_STREAM_WIDTH BUF_WIDTH
#define DESKTOP_UI_STREAM_HEIGHT 64

/******************************************************************************
	Global Functions
******************************************************************************/

static void *desktop_init(layer_texture_info_t *layer_textures, uint8_t layer_textures_count, clut_info_t *clut_info)
{
	uint32_t windows_width, windows_height;
	desktop_video_t *desktop = (desktop_video_t*)calloc(1, sizeof(desktop_video_t));
	desktop->draw_extra_info = false;
	desktop->clut_base = clut_info->base;

	// Create a window (width, height, window title)
	char title[256];
	sprintf(title, "%s %s", APPNAME_STR, VERSION_STR);
	windows_width = desktop->draw_extra_info ? BUF_WIDTH * 2 : OUTPUT_WIDTH;
	windows_height = desktop->draw_extra_info ? TEXTURE_HEIGHT * 2 : OUTPUT_HEIGHT;


    SDL_Window* window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windows_width, windows_height, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

	// Check that the window was successfully created
	if (window == NULL) {
		// In the case that the window could not be made...
		printf("Could not create window: %s\n", SDL_GetError());
		free(desktop);
		return NULL;
	}

	desktop->window = window;
	SDL_SetWindowMinimumSize(window, SCR_WIDTH, SCR_HEIGHT);

	// Create a renderer
	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

	// Check that the renderer was successfully created
	if (renderer == NULL) {
		// In the case that the renderer could not be made...
		printf("Could not create renderer: %s\n", SDL_GetError());
		SDL_DestroyWindow(desktop->window);
		free(desktop);
		return NULL;
	}

	desktop->renderer = renderer;
	desktop->vsync_enabled = 0;
#if SDL_VERSION_ATLEAST(2, 0, 18)
	desktop->vsync_control_available = 1;
#else
	desktop->vsync_control_available = 0;
#endif

	desktop->blendMode = SDL_ComposeCustomBlendMode(
		SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, 
		SDL_BLENDFACTOR_SRC_ALPHA, 
		SDL_BLENDOPERATION_ADD, 
		SDL_BLENDFACTOR_ZERO, 
		SDL_BLENDFACTOR_ZERO, 
		SDL_BLENDOPERATION_ADD
	);

	// Original buffers containing clut indexes
	size_t scrbitmapSize = BUF_WIDTH * SCR_HEIGHT * sizeof(uint16_t);
	desktop->scrbitmap = (uint8_t*)malloc(scrbitmapSize);
	if (desktop->scrbitmap == NULL) {
		printf("Could not allocate scrbitmap buffer\n");
		SDL_DestroyRenderer(desktop->renderer);
		SDL_DestroyWindow(desktop->window);
		free(desktop);
		return NULL;
	}
	memset(desktop->scrbitmap, 0, scrbitmapSize);
	desktop->ui_scratch = (uint16_t *)calloc(BUF_WIDTH * 160, sizeof(uint16_t));
	desktop->sdl_texture_ui_scratch = SDL_CreateTexture(desktop->renderer,
		SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, BUF_WIDTH, 160);
	if (!desktop->ui_scratch || !desktop->sdl_texture_ui_scratch) {
		printf("Could not allocate UI scratch: %s\n", SDL_GetError());
		free(desktop->ui_scratch);
		free(desktop->scrbitmap);
		SDL_DestroyRenderer(desktop->renderer);
		SDL_DestroyWindow(desktop->window);
		free(desktop);
		return NULL;
	}
	desktop->ui_stream_pixels = (uint32_t *)malloc((size_t)DESKTOP_UI_STREAM_WIDTH *
		DESKTOP_UI_STREAM_HEIGHT * sizeof(uint32_t));
	desktop->sdl_texture_ui_stream = SDL_CreateTexture(desktop->renderer,
		SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
		DESKTOP_UI_STREAM_WIDTH, DESKTOP_UI_STREAM_HEIGHT);
	if (!desktop->ui_stream_pixels || !desktop->sdl_texture_ui_stream) {
		printf("Could not allocate UI stream texture: %s\n", SDL_GetError());
		free(desktop->ui_stream_pixels);
		if (desktop->sdl_texture_ui_stream) SDL_DestroyTexture(desktop->sdl_texture_ui_stream);
		SDL_DestroyTexture(desktop->sdl_texture_ui_scratch);
		free(desktop->ui_scratch);
		free(desktop->scrbitmap);
		SDL_DestroyRenderer(desktop->renderer);
		SDL_DestroyWindow(desktop->window);
		free(desktop);
		return NULL;
	}

	size_t totalTextureSize = 0;
	for (int i = 0; i < layer_textures_count; i++) {
		totalTextureSize += layer_textures[i].width * layer_textures[i].height * layer_textures[i].bytes_per_pixel;
	}
	uint8_t *textures = (uint8_t*)malloc(totalTextureSize);
	desktop->texturesMem = textures;

	desktop->tex_layers = (texture_layer_t *)calloc(layer_textures_count, sizeof(texture_layer_t));
	desktop->tex_layers_count = layer_textures_count;

	// Create SDL textures
	desktop->sdl_texture_scrbitmap = SDL_CreateTexture(desktop->renderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_TARGET, BUF_WIDTH, SCR_HEIGHT);
	if (desktop->sdl_texture_scrbitmap == NULL) {
		printf("Could not create sdl_texture_scrbitmap: %s\n", SDL_GetError());
		exit(1);
	}
	/* scrbitmap is the complete work frame and is copied opaquely to the
	 * presentation target.  Using the layer blend equation here makes an
	 * opaque source keep the previous destination contents, which exposes
	 * undefined back-buffer pixels as coloured noise in the GUI background. */
	SDL_SetTextureBlendMode(desktop->sdl_texture_scrbitmap, SDL_BLENDMODE_NONE);
	
	size_t texOffset = 0;
	for (int i = 0; i < layer_textures_count; i++) {
		desktop->tex_layers[i].buffer = textures + texOffset;
		desktop->tex_layers[i].bytes_per_pixel = layer_textures[i].bytes_per_pixel;
		desktop->tex_layers[i].texture = SDL_CreateTexture(desktop->renderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, layer_textures[i].width, layer_textures[i].height);
		if (desktop->tex_layers[i].texture == NULL) {
			printf("Could not create texture layer %d: %s\n", i, SDL_GetError());
			exit(1);
		}
		SDL_SetTextureBlendMode(desktop->tex_layers[i].texture, desktop->blendMode);
		texOffset += layer_textures[i].width * layer_textures[i].height * layer_textures[i].bytes_per_pixel;
	}

	return desktop;
}


/*--------------------------------------------------------
	Video Processing Termination (Common)
--------------------------------------------------------*/

static void desktop_exit(desktop_video_t *desktop) {
	if (desktop->sdl_texture_scrbitmap) {
		SDL_DestroyTexture(desktop->sdl_texture_scrbitmap);
		desktop->sdl_texture_scrbitmap = NULL;
	}

	for (int i = 0; i < desktop->tex_layers_count; i++) {
		desktop->tex_layers[i].buffer = NULL;
		if (desktop->tex_layers[i].texture) {
			SDL_DestroyTexture(desktop->tex_layers[i].texture);
			desktop->tex_layers[i].texture = NULL;
		}
	}

	free(desktop->tex_layers);
	desktop->tex_layers = NULL;
	desktop->tex_layers_count = 0;

	if (desktop->sdl_texture_ui_scratch) {
		SDL_DestroyTexture(desktop->sdl_texture_ui_scratch);
		desktop->sdl_texture_ui_scratch = NULL;
	}
	free(desktop->ui_scratch);
	desktop->ui_scratch = NULL;
	if (desktop->sdl_texture_ui_stream) {
		SDL_DestroyTexture(desktop->sdl_texture_ui_stream);
		desktop->sdl_texture_ui_stream = NULL;
	}
	free(desktop->ui_stream_pixels);
	desktop->ui_stream_pixels = NULL;
	if (desktop->scrbitmap) {
		free(desktop->scrbitmap);
		desktop->scrbitmap = NULL;
	}

	if (desktop->texturesMem) {
		free(desktop->texturesMem);
		desktop->texturesMem = NULL;
	}
}

static void desktop_free(void *data)
{
	desktop_video_t *desktop = (desktop_video_t*)data;
	if (!desktop)
		return;

	/* Renderer-owned textures must be destroyed before the renderer itself. */
	desktop_exit(desktop);

	if (desktop->renderer) {
		SDL_DestroyRenderer(desktop->renderer);
		desktop->renderer = NULL;
	}

	if (desktop->window) {
		SDL_DestroyWindow(desktop->window);
		desktop->window = NULL;
	}

	free(desktop);
}

/*--------------------------------------------------------
	Wait for VSYNC
--------------------------------------------------------*/

static void desktop_waitVsync(void *data)
{
	(void)data;
}


/*--------------------------------------------------------
	Flip Screen
--------------------------------------------------------*/

static void desktop_flipScreen(void *data, bool vsync)
{
	desktop_video_t *desktop = (desktop_video_t*)data;

#if SDL_VERSION_ATLEAST(2, 0, 18)
	if (desktop->vsync_control_available &&
		desktop->vsync_enabled != (vsync ? 1 : 0))
	{
		if (SDL_RenderSetVSync(desktop->renderer, vsync ? 1 : 0) == 0)
			desktop->vsync_enabled = vsync ? 1 : 0;
		else
		{
			printf("Desktop video: runtime VSync control unavailable: %s\n",
				SDL_GetError());
			desktop->vsync_control_available = 0;
		}
	}
#else
	(void)vsync;
#endif

	SDL_RenderPresent(desktop->renderer);
}

static void desktop_beginFrame(void *data)
{
	(void)data;
	/* No-op: SDL2 doesn't use command lists */
}

static void desktop_endFrame(void *data)
{
	(void)data;
	/* No-op: SDL2 doesn't use command lists */
}


/*--------------------------------------------------------
		Get VRAM Address
--------------------------------------------------------*/

static void *desktop_frameAddr(void *data, int frameIndex, int x, int y)
{
	desktop_video_t *desktop = (desktop_video_t *)data;
	if (!desktop || frameIndex != COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER ||
		x < 0 || y < 0 || x >= BUF_WIDTH || y >= 160)
		return NULL;
	return desktop->ui_scratch + (size_t)y * BUF_WIDTH + x;
}

static void desktop_getOutputSize(void *data, int *width, int *height)
{
	desktop_video_t *desktop = (desktop_video_t *)data;
	int w = SCR_WIDTH;
	int h = SCR_HEIGHT;

	if (desktop && desktop->renderer)
		SDL_GetRendererOutputSize(desktop->renderer, &w, &h);
	if (width) *width = w;
	if (height) *height = h;
}

static void desktop_scissor(void *data, uint16_t left, uint16_t top, uint16_t right, uint16_t bottom)
{
	desktop_video_t *desktop = (desktop_video_t*)data;
	
	SDL_Rect sdl_rect;
	sdl_rect.x = left;
	sdl_rect.y = top;
	sdl_rect.w = right - left;
	sdl_rect.h = bottom - top;
	
	SDL_RenderSetClipRect(desktop->renderer, &sdl_rect);
}

/*--------------------------------------------------------
	Clear Draw/Display Frame
--------------------------------------------------------*/

static void desktop_clearScreen(void *data) {
    desktop_video_t *desktop = (desktop_video_t*)data;
    
	SDL_SetRenderDrawColor(desktop->renderer, 0, 0, 0, 0);
	SDL_RenderClear(desktop->renderer);
}

/*--------------------------------------------------------
	Clear Specified Frame
--------------------------------------------------------*/

static void desktop_clearFrame(void *data, int index)
{
	desktop_video_t *desktop = (desktop_video_t*)data;

	switch (index) {
	case COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER:
		/* Clear the work frame (scrbitmap render target) to transparent black */
		SDL_SetRenderTarget(desktop->renderer, desktop->sdl_texture_scrbitmap);
		SDL_SetRenderDrawColor(desktop->renderer, 0, 0, 0, 0);
		SDL_RenderClear(desktop->renderer);
		break;
	default:
		break;
	}
}


/*--------------------------------------------------------
	Fill Specified Frame
--------------------------------------------------------*/

static void desktop_fillFrame(void *data, int frameIndex, uint32_t color)
{
	desktop_video_t *desktop = (desktop_video_t*)data;
	uint8_t r = (color >> 0)  & 0xFF;
	uint8_t g = (color >> 8)  & 0xFF;
	uint8_t b = (color >> 16) & 0xFF;
	uint8_t a = (color >> 24) & 0xFF;

	switch (frameIndex) {
	case COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER:
		SDL_SetRenderTarget(desktop->renderer, desktop->sdl_texture_scrbitmap);
		SDL_SetRenderDrawColor(desktop->renderer, r, g, b, a);
		SDL_RenderClear(desktop->renderer);
		break;
	case COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER:
		SDL_SetRenderTarget(desktop->renderer, NULL);
		SDL_SetRenderDrawColor(desktop->renderer, r, g, b, a);
		SDL_RenderClear(desktop->renderer);
		break;
	default:
		break;
	}
}


/*--------------------------------------------------------
	Copy Rectangular Area
--------------------------------------------------------*/

static void desktop_startWorkFrame(void *data, uint32_t color) {
    desktop_video_t *desktop = (desktop_video_t*)data;
    
    if (SDL_SetRenderTarget(desktop->renderer, desktop->sdl_texture_scrbitmap) != 0) {
        printf("Failed to set render target: %s\n", SDL_GetError());
    }

    uint8_t alpha = color >> 24;
    uint8_t blue = color >> 16;
    uint8_t green = color >> 8;
    uint8_t red = color >> 0;
    SDL_SetRenderDrawColor(desktop->renderer, red, green, blue, alpha);
    SDL_RenderClear(desktop->renderer);
}

static void desktop_transferWorkFrame(void *data, RECT *src_rect, RECT *dst_rect)
{
	desktop_video_t *desktop = (desktop_video_t*)data;
    
    SDL_Rect dst, src;
    
    dst.x = dst_rect->left;
    dst.y = dst_rect->top;
    dst.w = dst_rect->right - dst_rect->left;
    dst.h = dst_rect->bottom - dst_rect->top;
    
    src.x = src_rect->left;
    src.y = src_rect->top;
    src.w = src_rect->right - src_rect->left;
    src.h = src_rect->bottom - src_rect->top;

	/* Reference frames for the GPU backends (tools/compare_frames.py). */
	if (desktop_dump_wanted(++desktop->presented_frames)) {
		uint8_t *pixels = malloc((size_t)src.w * src.h * 4);
		if (pixels != NULL) {
			SDL_SetRenderTarget(desktop->renderer, desktop->sdl_texture_scrbitmap);
			if (SDL_RenderReadPixels(desktop->renderer, &src, SDL_PIXELFORMAT_ABGR8888,
					pixels, src.w * 4) == 0)
				desktop_dump_write("sdl", desktop->presented_frames, pixels,
					src.w, src.h, src.w * 4, false);
			free(pixels);
		}
	}
    
    SDL_SetRenderTarget(desktop->renderer, NULL);
    SDL_RenderCopy(desktop->renderer, desktop->sdl_texture_scrbitmap, &src, &dst);

	// if (!desktop->draw_extra_info) {
	// 	return;
	// }

	// // Let's print the SPR0, SPR1 SPR2 and FIX in the empty space of the screen (right size 0.5 scale)
	// SDL_Rect dst_rect_spr0 = { BUF_WIDTH, 0, BUF_WIDTH / 2, TEXTURE_HEIGHT / 2 };
	// SDL_Rect dst_rect_spr0_border = { dst_rect_spr0.x - 1, dst_rect_spr0.y - 1, dst_rect_spr0.w + 2, dst_rect_spr0.h + 2 };
	// SDL_Rect dst_rect_spr1 = { BUF_WIDTH, dst_rect_spr0.y + dst_rect_spr0.h + 20, BUF_WIDTH / 2, TEXTURE_HEIGHT / 2 };
	// SDL_Rect dst_rect_spr1_border = { dst_rect_spr1.x - 1, dst_rect_spr1.y - 1, dst_rect_spr1.w + 2, dst_rect_spr1.h + 2 };
	// SDL_Rect dst_rect_spr2 = { BUF_WIDTH, dst_rect_spr1.y + dst_rect_spr1.h + 20, BUF_WIDTH / 2, TEXTURE_HEIGHT / 2 };
	// SDL_Rect dst_rect_spr2_border = { dst_rect_spr2.x - 1, dst_rect_spr2.y - 1, dst_rect_spr2.w + 2, dst_rect_spr2.h + 2 };
	// SDL_Rect dst_rect_fix = { BUF_WIDTH, dst_rect_spr2.y + dst_rect_spr2.h + 20, BUF_WIDTH / 2, SCR_HEIGHT / 2 };
	// SDL_Rect dst_rect_fix_border = { dst_rect_fix.x - 1, dst_rect_fix.y - 1, dst_rect_fix.w + 2, dst_rect_fix.h + 2 };

	// SDL_SetRenderDrawColor(desktop->renderer, 255, 0, 0, 255);
	// SDL_RenderFillRect(desktop->renderer, &dst_rect_spr0_border);
	// SDL_RenderFillRect(desktop->renderer, &dst_rect_spr1_border);
	// SDL_RenderFillRect(desktop->renderer, &dst_rect_spr2_border);
	// SDL_RenderFillRect(desktop->renderer, &dst_rect_fix_border);
	// SDL_SetRenderDrawColor(desktop->renderer, 0, 0, 0, 255);
	// SDL_RenderFillRect(desktop->renderer, &dst_rect_spr0);
	// SDL_RenderFillRect(desktop->renderer, &dst_rect_spr1);
	// SDL_RenderFillRect(desktop->renderer, &dst_rect_spr2);
	// SDL_RenderFillRect(desktop->renderer, &dst_rect_fix);

	// SDL_RenderCopy(desktop->renderer, desktop->sdl_texture_tex_spr0, NULL, &dst_rect_spr0);
	// SDL_RenderCopy(desktop->renderer, desktop->sdl_texture_tex_spr1, NULL, &dst_rect_spr1);
	// SDL_RenderCopy(desktop->renderer, desktop->sdl_texture_tex_spr2, NULL, &dst_rect_spr2);	
	// SDL_RenderCopy(desktop->renderer, desktop->sdl_texture_tex_fix, NULL, &dst_rect_fix);

}

static int desktop_capture_to_scratch(desktop_video_t *desktop, RECT *src_rect, RECT *dst_rect, int rotate)
{
	int sw = src_rect->right - src_rect->left;
	int sh = src_rect->bottom - src_rect->top;
	int dw = dst_rect->right - dst_rect->left;
	int dh = dst_rect->bottom - dst_rect->top;
	uint16_t *pixels;
	SDL_Rect src = { src_rect->left, src_rect->top, sw, sh };

	if (dw <= 0 || dh <= 0 || dst_rect->left < 0 || dst_rect->top < 0 ||
		dst_rect->right > BUF_WIDTH || dst_rect->bottom > 160)
		return 0;
	pixels = (uint16_t *)malloc((size_t)sw * sh * sizeof(uint16_t));
	if (!pixels) return 0;
	SDL_SetRenderTarget(desktop->renderer, desktop->sdl_texture_scrbitmap);
	if (SDL_RenderReadPixels(desktop->renderer, &src, SDL_PIXELFORMAT_ABGR1555,
		pixels, sw * (int)sizeof(uint16_t)) != 0) {
		SDL_SetRenderTarget(desktop->renderer, NULL);
		free(pixels);
		return 0;
	}
	SDL_SetRenderTarget(desktop->renderer, NULL);
	for (int y = 0; y < dh; y++) {
		uint16_t *dst = desktop->ui_scratch +
			(size_t)(dst_rect->top + y) * BUF_WIDTH + dst_rect->left;
		for (int x = 0; x < dw; x++) {
			int sx, sy;
			if (rotate) {
				sx = (y * sw) / dh;
				sy = sh - 1 - (x * sh) / dw;
			} else {
				sx = (x * sw) / dw;
				sy = (y * sh) / dh;
			}
			dst[x] = pixels[(size_t)sy * sw + sx];
		}
	}
	free(pixels);
	return 1;
}

static void desktop_draw_scratch(desktop_video_t *desktop, RECT *src_rect, RECT *dst_rect)
{
	SDL_Rect src = { src_rect->left, src_rect->top, src_rect->right - src_rect->left, src_rect->bottom - src_rect->top };
	SDL_Rect dst = { dst_rect->left, dst_rect->top, dst_rect->right - dst_rect->left, dst_rect->bottom - dst_rect->top };
	SDL_UpdateTexture(desktop->sdl_texture_ui_scratch, NULL, desktop->ui_scratch,
		BUF_WIDTH * (int)sizeof(uint16_t));
	SDL_SetRenderTarget(desktop->renderer, NULL);
	SDL_RenderCopy(desktop->renderer, desktop->sdl_texture_ui_scratch, &src, &dst);
}

static void desktop_copyRect(void *data, int srcIndex, int dstIndex, RECT *src_rect, RECT *dst_rect)
{
	desktop_video_t *desktop = (desktop_video_t*)data;
	SDL_Rect src = { src_rect->left, src_rect->top, src_rect->right - src_rect->left, src_rect->bottom - src_rect->top };
	SDL_Rect dst = { dst_rect->left, dst_rect->top, dst_rect->right - dst_rect->left, dst_rect->bottom - dst_rect->top };

	if (srcIndex == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP &&
		dstIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER) {
		desktop_capture_to_scratch(desktop, src_rect, dst_rect, 0);
	} else if (srcIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER &&
		dstIndex == COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER) {
		desktop_draw_scratch(desktop, src_rect, dst_rect);
	} else if (srcIndex == COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER &&
		dstIndex == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP) {
		SDL_SetRenderTarget(desktop->renderer, NULL);
		SDL_RenderCopy(desktop->renderer, desktop->sdl_texture_scrbitmap, &src, &dst);
	}
}


/*--------------------------------------------------------
	Copy Rectangular Area with Horizontal Flip
--------------------------------------------------------*/

static void desktop_copyRectFlip(void *data, int srcIndex, int dstIndex, RECT *src_rect, RECT *dst_rect)
{
	(void)data; (void)dstIndex; (void)dst_rect; (void)srcIndex; (void)src_rect;
}


/*--------------------------------------------------------
	Copy Rectangular Area with 270-degree Rotation
--------------------------------------------------------*/

static void desktop_copyRectRotate(void *data, int srcIndex, int dstIndex, RECT *src_rect, RECT *dst_rect)
{
	desktop_video_t *desktop = (desktop_video_t *)data;
	if (srcIndex == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP &&
		dstIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER)
		desktop_capture_to_scratch(desktop, src_rect, dst_rect, 1);
}


/*--------------------------------------------------------
	Draw Texture with Specified Rectangular Area
--------------------------------------------------------*/

static void desktop_drawTexture(void *data, int srcIndex, int dstIndex, RECT *src_rect, RECT *dst_rect)
{
	desktop_video_t *desktop = (desktop_video_t *)data;
	if (srcIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER &&
		dstIndex == COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER)
		desktop_draw_scratch(desktop, src_rect, dst_rect);
}

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

static void desktop_writeIndexedTextureRect(void *data, uint8_t textureIndex,
	int x, int y, int width, int height, const uint8_t *pixels, int srcPitch)
{
	desktop_video_t *desktop = (desktop_video_t *)data;
	texture_layer_t *layer;
	int texture_width, texture_height;
	int row;

	if (!desktop || textureIndex >= desktop->tex_layers_count || !pixels ||
	    width <= 0 || height <= 0 || srcPitch < width)
		return;
	layer = &desktop->tex_layers[textureIndex];
	if (layer->bytes_per_pixel != 1 || !layer->buffer || !layer->texture)
		return;
	if (SDL_QueryTexture(layer->texture, NULL, NULL, &texture_width, &texture_height) != 0)
		return;
	if (x < 0 || y < 0 || x + width > texture_width || y + height > texture_height)
		return;

	for (row = 0; row < height; row++)
		memcpy(layer->buffer + (y + row) * texture_width + x,
			pixels + row * srcPitch, (size_t)width);
}

static void desktop_writeDirectTextureRect(void *data, uint8_t textureIndex,
	int x, int y, int width, int height, const uint16_t *pixels, int srcPitch)
{
	desktop_video_t *desktop = (desktop_video_t *)data;
	texture_layer_t *layer;
	int texture_width, texture_height;
	int row;

	if (!desktop || textureIndex >= desktop->tex_layers_count || !pixels ||
	    width <= 0 || height <= 0 || srcPitch < width)
		return;
	layer = &desktop->tex_layers[textureIndex];
	if (layer->bytes_per_pixel != 2 || !layer->buffer || !layer->texture)
		return;
	if (SDL_QueryTexture(layer->texture, NULL, NULL, &texture_width, &texture_height) != 0)
		return;
	if (x < 0 || y < 0 || x + width > texture_width || y + height > texture_height)
		return;

	for (row = 0; row < height; row++)
		memcpy(layer->buffer + ((size_t)(y + row) * texture_width + x) * 2,
			pixels + row * srcPitch, (size_t)width * sizeof(uint16_t));
}

static SDL_Texture *desktop_prepareBlitTexture(desktop_video_t *desktop,
	uint8_t textureIndex, const uint16_t *clut)
{
	SDL_Point size;
	texture_layer_t *layer;
	void *pixels;
	int pitch;

	if (!desktop || textureIndex >= desktop->tex_layers_count)
		return NULL;
	layer = &desktop->tex_layers[textureIndex];
	if (!layer->texture || !layer->buffer)
		return NULL;
	if (SDL_QueryTexture(layer->texture, NULL, NULL, &size.x, &size.y) != 0)
		return NULL;
	if (SDL_LockTexture(layer->texture, NULL, &pixels, &pitch) != 0)
		return NULL;

	if (layer->bytes_per_pixel == 1) {
		int i, j;
		if (!clut) {
			SDL_UnlockTexture(layer->texture);
			return NULL;
		}
		for (i = 0; i < size.y; ++i) {
			uint16_t *dst_row = (uint16_t *)((uint8_t *)pixels + i * pitch);
			for (j = 0; j < size.x; ++j) {
				uint8_t pixel_value = layer->buffer[i * size.x + j];
				dst_row[j] = clut[pixel_value];
			}
		}
	} else {
		int row;
		for (row = 0; row < size.y; row++)
			memcpy((uint8_t *)pixels + row * pitch,
				layer->buffer + (size_t)row * size.x * 2,
				(size_t)size.x * 2);
	}

	SDL_UnlockTexture(layer->texture);
	return layer->texture;
}

static void desktop_blitSpriteVertices(void *data, uint8_t textureIndex,
	const uint16_t *clut, uint8_t bank_index,
	uint32_t vertices_count, const video_sprite_vertex_t *vertices)
{
	desktop_video_t *desktop = (desktop_video_t *)data;
	SDL_Texture *texture;
	uint32_t i;

	(void)bank_index;
	if (!vertices || vertices_count < 2)
		return;
	texture = desktop_prepareBlitTexture(desktop, textureIndex, clut);
	if (!texture)
		return;

	for (i = 0; i + 1 < vertices_count; i += 2) {
		const video_sprite_vertex_t *vertex1 = &vertices[i];
		const video_sprite_vertex_t *vertex2 = &vertices[i + 1];
		SDL_Rect dst_rect = {
			vertex1->x,
			vertex1->y,
			abs(vertex2->x - vertex1->x),
			abs(vertex2->y - vertex1->y)
		};
		SDL_Rect src_rect = {
			MIN(vertex1->u, vertex2->u),
			MIN(vertex1->v, vertex2->v),
			abs((int)vertex2->u - (int)vertex1->u),
			abs((int)vertex2->v - (int)vertex1->v)
		};
		SDL_RendererFlip flip = SDL_FLIP_NONE;

		if (vertex1->u > vertex2->u) flip |= SDL_FLIP_HORIZONTAL;
		if (vertex1->v > vertex2->v) flip |= SDL_FLIP_VERTICAL;
		SDL_RenderCopyEx(desktop->renderer, texture, &src_rect, &dst_rect,
			0, NULL, flip);
	}
}

static void desktop_commitTextureUpdates(void *data, uint8_t textureIndex) {
	(void)data; (void)textureIndex;
}

static void desktop_uploadClut(void *data, uint16_t *clut, uint8_t bank_index) {
	(void)bank_index; (void)clut; (void)data;
}

static void desktop_blitPointVertices(void *data, uint32_t points_count,
	const video_point_vertex_t *vertices) {
	desktop_video_t *desktop = (desktop_video_t*)data;
	uint32_t i;

	if (!desktop || !vertices)
		return;
	for (i = 0; i < points_count; i++)
	{
		uint16_t c = vertices[i].color;
		SDL_SetRenderDrawColor(desktop->renderer, GETR15(c), GETG15(c), GETB15(c), 255);
		SDL_RenderDrawPoint(desktop->renderer, vertices[i].x, vertices[i].y);
	}
}

static void desktop_enableDepthTest(void *data) {
	(void)data;
	// No-op: depth test not needed on desktop yet
}

static void desktop_disableDepthTest(void *data) {
	(void)data;
	// No-op: depth test not needed on desktop yet
}

static void desktop_clearDepthBuffer(void *data) {
	(void)data;
	// No-op: depth buffer not used on desktop yet
}

static void desktop_clearColorBuffer(void *data) {
	(void)data;
	// No-op: color buffer clear within scissor not needed on desktop yet
}

/*--------------------------------------------------------
	2D UI Drawing Primitives
--------------------------------------------------------*/

static uint32_t desktop_ui_texel_to_argb(uint16_t c, int format)
{
	if (format == UI_PIXFMT_4444) {
		uint8_t r = (uint8_t)((c & 0x000f) * 17);
		uint8_t g = (uint8_t)(((c >> 4) & 0x000f) * 17);
		uint8_t b = (uint8_t)(((c >> 8) & 0x000f) * 17);
		uint8_t a = (uint8_t)(((c >> 12) & 0x000f) * 17);
		return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
	}
	{
		uint8_t r = (uint8_t)((c & 0x001f) * 255 / 31);
		uint8_t g = (uint8_t)(((c >> 5) & 0x001f) * 255 / 31);
		uint8_t b = (uint8_t)(((c >> 10) & 0x001f) * 255 / 31);
		uint8_t a = (c & 0x8000) ? 255 : 0;
		return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
	}
}

static void desktop_drawUISprite(void *data, void *tex, int tex_format, int tex_swizzled,
	int tex_width, int tex_height, int tex_stride,
	int su, int sv, int sw, int sh,
	int dx, int dy, int dw, int dh, int blend)
{
	desktop_video_t *desktop = (desktop_video_t *)data;
	const uint16_t *pixels = (const uint16_t *)tex;
	SDL_Rect upload = {0, 0, sw, sh};
	SDL_Rect src_rect = {0, 0, sw, sh};
	SDL_Rect dst_rect = {dx, dy, dw, dh};
	(void)tex_swizzled;
	(void)tex_width;

	if (!desktop || !desktop->renderer || !desktop->sdl_texture_ui_stream || !pixels ||
		sw <= 0 || sh <= 0 || sw > DESKTOP_UI_STREAM_WIDTH || sh > DESKTOP_UI_STREAM_HEIGHT ||
		su < 0 || sv < 0 || su + sw > tex_stride || sv + sh > tex_height)
		return;

	for (int y = 0; y < sh; y++) {
		const uint16_t *src = pixels + (size_t)(sv + y) * tex_stride + su;
		uint32_t *dst = desktop->ui_stream_pixels + (size_t)y * sw;
		for (int x = 0; x < sw; x++)
			dst[x] = desktop_ui_texel_to_argb(src[x], tex_format);
	}

	if (SDL_UpdateTexture(desktop->sdl_texture_ui_stream, &upload,
		desktop->ui_stream_pixels, sw * (int)sizeof(uint32_t)) != 0)
		return;
	SDL_SetTextureBlendMode(desktop->sdl_texture_ui_stream,
		blend ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
	SDL_SetTextureColorMod(desktop->sdl_texture_ui_stream, 255, 255, 255);
	SDL_SetTextureAlphaMod(desktop->sdl_texture_ui_stream, 255);
	SDL_RenderCopy(desktop->renderer, desktop->sdl_texture_ui_stream, &src_rect, &dst_rect);
}

static void desktop_drawUILine(void *data,
	int x1, int y1, int x2, int y2, uint32_t color)
{
	desktop_video_t *desktop = (desktop_video_t *)data;
	uint8_t r = color & 0xFF;
	uint8_t g = (color >> 8) & 0xFF;
	uint8_t b = (color >> 16) & 0xFF;
	uint8_t a = (color >> 24) & 0xFF;

	if (!desktop || !desktop->renderer)
		return;
	SDL_SetRenderDrawColor(desktop->renderer, r, g, b, a);
	SDL_RenderDrawLine(desktop->renderer, x1, y1, x2, y2);
}

static void desktop_drawUILineGradient(void *data,
	int x1, int y1, int x2, int y2, uint32_t color1, uint32_t color2)
{
	desktop_video_t *desktop = (desktop_video_t *)data;
	int dx = x2 - x1;
	int dy = y2 - y1;
	int abs_dx = dx > 0 ? dx : -dx;
	int abs_dy = dy > 0 ? dy : -dy;
	int steps = abs_dx > abs_dy ? abs_dx : abs_dy;
	uint8_t r1 = color1 & 0xFF;
	uint8_t g1 = (color1 >> 8) & 0xFF;
	uint8_t b1 = (color1 >> 16) & 0xFF;
	uint8_t a1 = (color1 >> 24) & 0xFF;
	uint8_t r2 = color2 & 0xFF;
	uint8_t g2 = (color2 >> 8) & 0xFF;
	uint8_t b2 = (color2 >> 16) & 0xFF;
	uint8_t a2 = (color2 >> 24) & 0xFF;
	int i;

	if (!desktop || !desktop->renderer || steps == 0)
		return;

	for (i = 0; i <= steps; i++) {
		float t = (float)i / steps;
		int x = x1 + (int)(dx * t);
		int y = y1 + (int)(dy * t);
		uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
		uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
		uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);
		uint8_t a = (uint8_t)(a1 + (a2 - a1) * t);

		SDL_SetRenderDrawColor(desktop->renderer, r, g, b, a);
		SDL_RenderDrawPoint(desktop->renderer, x, y);
	}
}

static void desktop_drawUIRect(void *data,
	int x, int y, int w, int h, uint32_t color)
{
	desktop_video_t *desktop = (desktop_video_t *)data;
	SDL_Rect rect = {x, y, w, h};
	uint8_t r = color & 0xFF;
	uint8_t g = (color >> 8) & 0xFF;
	uint8_t b = (color >> 16) & 0xFF;
	uint8_t a = (color >> 24) & 0xFF;

	if (!desktop || !desktop->renderer)
		return;
	SDL_SetRenderDrawColor(desktop->renderer, r, g, b, a);
	SDL_RenderDrawRect(desktop->renderer, &rect);
}

static void desktop_fillUIRect(void *data,
	int x, int y, int w, int h, uint32_t color)
{
	desktop_video_t *desktop = (desktop_video_t *)data;
	SDL_Rect rect = {x, y, w, h};
	uint8_t r = color & 0xFF;
	uint8_t g = (color >> 8) & 0xFF;
	uint8_t b = (color >> 16) & 0xFF;
	uint8_t a = (color >> 24) & 0xFF;

	if (!desktop || !desktop->renderer)
		return;
	SDL_SetRenderDrawBlendMode(desktop->renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(desktop->renderer, r, g, b, a);
	SDL_RenderFillRect(desktop->renderer, &rect);
}

static void desktop_fillUIRectGradient(void *data,
	int x, int y, int w, int h, uint32_t color1, uint32_t color2, int direction)
{
	desktop_video_t *desktop = (desktop_video_t *)data;
	uint8_t r1 = color1 & 0xFF;
	uint8_t g1 = (color1 >> 8) & 0xFF;
	uint8_t b1 = (color1 >> 16) & 0xFF;
	uint8_t a1 = (color1 >> 24) & 0xFF;
	uint8_t r2 = color2 & 0xFF;
	uint8_t g2 = (color2 >> 8) & 0xFF;
	uint8_t b2 = (color2 >> 16) & 0xFF;
	uint8_t a2 = (color2 >> 24) & 0xFF;
	int lines = direction == 0 ? w : h;
	int i;

	if (!desktop || !desktop->renderer || w <= 0 || h <= 0 || lines <= 0)
		return;
	if (lines == 1) {
		desktop_fillUIRect(data, x, y, w, h, color1);
		return;
	}

	SDL_SetRenderDrawBlendMode(desktop->renderer, SDL_BLENDMODE_BLEND);
	for (i = 0; i < lines; i++) {
		float t = (float)i / (lines - 1);
		uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
		uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
		uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);
		uint8_t a = (uint8_t)(a1 + (a2 - a1) * t);

		SDL_SetRenderDrawColor(desktop->renderer, r, g, b, a);
		if (direction == 0)
			SDL_RenderDrawLine(desktop->renderer, x + i, y, x + i, y + h - 1);
		else
			SDL_RenderDrawLine(desktop->renderer, x, y + i, x + w - 1, y + i);
	}
}

static void desktop_setUIScissor(void *data, int x, int y, int w, int h)
{
	desktop_video_t *desktop = (desktop_video_t *)data;
	SDL_Rect scissor = {x, y, w, h};

	if (!desktop || !desktop->renderer)
		return;
	SDL_RenderSetClipRect(desktop->renderer, &scissor);
}

video_driver_t video_desktop_sdl = {
	"desktop",
	desktop_init,
	desktop_free,
	desktop_waitVsync,
	desktop_flipScreen,
	desktop_beginFrame,
	desktop_endFrame,
	desktop_frameAddr,
	NULL, // readFrame: Desktop state thumbnails use CPU-side UI scratch
	desktop_getOutputSize,
	desktop_scissor,
	desktop_clearScreen,
	desktop_clearFrame,
	desktop_fillFrame,
	desktop_startWorkFrame,
	desktop_transferWorkFrame,
	desktop_copyRect,
	desktop_copyRectFlip,
	desktop_copyRectRotate,
	desktop_drawTexture,
	desktop_commitTextureUpdates,
	desktop_uploadClut,
	desktop_writeIndexedTextureRect,
	desktop_writeDirectTextureRect,
	desktop_blitSpriteVertices,
	desktop_blitPointVertices,
	desktop_enableDepthTest,
	desktop_disableDepthTest,
	desktop_clearDepthBuffer,
	desktop_clearColorBuffer,
	desktop_drawUISprite,
	desktop_drawUILine,
	desktop_drawUILineGradient,
	desktop_drawUIRect,
	desktop_fillUIRect,
	desktop_fillUIRectGradient,
	desktop_setUIScissor,
	NULL, // prepareSpriteVertices
	NULL, // getPresentationViewport
	NULL, // setOutputOffset
	NULL, // setOutputMode
};
