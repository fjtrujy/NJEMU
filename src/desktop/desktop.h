/******************************************************************************

	desktop.h


******************************************************************************/

#ifndef DESKTOP_H
#define DESKTOP_H

#include <stdbool.h>
#include <SDL.h>
#include "common/video_geometry.h"

#define FONTSIZE			14

/*------------------------------------------------------
	Desktop video driver state (shared with all platform files)
------------------------------------------------------*/

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

#endif /* DESKTOP_H */
