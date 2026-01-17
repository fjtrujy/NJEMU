/******************************************************************************

	video.c

	Desktop Video Control Functions

******************************************************************************/

#include "emumain.h"

#include <stdlib.h>
#include <SDL.h>

#define OUTPUT_WIDTH 640
#define OUTPUT_HEIGHT 480

typedef struct desktop_video {
	SDL_Window* window;
	SDL_Renderer* renderer;
	bool draw_extra_info;
	SDL_BlendMode blendMode;
    
    // Base clut starting address
    uint16_t *clut_base;

	// Original buffers containing clut indexes
	uint8_t *scrbitmap;
	uint8_t *tex_spr;
	uint8_t *tex_spr0;
	uint8_t *tex_spr1;
	uint8_t *tex_spr2;
	uint8_t *tex_fix;

	SDL_Texture *sdl_texture_scrbitmap;
	SDL_Texture *sdl_texture_tex_spr0;
	SDL_Texture *sdl_texture_tex_spr1;
	SDL_Texture *sdl_texture_tex_spr2;
	SDL_Texture *sdl_texture_tex_fix;
} desktop_video_t;

/******************************************************************************
	Global Functions
******************************************************************************/

static void desktop_start(void *data) {
	desktop_video_t *desktop = (desktop_video_t*)data;

	// Original buffers containing clut indexes
	size_t scrbitmapSize = BUF_WIDTH * SCR_HEIGHT;
	size_t textureSize = BUF_WIDTH * TEXTURE_HEIGHT;
	desktop->scrbitmap = (uint8_t*)malloc(scrbitmapSize);
	uint8_t *tex_spr = (uint8_t*)malloc(textureSize * 3);
	desktop->tex_spr = tex_spr;
	desktop->tex_spr0 = tex_spr;
	desktop->tex_spr1 = tex_spr + textureSize;
	desktop->tex_spr2 = tex_spr + textureSize * 2;
	desktop->tex_fix = (uint8_t*)malloc(textureSize);

	// Create SDL textures
	desktop->sdl_texture_scrbitmap = SDL_CreateTexture(desktop->renderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_TARGET, BUF_WIDTH, SCR_HEIGHT);
	if (desktop->sdl_texture_scrbitmap == NULL) {
		printf("Could not create sdl_texture_scrbitmap: %s\n", SDL_GetError());
		return;
	}	
	desktop->sdl_texture_tex_spr0 = SDL_CreateTexture(desktop->renderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, BUF_WIDTH, TEXTURE_HEIGHT);
	if (desktop->sdl_texture_tex_spr0 == NULL) {
		printf("Could not create sdl_texture_tex_spr0: %s\n", SDL_GetError());
		return;
	}
	desktop->sdl_texture_tex_spr1 = SDL_CreateTexture(desktop->renderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, BUF_WIDTH, TEXTURE_HEIGHT);
	if (desktop->sdl_texture_tex_spr1 == NULL) {
		printf("Could not create sdl_texture_tex_spr1: %s\n", SDL_GetError());
		return;
	}

	desktop->sdl_texture_tex_spr2 = SDL_CreateTexture(desktop->renderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, BUF_WIDTH, TEXTURE_HEIGHT);
	if (desktop->sdl_texture_tex_spr2 == NULL) {
		printf("Could not create sdl_texture_tex_spr2: %s\n", SDL_GetError());
		return;
	}

	desktop->sdl_texture_tex_fix = SDL_CreateTexture(desktop->renderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STREAMING, BUF_WIDTH, TEXTURE_HEIGHT);
	if (desktop->sdl_texture_tex_fix == NULL) {
		printf("Could not create sdl_texture_tex_fix: %s\n", SDL_GetError());
		return;
	}

	SDL_SetTextureBlendMode(desktop->sdl_texture_scrbitmap, desktop->blendMode);
	SDL_SetTextureBlendMode(desktop->sdl_texture_tex_spr0, desktop->blendMode);
	SDL_SetTextureBlendMode(desktop->sdl_texture_tex_spr1, desktop->blendMode);
	SDL_SetTextureBlendMode(desktop->sdl_texture_tex_spr2, desktop->blendMode);
	SDL_SetTextureBlendMode(desktop->sdl_texture_tex_fix, desktop->blendMode);

	ui_init();
}

static void *desktop_init(void)
{
	uint32_t windows_width, windows_height;
	desktop_video_t *desktop = (desktop_video_t*)calloc(1, sizeof(desktop_video_t));
	desktop->draw_extra_info = false;

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

	desktop_start(desktop);
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

	if (desktop->sdl_texture_tex_spr0) {
		SDL_DestroyTexture(desktop->sdl_texture_tex_spr0);
		desktop->sdl_texture_tex_spr0 = NULL;
	}

	if (desktop->sdl_texture_tex_spr1) {
		SDL_DestroyTexture(desktop->sdl_texture_tex_spr1);
		desktop->sdl_texture_tex_spr1 = NULL;
	}

	if (desktop->sdl_texture_tex_spr2) {
		SDL_DestroyTexture(desktop->sdl_texture_tex_spr2);
		desktop->sdl_texture_tex_spr2 = NULL;
	}

	if (desktop->sdl_texture_tex_fix) {
		SDL_DestroyTexture(desktop->sdl_texture_tex_fix);
		desktop->sdl_texture_tex_fix = NULL;
	}

	if (desktop->scrbitmap) {
		free(desktop->scrbitmap);
		desktop->scrbitmap = NULL;
	}

	if (desktop->tex_spr) {
		free(desktop->tex_spr);
		desktop->tex_spr = NULL;
		desktop->tex_spr0 = NULL;
		desktop->tex_spr1 = NULL;
		desktop->tex_spr2 = NULL;
	}

	if (desktop->tex_fix) {
		free(desktop->tex_fix);
		desktop->tex_fix = NULL;
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
	Video Mode Setting
--------------------------------------------------------*/


static void desktop_setMode(void *data, int mode)
{
	desktop_video_t *desktop = (desktop_video_t*)data;
}

static void desktop_setClutBaseAddr(void *data, uint16_t *clut_base)
{
	desktop_video_t *desktop = (desktop_video_t*)data;
	desktop->clut_base = clut_base;
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


/*--------------------------------------------------------
	Get VRAM Address
--------------------------------------------------------*/

static void *desktop_frameAddr(void *data, void *frame, int x, int y)
{
	return NULL;
}

static void *desktop_workFrame(void *data, enum WorkBuffer buffer)
{
	desktop_video_t *desktop = (desktop_video_t*)data;
	switch (buffer) {
		case SCRBITMAP:
			return desktop->scrbitmap;
		case TEX_SPR0:
			return desktop->tex_spr0;
		case TEX_SPR1:
			return desktop->tex_spr1;
		case TEX_SPR2:
			return desktop->tex_spr2;
		case TEX_FIX:
			return desktop->tex_fix;
		default:
			return NULL;
	}
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

static void desktop_clearFrame(void *data, void *frame)
{
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

	if (!desktop->draw_extra_info) {
		return;
	}

	// Let's print the SPR0, SPR1 SPR2 and FIX in the empty space of the screen (right size 0.5 scale)
	SDL_Rect dst_rect_spr0 = { BUF_WIDTH, 0, BUF_WIDTH / 2, TEXTURE_HEIGHT / 2 };
	SDL_Rect dst_rect_spr0_border = { dst_rect_spr0.x - 1, dst_rect_spr0.y - 1, dst_rect_spr0.w + 2, dst_rect_spr0.h + 2 };
	SDL_Rect dst_rect_spr1 = { BUF_WIDTH, dst_rect_spr0.y + dst_rect_spr0.h + 20, BUF_WIDTH / 2, TEXTURE_HEIGHT / 2 };
	SDL_Rect dst_rect_spr1_border = { dst_rect_spr1.x - 1, dst_rect_spr1.y - 1, dst_rect_spr1.w + 2, dst_rect_spr1.h + 2 };
	SDL_Rect dst_rect_spr2 = { BUF_WIDTH, dst_rect_spr1.y + dst_rect_spr1.h + 20, BUF_WIDTH / 2, TEXTURE_HEIGHT / 2 };
	SDL_Rect dst_rect_spr2_border = { dst_rect_spr2.x - 1, dst_rect_spr2.y - 1, dst_rect_spr2.w + 2, dst_rect_spr2.h + 2 };
	SDL_Rect dst_rect_fix = { BUF_WIDTH, dst_rect_spr2.y + dst_rect_spr2.h + 20, BUF_WIDTH / 2, SCR_HEIGHT / 2 };
	SDL_Rect dst_rect_fix_border = { dst_rect_fix.x - 1, dst_rect_fix.y - 1, dst_rect_fix.w + 2, dst_rect_fix.h + 2 };

	SDL_SetRenderDrawColor(desktop->renderer, 255, 0, 0, 255);
	SDL_RenderFillRect(desktop->renderer, &dst_rect_spr0_border);
	SDL_RenderFillRect(desktop->renderer, &dst_rect_spr1_border);
	SDL_RenderFillRect(desktop->renderer, &dst_rect_spr2_border);
	SDL_RenderFillRect(desktop->renderer, &dst_rect_fix_border);
	SDL_SetRenderDrawColor(desktop->renderer, 0, 0, 0, 255);
	SDL_RenderFillRect(desktop->renderer, &dst_rect_spr0);
	SDL_RenderFillRect(desktop->renderer, &dst_rect_spr1);
	SDL_RenderFillRect(desktop->renderer, &dst_rect_spr2);
	SDL_RenderFillRect(desktop->renderer, &dst_rect_fix);

	SDL_RenderCopy(desktop->renderer, desktop->sdl_texture_tex_spr0, NULL, &dst_rect_spr0);
	SDL_RenderCopy(desktop->renderer, desktop->sdl_texture_tex_spr1, NULL, &dst_rect_spr1);
	SDL_RenderCopy(desktop->renderer, desktop->sdl_texture_tex_spr2, NULL, &dst_rect_spr2);	
	SDL_RenderCopy(desktop->renderer, desktop->sdl_texture_tex_fix, NULL, &dst_rect_fix);

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
	return NULL;
}

static SDL_Texture *desktop_getTexture(void *data, enum WorkBuffer buffer) {
	desktop_video_t *desktop = (desktop_video_t*)data;
	switch (buffer) {
		case SCRBITMAP:
			return desktop->sdl_texture_scrbitmap;
		case TEX_SPR0:
			return desktop->sdl_texture_tex_spr0;
		case TEX_SPR1:
			return desktop->sdl_texture_tex_spr1;
		case TEX_SPR2:
			return desktop->sdl_texture_tex_spr2;
		case TEX_FIX:
			return desktop->sdl_texture_tex_fix;
		default:
			return NULL;
	}
}

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

static void desktop_blitTexture(void *data, enum WorkBuffer buffer, void *clut, uint8_t clut_index, uint32_t vertices_count, void *vertices) {
	// We need to transform the texutres saved that uses clut into a SDL texture compatible format
	SDL_Point size;
	desktop_video_t *desktop = (desktop_video_t *)data;
	struct Vertex *vertexs = (struct Vertex *)vertices;
	uint16_t *clut_texture = (uint16_t *)clut;
	uint8_t *tex_fix = desktop_workFrame(data, buffer);
	SDL_Texture *texture = desktop_getTexture(data, buffer);
	SDL_QueryTexture(texture, NULL, NULL, &size.x, &size.y);

	// Lock texture
	void *pixels;
	int pitch;
	SDL_LockTexture(texture, NULL, &pixels, &pitch);

	// Obtain the color from the clut using the index and copy it to the pixels array
	for (int i = 0; i < size.y; ++i) {
		for (int j = 0; j < size.x; ++j) {
			int index = i * size.x + j;
			uint8_t pixelValue = tex_fix[index];
			uint16_t color = clut_texture[pixelValue];

			uint16_t *pixel = (uint16_t*)pixels + index;
			*pixel = color;
		}
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

static void desktop_uploadMem(void *data, enum WorkBuffer buffer) {
}

static void desktop_uploadClut(void *data, uint16_t *bank, uint8_t bank_index) {
}


video_driver_t video_desktop = {
	"desktop",
	desktop_init,
	desktop_free,
	desktop_setMode,
	desktop_setClutBaseAddr,
	desktop_waitVsync,
	desktop_flipScreen,
	desktop_frameAddr,
	desktop_workFrame,
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
};
