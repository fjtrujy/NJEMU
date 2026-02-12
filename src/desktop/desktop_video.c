/******************************************************************************

	video.c

	Desktop Video Control Functions

******************************************************************************/

#include "emumain.h"

#include <stdlib.h>
#include <SDL.h>

#define OUTPUT_WIDTH 640
#define OUTPUT_HEIGHT 480

typedef struct texture_layer {
	SDL_Texture *texture;
	uint8_t *buffer;
	uint8_t bytes_per_pixel;
} texture_layer_t;

typedef struct desktop_video {
	SDL_Window* window;
	SDL_Renderer* renderer;
	bool draw_extra_info;
	SDL_BlendMode blendMode;
    
    // Base clut starting address
    uint16_t *clut_base;
	uint8_t *texturesMem;

	// Original buffers containing clut indexes
	SDL_Texture *sdl_texture_scrbitmap;
	uint8_t *scrbitmap;
	
	texture_layer_t *tex_layers;
	uint8_t tex_layers_count;
} desktop_video_t;

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


    SDL_Window* window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windows_width, windows_height, SDL_WINDOW_SHOWN);

	// Check that the window was successfully created
	if (window == NULL) {
		// In the case that the window could not be made...
		printf("Could not create window: %s\n", SDL_GetError());
		free(desktop);
		return NULL;
	}

	desktop->window = window;

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
	SDL_SetTextureBlendMode(desktop->sdl_texture_scrbitmap, desktop->blendMode);
	
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

	ui_init();

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

	if (desktop->scrbitmap) {
		free(desktop->scrbitmap);
		desktop->scrbitmap = NULL;
	}

	if (desktop->texturesMem) {
		free(desktop->texturesMem);
		desktop->texturesMem = NULL;
	}

	for (int i = 0; i < desktop->tex_layers_count; i++) {
		desktop->tex_layers[i].buffer = NULL;
		if (desktop->tex_layers[i].texture) {
			SDL_DestroyTexture(desktop->tex_layers[i].texture);
			desktop->tex_layers[i].texture = NULL;
		}
	}
}

static void desktop_free(void *data)
{
	desktop_video_t *desktop = (desktop_video_t*)data;

	SDL_DestroyRenderer(desktop->renderer);
	SDL_DestroyWindow(desktop->window);
	
	desktop_exit(desktop);
	free(desktop);
}

/*--------------------------------------------------------
	Wait for VSYNC
--------------------------------------------------------*/

static void desktop_waitVsync(void *data)
{
}


/*--------------------------------------------------------
	Flip Screen
--------------------------------------------------------*/

static void desktop_flipScreen(void *data, bool vsync)
{
	desktop_video_t *desktop = (desktop_video_t*)data;
	SDL_RenderPresent(desktop->renderer);
}

static void desktop_beginFrame(void *data)
{
	/* No-op: SDL2 doesn't use command lists */
}

static void desktop_endFrame(void *data)
{
	/* No-op: SDL2 doesn't use command lists */
}


/*--------------------------------------------------------
		Get VRAM Address
--------------------------------------------------------*/

static void *desktop_frameAddr(void *data, void *frame, int x, int y)
{
	return NULL;
}

static void *desktop_workFrame(void *data)
{
	desktop_video_t *desktop = (desktop_video_t*)data;
	return desktop->scrbitmap;
}

static void *desktop_textureLayer(void *data, uint8_t layerIndex)
{
	desktop_video_t *desktop = (desktop_video_t*)data;
	return desktop->tex_layers[layerIndex].buffer;
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

static void desktop_fillFrame(void *data, void *frame, uint32_t color)
{
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

static void desktop_copyRect(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
}


/*--------------------------------------------------------
	Copy Rectangular Area with Horizontal Flip
--------------------------------------------------------*/

static void desktop_copyRectFlip(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
}


/*--------------------------------------------------------
	Copy Rectangular Area with 270-degree Rotation
--------------------------------------------------------*/

static void desktop_copyRectRotate(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
}


/*--------------------------------------------------------
	Draw Texture with Specified Rectangular Area
--------------------------------------------------------*/

static void desktop_drawTexture(void *data, uint32_t src_fmt, uint32_t dst_fmt, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
}

static void *desktop_getNativeObjects(void *data, int index) {
	desktop_video_t *desktop = (desktop_video_t *)data;
	switch (index) {
	case COMMON_GRAPHIC_OBJECTS_GLOBAL_CONTEXT:
		return desktop->renderer;
	default:
		return NULL;
	}
}

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

static void desktop_blitTexture(void *data, uint8_t textureIndex, void *clut, uint8_t clut_index, uint32_t vertices_count, void *vertices) {
	// We need to transform the texutres saved that uses clut into a SDL texture compatible format
	SDL_Point size;
	desktop_video_t *desktop = (desktop_video_t *)data;
	struct Vertex *vertexs = (struct Vertex *)vertices;
	uint16_t *clut_texture = (uint16_t *)clut;
	uint8_t *tex = desktop->tex_layers[textureIndex].buffer;
	SDL_Texture *texture = desktop->tex_layers[textureIndex].texture;
	SDL_QueryTexture(texture, NULL, NULL, &size.x, &size.y);

	// Lock texture
	void *pixels;
	int pitch;
	SDL_LockTexture(texture, NULL, &pixels, &pitch);

	if (desktop->tex_layers[textureIndex].bytes_per_pixel == 1) {
		// Obtain the color from the clut using the index and copy it to the pixels array
		for (int i = 0; i < size.y; ++i) {
			for (int j = 0; j < size.x; ++j) {
				int index = i * size.x + j;
				uint8_t pixelValue = tex[index];
				uint16_t color = clut_texture[pixelValue];

				uint16_t *pixel = (uint16_t*)pixels + index;
				*pixel = color;
			}
		}
	} else {
		// Direct copy for 2 bytes per pixel (memcpy)
		memcpy(pixels, tex, size.x * size.y * 2);
	}

	// Unlock texture
	SDL_UnlockTexture(texture);

    SDL_Rect dst_rect, src_rect;
    SDL_RendererFlip horizontalFlip, verticalFlip;
	// Render Geometry expect to receive a SDL_Vertex array and it is using triangles
	// however desktop_blitTexture receives a SDL_Vertex array using 2 vertex per sprite
	// so we are going to use SDL_RenderCopy to render all the sprites
	for (int i = 0; i < vertices_count; i += 2) {
		struct Vertex *vertex1 = &vertexs[i];
		struct Vertex *vertex2 = &vertexs[i + 1];

        dst_rect.x = vertex1->x;
        dst_rect.y = vertex1->y;
        dst_rect.w = abs(vertex2->x - vertex1->x);
        dst_rect.h = abs(vertex2->y - vertex1->y);
        
        src_rect.x = MIN(vertex1->u, vertex2->u);
        src_rect.y = MIN(vertex1->v, vertex2->v);
        src_rect.w = abs(vertex2->u - vertex1->u);
        src_rect.h = abs(vertex2->v - vertex1->v);
        
        horizontalFlip = vertex1->u > vertex2->u ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        verticalFlip = vertex1->v > vertex2->v ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE;
        
		// Render the sprite
        SDL_RenderCopyEx(desktop->renderer, texture, &src_rect, &dst_rect, 0, NULL, horizontalFlip | verticalFlip);
	}
}

static void desktop_uploadMem(void *data, uint8_t textureIndex) {
}

static void desktop_uploadClut(void *data, uint16_t *clut, uint8_t bank_index) {
}

static void desktop_blitPoints(void *data, uint32_t points_count, void *vertices) {
	desktop_video_t *desktop = (desktop_video_t*)data;
	struct PointVertex *pts = (struct PointVertex *)vertices;
	uint32_t i;

	for (i = 0; i < points_count; i++)
	{
		uint16_t c = pts[i].color;
		SDL_SetRenderDrawColor(desktop->renderer, GETR15(c), GETG15(c), GETB15(c), 255);
		SDL_RenderDrawPoint(desktop->renderer, pts[i].x, pts[i].y);
	}
}

static void desktop_flushCache(void *data, void *addr, size_t size) {
	// No cache to flush on desktop
}

static void desktop_enableDepthTest(void *data) {
	// No-op: depth test not needed on desktop yet
}

static void desktop_disableDepthTest(void *data) {
	// No-op: depth test not needed on desktop yet
}

static void desktop_clearDepthBuffer(void *data) {
	// No-op: depth buffer not used on desktop yet
}

static void desktop_clearColorBuffer(void *data) {
	// No-op: color buffer clear within scissor not needed on desktop yet
}

video_driver_t video_desktop = {
	"desktop",
	desktop_init,
	desktop_free,
	desktop_waitVsync,
	desktop_flipScreen,
	desktop_beginFrame,
	desktop_endFrame,
	desktop_frameAddr,
	desktop_workFrame,
	desktop_textureLayer,
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
	desktop_getNativeObjects,
	desktop_uploadMem,
	desktop_uploadClut,
	desktop_blitTexture,
	desktop_blitPoints,
	desktop_flushCache,
	desktop_enableDepthTest,
	desktop_disableDepthTest,
	desktop_clearDepthBuffer,
	desktop_clearColorBuffer,
};
