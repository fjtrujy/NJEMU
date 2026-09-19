/******************************************************************************

	ui_draw_driver.h

	Cross-platform UI drawing driver interface.

	All 2D GUI rendering (text, icons, lines, rectangles, shadows) goes
	through this driver so that platform-specific GPU code is isolated.

******************************************************************************/

#ifndef COMMON_UI_DRAW_DRIVER_H
#define COMMON_UI_DRAW_DRIVER_H

#include <stdint.h>
#include "common/font_t.h"

/*------------------------------------------------------
	Texture slot identifiers
------------------------------------------------------*/

enum {
	UI_TEXTURE_FONT = 0,       /* Scratch texture for font glyph rendering (4444) */
	UI_TEXTURE_SMALLFONT,      /* Pre-baked 8x8 bitmap font (5551) */
	UI_TEXTURE_BOXSHADOW,      /* 9-slice box shadow tiles (4444) */
	UI_TEXTURE_VOLICON,        /* Volume speaker + bar icons (4444) */
	UI_TEXTURE_MAX
};

/*------------------------------------------------------
	Pixel formats for uploadTexture
------------------------------------------------------*/

enum {
	UI_PIXFMT_4444 = 0,       /* 16-bit RGBA 4444 */
	UI_PIXFMT_5551             /* 16-bit RGBA 5551 */
};

/*------------------------------------------------------
	Gradient direction
------------------------------------------------------*/

enum {
	UI_GRADIENT_HORIZONTAL = 0,
	UI_GRADIENT_VERTICAL
};

/*------------------------------------------------------
	Driver interface
------------------------------------------------------*/

typedef struct ui_draw_driver
{
	/*
	 * init — Allocate platform texture storage for UI.
	 * Called once from ui_init(). Returns opaque driver data.
	 */
	void *(*init)(void *video_data);

	/*
	 * term — Release platform texture storage.
	 */
	void (*term)(void *data);

	/*
	 * uploadTexture — Upload a pixel buffer to a named texture slot.
	 *   slot:    UI_TEXTURE_* enum
	 *   pixels:  source pixel data (16-bit per pixel)
	 *   w, h:    dimensions in pixels
	 *   pitch:   row stride in pixels (may be > w)
	 *   format:  UI_PIXFMT_*
	 *   swizzle: non-zero if platform should store swizzled
	 */
	void (*uploadTexture)(void *data, int slot, const uint16_t *pixels,
	                      int w, int h, int pitch, int format, int swizzle);

	/*
	 * clearTexture — Clear a region of a texture slot to zero.
	 *   slot:  UI_TEXTURE_* enum
	 *   w, h:  region size
	 *   pitch: row stride in pixels
	 */
	void (*clearTexture)(void *data, int slot, int w, int h, int pitch);

	/*
	 * getTextureBasePtr — Get the CPU-writable base address of a texture
	 *   slot for direct pixel manipulation (font glyph building, etc).
	 *   Returns NULL if the platform does not support direct writes.
	 *   On PSP this returns the VRAM pointer; other platforms use a
	 *   staging buffer that is uploaded with uploadTexture afterward.
	 */
	uint16_t *(*getTextureBasePtr)(void *data, int slot);

	/*
	 * drawSprite — Draw a textured rectangle from a texture slot.
	 *   slot:     UI_TEXTURE_* enum
	 *   su,sv:    source top-left in texture
	 *   sw,sh:    source size
	 *   dx,dy:    destination top-left on screen
	 *   dw,dh:    destination size (usually == sw,sh)
	 *   color:    tint color (0xRRGGBBAA), 0xFFFFFFFF = no tint
	 *   blend:    non-zero to enable alpha blending
	 */
	void (*drawSprite)(void *data, int slot,
	                   int su, int sv, int sw, int sh,
	                   int dx, int dy, int dw, int dh,
	                   uint32_t color, int blend);

	/*
	 * drawLine — Draw a single-pixel line.
	 *   color: 0xRRGGBBAA
	 */
	void (*drawLine)(void *data,
	                 int x1, int y1, int x2, int y2,
	                 uint32_t color);

	/*
	 * drawLineGradient — Draw a line with per-endpoint colors.
	 *   color1, color2: 0xRRGGBBAA
	 */
	void (*drawLineGradient)(void *data,
	                         int x1, int y1, int x2, int y2,
	                         uint32_t color1, uint32_t color2);

	/*
	 * drawRect — Draw an unfilled rectangle outline (1px border).
	 *   color: 0xRRGGBBAA
	 */
	void (*drawRect)(void *data,
	                 int x, int y, int w, int h,
	                 uint32_t color);

	/*
	 * fillRect — Draw a filled rectangle.
	 *   color: 0xRRGGBBAA
	 */
	void (*fillRect)(void *data,
	                 int x, int y, int w, int h,
	                 uint32_t color);

	/*
	 * fillRectGradient — Draw a gradient-filled rectangle.
	 *   color1:    start color (0xRRGGBBAA)
	 *   color2:    end color
	 *   direction: UI_GRADIENT_HORIZONTAL or UI_GRADIENT_VERTICAL
	 */
	void (*fillRectGradient)(void *data,
	                         int x, int y, int w, int h,
	                         uint32_t color1, uint32_t color2,
	                         int direction);

	/*
	 * setScissor — Set the clipping rectangle for subsequent draws.
	 *   Pass full screen dimensions to disable clipping.
	 */
	void (*setScissor)(void *data, int x, int y, int w, int h);

} ui_draw_driver_t;


/*------------------------------------------------------
	Platform driver instances
------------------------------------------------------*/

extern const ui_draw_driver_t psp_ui_draw_driver;
extern const ui_draw_driver_t ps2_ui_draw_driver;
extern const ui_draw_driver_t desktop_ui_draw_driver;
extern const ui_draw_driver_t null_ui_draw_driver;

/*------------------------------------------------------
	Global driver array + data (compile-time selected)
------------------------------------------------------*/

extern const ui_draw_driver_t *ui_draw_drivers[];
#define ui_draw_driver ui_draw_drivers[0]

extern void *ui_draw_data;

#endif /* COMMON_UI_DRAW_DRIVER_H */
