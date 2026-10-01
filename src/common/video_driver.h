/******************************************************************************

	video_driver.h

******************************************************************************/

#ifndef VIDEO_DRIVER_H
#define VIDEO_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAKECOL15(r, g, b)	(((b & 0xf8) << 7) | ((g & 0xf8) << 2) | ((r & 0xf8) >> 3))
#define GETR15(col)			(((col << 3) & 0xf8) | ((col >>  2) & 0x07))
#define GETG15(col)			(((col >> 2) & 0xf8) | ((col >>  7) & 0x07))
#define GETB15(col)			(((col >> 7) & 0xf8) | ((col >> 12) & 0x07))

#define MAKECOL32(r, g, b)	(0xff000000 | ((b & 0xff) << 16) | ((g & 0xff) << 8) | (r & 0xff))
#define GETR32(col)			((col >>  0) & 0xff)
#define GETG32(col)			((col >>  8) & 0xff)
#define GETB32(col)			((col >> 16) & 0xff)

#define MAKECOL32A(r, g, b, a)	(((a & 0xff) << 24) | ((b & 0xff) << 16) | ((g & 0xff) << 8) | (r & 0xff))

#define COLOR_BLACK			  0,  0,  0
#define COLOR_RED			255,  0,  0
#define COLOR_GREEN			  0,255,  0
#define COLOR_BLUE			  0,  0,255
#define COLOR_YELLOW		255,255,  0
#define COLOR_PURPLE		255,  0,255
#define COLOR_CYAN			  0,255,255
#define COLOR_WHITE			255,255,255
#define COLOR_GRAY			127,127,127
#define COLOR_DARKRED		127,  0,  0
#define COLOR_DARKGREEN		  0,127,  0
#define COLOR_DARKBLUE		  0,  0,127
#define COLOR_DARKYELLOW	127,127,  0
#define COLOR_DARKPURPLE	127,  0,127
#define COLOR_DARKCYAN		  0,127,127
#define COLOR_DARKGRAY		 63, 63, 63

#define CNVCOL15TO32(c)				(GETR15(c) | (GETG15(c) << 8) | (GETB15(c) << 16))

typedef struct video_sprite_vertex
{
	uint16_t u, v;
	uint16_t color;
	int16_t x, y, z;
} video_sprite_vertex_t;

typedef struct video_point_vertex
{
	uint16_t color;
	int16_t x, y, z;
} video_point_vertex_t;

struct rectangle
{
	int min_x;
	int max_x;
	int min_y;
	int max_y;
};

typedef struct rect_t
{
	int16_t left;
	int16_t top;
	int16_t right;
	int16_t bottom;
} RECT;

enum CommonGraphicObjects {
	COMMON_GRAPHIC_OBJECTS_GLOBAL_CONTEXT,
	COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER,
	COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER,
	COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP,
	COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER,
};

typedef struct layer_texture_info {
	size_t width;
	size_t height;
	uint8_t bytes_per_pixel;
} layer_texture_info_t;

/* CLUT (Color Look-Up Table) configuration for indexed textures.
 * Each target defines its own emu_clut_info based on palette requirements:
 *   - MVS/NCDZ: 2 banks × 4096 colors (video_palettebank[2][4096])
 *   - CPS1:     1 bank × 3072 colors (video_palette[3072])
 */
typedef struct clut_info {
	uint16_t *base;              /* Pointer to palette memory */
	uint16_t entries_per_bank;   /* Colors per bank (e.g., 4096 for Neo Geo, 3072 for CPS1) */
	uint8_t bank_count;          /* Number of banks (2 for Neo Geo, 1 for CPS) */
} clut_info_t;

enum {
	UI_GRADIENT_HORIZONTAL = 0,
	UI_GRADIENT_VERTICAL
};

typedef struct video_driver
{
	/* Human-readable identifier. */
	const char *ident;
	/* Creates and initializes handle to video driver.
	 *
	 * Parameters:
	 *   layer_textures: Array of texture layer configurations
	 *   layer_textures_count: Number of texture layers
	 *   clut_info: CLUT configuration (base address, entries per bank, bank count)
	 *
	 * Returns: video driver handle on success, otherwise NULL.
	 **/
	void *(*init)(layer_texture_info_t *layer_textures, uint8_t layer_textures_count, clut_info_t *clut_info);
	/* Stops and frees driver data. */
   	void (*free)(void *data);
	/* Wait for one presentation refresh without swapping buffers. */
	void (*waitVsync)(void *data);
	/* Present the completed frame. When vsync is true, the backend must wait
	 * for the next presentation boundary when that capability is available. */
	void (*flipScreen)(void *data, bool vsync);
	/* Begin a new rendering frame (e.g. start GPU command list).
	 * All draw calls between beginFrame/endFrame just enqueue commands. */
	void (*beginFrame)(void *data);
	/* End the current rendering frame (e.g. finish and sync GPU command list). */
	void (*endFrame)(void *data);
	void *(*frameAddr)(void *data, int frameIndex, int x, int y);
	/* Optional CPU readback for surfaces that are not directly addressable. */
	int (*readFrame)(void *data, int frameIndex,
		int x, int y, int width, int height, uint16_t *dst, int dstPitch);
	/* Physical presentation size owned by the backend. */
	void (*getOutputSize)(void *data, int *width, int *height);
	void (*scissor)(void *data, uint16_t left, uint16_t top, uint16_t right, uint16_t bottom);
	void (*clearScreen)(void *data);
	void (*clearFrame)(void *data, int index);
	void (*fillFrame)(void *data, int frameIndex, uint32_t color);
	void (*startWorkFrame)(void *data, uint32_t color);
	void (*transferWorkFrame)(void *data, RECT *src_rect, RECT *dst_rect);
	void (*copyRect)(void *data, int srcIndex, int dstIndex, RECT *src_rect, RECT *dst_rect);
	void (*copyRectFlip)(void *data, int srcIndex, int dstIndex, RECT *src_rect, RECT *dst_rect);
	void (*copyRectRotate)(void *data, int srcIndex, int dstIndex, RECT *src_rect, RECT *dst_rect);
	void (*drawTexture)(void *data, int srcIndex, int dstIndex, RECT *src_rect, RECT *dst_rect);
	void (*uploadMem)(void *data, uint8_t textureIndex);
	void (*uploadClut)(void *data, uint16_t *bank, uint8_t bank_index);
	/* Portable indexed-atlas update used by target-common renderers.  x/y/w/h
	 * use logical texture coordinates; the backend owns native/swizzled layout. */
	void (*writeIndexedTextureRect)(void *data, uint8_t textureIndex,
		int x, int y, int width, int height,
		const uint8_t *pixels, int srcPitch);
	/* Portable direct-color texture update. Pixels and srcPitch use 16-bit
	 * texel units; the backend owns native texture layout/upload details. */
	void (*writeDirectTextureRect)(void *data, uint8_t textureIndex,
		int x, int y, int width, int height,
		const uint16_t *pixels, int srcPitch);
	/* Portable sprite batch. Backends translate these stable common vertices
	 * into native GPU commands/vertices where necessary. */
	void (*blitSpriteVertices)(void *data, uint8_t textureIndex,
		const uint16_t *clut, uint8_t bank_index,
		uint32_t vertices_count, const video_sprite_vertex_t *vertices);
	void (*blitPointVertices)(void *data, uint32_t points_count,
		const video_point_vertex_t *vertices);

	/* Depth-test support (used by CPS2 priority masking) */
	void (*enableDepthTest)(void *data);
	void (*disableDepthTest)(void *data);
	void (*clearDepthBuffer)(void *data);
	void (*clearColorBuffer)(void *data);

	/* Low-level 2D UI drawing primitives. Common ui_draw.c owns UI semantics and
	 * the UI texture adapter resolves native texture storage for drawUISprite. */
	void (*drawUISprite)(void *data, void *tex, int tex_format, int tex_swizzled,
	                    int tex_width, int tex_height, int tex_stride,
	                    int su, int sv, int sw, int sh,
	                    int dx, int dy, int dw, int dh, int blend);
	void (*drawUILine)(void *data, int x1, int y1, int x2, int y2, uint32_t color);
	void (*drawUILineGradient)(void *data, int x1, int y1, int x2, int y2,
	                          uint32_t color1, uint32_t color2);
	void (*drawUIRect)(void *data, int x, int y, int w, int h, uint32_t color);
	void (*fillUIRect)(void *data, int x, int y, int w, int h, uint32_t color);
	void (*fillUIRectGradient)(void *data, int x, int y, int w, int h,
	                          uint32_t color1, uint32_t color2, int direction);
	/* UI clipping uses x/y/width/height semantics, unlike the emulator scissor
	 * callback above which uses edge coordinates. */
	void (*setUIScissor)(void *data, int x, int y, int w, int h);

	/* Optional cache-coherency preparation for a contiguous sprite vertex array.
	 * Backends that require CPU/GPU cache synchronization can flush once before
	 * a renderer emits many sub-batches from the same array. */
	void (*prepareSpriteVertices)(void *data, uint32_t vertices_count,
		const video_sprite_vertex_t *vertices);

} video_driver_t;

typedef struct video_backend_choice
{
	int id;
	const char *name;
} video_backend_choice_t;

/* Platform registry used by the global backend setting. A single-backend
 * platform returns one read-only choice; multi-backend builds return all
 * compiled choices plus any policy choice such as Auto. */
int video_backend_choice_count(void);
video_backend_choice_t video_backend_choice_at(int index);
int video_backend_option_available(int id);
void video_backend_select(int id);

extern video_driver_t *video_driver;

extern RECT full_rect;

extern void *video_data;

#endif /* VIDEO_DRIVER_H */
