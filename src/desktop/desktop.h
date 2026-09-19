/******************************************************************************

	desktop.h


******************************************************************************/

#ifndef DESKTOP_H
#define DESKTOP_H

#include <stdbool.h>
#include <SDL.h>

#define SCR_WIDTH			480
#define SCR_HEIGHT			272
#define BUF_WIDTH			512

#define REFRESH_RATE		(59.940059)		// (9000000Hz * 1) / (525 * 286)

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
