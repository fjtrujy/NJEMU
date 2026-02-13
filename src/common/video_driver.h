/******************************************************************************

	power_driver.h

******************************************************************************/

#ifndef VIDEO_DRIVER_H
#define VIDEO_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

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

#define GU_FRAME_ADDR(frame)		(uint16_t *)((uint32_t)frame | 0x44000000)
#define CNVCOL15TO32(c)				(GETR15(c) | (GETG15(c) << 8) | (GETB15(c) << 16))
#define CNVCOL32TO15(c)				(((GETR32(c) & 0xf8) >> 3) | ((GETG32(c) & 0xf8) << 2) | ((GETB32(src[x]) & 0xf8) << 7))

#define SWIZZLED_8x8(tex, idx)		&tex[(idx) << 6]
#define SWIZZLED_16x16(tex, idx)	&tex[((idx & ~31) << 8) | ((idx & 31) << 7)]
#define SWIZZLED_32x32(tex, idx)	&tex[((idx & ~15) << 10) | ((idx & 15) << 8)]

#define NONE_SWIZZLED_8x8(tex, idx)		&tex[((idx & ~63) << 6) | ((idx & 63) << 3)]
#define NONE_SWIZZLED_16x16(tex, idx)	&tex[((idx & ~31) << 8) | ((idx & 31) << 4)]
#define NONE_SWIZZLED_32x32(tex, idx)	&tex[((idx & ~15) << 10) | ((idx & 15) << 5)]

#define SWIZZLED8_8x8(tex, idx)		&tex[((idx & ~1) << 6) | ((idx & 1) << 3)]
#define SWIZZLED8_16x16(tex, idx)	&tex[((idx & ~31) << 8) | ((idx & 31) << 7)]
#define SWIZZLED8_32x32(tex, idx)	&tex[((idx & ~15) << 10) | ((idx & 15) << 8)]

struct Vertex
{
	uint16_t u, v;
	uint16_t color;
	int16_t x, y, z;
};

struct PointVertex
{
	uint16_t color;
	int16_t x, y, z;
};

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
	void (*waitVsync)(void *data);
	void (*flipScreen)(void *data, bool vsync);
	/* Begin a new rendering frame (e.g. start GPU command list).
	 * All draw calls between beginFrame/endFrame just enqueue commands. */
	void (*beginFrame)(void *data);
	/* End the current rendering frame (e.g. finish and sync GPU command list). */
	void (*endFrame)(void *data);
	void *(*frameAddr)(void *data, int frameIndex, int x, int y);
	void *(*workFrame)(void *data);
	void *(*drawFrame)(void *data);
	void *(*textureLayer)(void *data, uint8_t layerIndex);
	void (*scissor)(void *data, uint16_t left, uint16_t top, uint16_t right, uint16_t bottom);
	void (*clearScreen)(void *data);
	void (*clearFrame)(void *data, int index);
	void (*fillFrame)(void *data, void *frame, uint32_t color);
	void (*startWorkFrame)(void *data, uint32_t color);
	void (*transferWorkFrame)(void *data, RECT *src_rect, RECT *dst_rect);
	void (*copyRect)(void *data, int srcIndex, int dstIndex, RECT *src_rect, RECT *dst_rect);
	void (*copyRectFlip)(void *data, int srcIndex, int dstIndex, RECT *src_rect, RECT *dst_rect);
	void (*copyRectRotate)(void *data, int srcIndex, int dstIndex, RECT *src_rect, RECT *dst_rect);
	void (*drawTexture)(void *data, uint32_t src_fmt, uint32_t dst_fmt, void *src, void *dst, RECT *src_rect, RECT *dst_rect);
	void *(*getNativeObjects)(void *data, int index);
	void (*uploadMem)(void *data, uint8_t textureIndex);
	void (*uploadClut)(void *data, uint16_t *bank, uint8_t bank_index);
	void (*blitTexture)(void *data, uint8_t textureIndex, void *clut, uint8_t bank_index, uint32_t vertices_count, void *vertices);
	void (*blitPoints)(void *data, uint32_t points_count, void *vertices);
	void (*flushCache)(void *data, void *addr, size_t size);

	/* Depth-test support (used by CPS2 priority masking) */
	void (*enableDepthTest)(void *data);
	void (*disableDepthTest)(void *data);
	void (*clearDepthBuffer)(void *data);
	void (*clearColorBuffer)(void *data);

} video_driver_t;

extern int platform_cpuclock;

extern video_driver_t video_psp;
extern video_driver_t video_ps2;
extern video_driver_t video_desktop;
extern video_driver_t video_null;

extern video_driver_t *video_drivers[];

#define video_driver video_drivers[0]

extern RECT full_rect;

extern void *video_data;

#endif /* VIDEO_DRIVER_H */