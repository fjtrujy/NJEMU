/******************************************************************************

	ui_draw_driver.h

	Cross-platform UI texture/presentation adapter.

	Common UI semantics live in ui_draw.c and low-level drawing is owned by
	video_driver_t. This adapter only owns platform-specific UI texture storage,
	presentation policy/capabilities, and texture lifetime synchronization.

******************************************************************************/

#ifndef COMMON_UI_DRAW_DRIVER_H
#define COMMON_UI_DRAW_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

/*------------------------------------------------------
	Texture slot identifiers
------------------------------------------------------*/

enum {
	UI_TEXTURE_FONT = 0,       /* Scratch texture for font glyph rendering (4444) */
	UI_TEXTURE_SMALLFONT,      /* Pre-baked 8x8 bitmap font (5551) */
	UI_TEXTURE_BOXSHADOW,      /* 9-slice box shadow tiles (4444) */
	UI_TEXTURE_MAX
};

/*------------------------------------------------------
	Pixel formats for uploadTexture
------------------------------------------------------*/

enum {
	UI_PIXFMT_4444 = 0,       /* 16-bit RGBA 4444 */
	UI_PIXFMT_5551             /* 16-bit RGBA 5551 */
};

typedef struct ui_texture_draw
{
	void *texture;
	int format;
	int swizzled;
	int width;
	int height;
	int stride;
} ui_texture_draw_t;

/*------------------------------------------------------
	Backend capabilities
------------------------------------------------------*/

enum {
	UI_DRAW_CAP_CACHE_CHROME        = 1u << 0,
	UI_DRAW_CAP_TRANSLUCENT_CHROME  = 1u << 1,
	UI_DRAW_CAP_FILTERED_SHADOWS    = 1u << 2,
	UI_DRAW_CAP_ANIMATED_GLOW       = 1u << 3,
	UI_DRAW_CAP_PARTIAL_REFRESH     = 1u << 4
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

	/* getLogicalSize — Select the logical UI canvas for a physical output.
	 * Backends can choose native 1:1 coordinates or a scaled logical canvas
	 * without leaking platform checks into common UI code. */
	void (*getLogicalSize)(void *data, int output_width, int output_height,
	                      int *logical_width, int *logical_height);

	/* Rendering/style capabilities used by common UI policy. */
	uint32_t capabilities;

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

	/* Resolve a UI texture slot into the native texture consumed by the video
	 * backend. Backends may upload/flush mutable staging data here. */
	bool (*prepareTextureDraw)(void *data, int slot,
		int su, int sv, int sw, int sh, ui_texture_draw_t *draw);

	/* Complete any synchronization required after the video backend enqueues the
	 * draw (for example PSP mutable scratch or the PS2 glyph ring). */
	void (*finishTextureDraw)(void *data, int slot);

} ui_draw_driver_t;


extern const ui_draw_driver_t *const ui_draw_driver;

extern void *ui_draw_data;

void ui_draw_configure_layout(void);
int ui_draw_has_capability(uint32_t capability);

#endif /* COMMON_UI_DRAW_DRIVER_H */
