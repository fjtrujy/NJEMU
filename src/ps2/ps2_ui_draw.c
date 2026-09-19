/******************************************************************************

	ps2_ui_draw.c

	PS2 implementation of ui_draw_driver_t.
	Manages GS VRAM texture slots and delegates rendering to video_driver.

	Architecture:
	- Allocate 4 textures (512x512 each @ 16-bit) in GS VRAM via gsKit
	- Store pixel data in CPU RAM as staging buffers
	- Upload to VRAM on demand when texture changes
	- All rendering delegates to video_driver UI functions

******************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gsKit.h>
#include <gsInline.h>
#include <gsToolkit.h>
#include <dmaKit.h>
#include "ps2/ps2.h"
#include "common/ui_draw_driver.h"
#include "common/video_driver.h"

/* Mirror gs_enable/disable_alpha_blend from ps2_video.c so this TU can
 * toggle alpha blending without round-tripping through the video driver. */
#define UI_GS_ALPHA_BLEND  GS_SETREG_ALPHA(0, 1, 0, 1, 0)
#define ui_enable_alpha_blend(g)   gsKit_set_primalpha((g), UI_GS_ALPHA_BLEND, 0)


/******************************************************************************
	Texture management
******************************************************************************/

typedef struct ps2_ui_texture {
	GSTEXTURE texture;          /* GS texture object (CT32 / ABGR8888) */
	uint16_t *buffer;           /* CPU-side staging buffer (16-bit ABGR4444 or ABGR1555) */
	uint32_t *upload_buffer;    /* 32-bit ABGR8888 buffer used to feed the GS */
	int width, height;
	int pitch;                  /* Row stride in pixels */
	int format;                 /* UI_PIXFMT_4444 or UI_PIXFMT_5551 */
	int buffer_valid;           /* 1 if buffer has valid data */
	int vram_valid;             /* 1 if VRAM texture is up-to-date */
} ps2_ui_texture_t;

typedef struct ps2_ui_data {
	void *video_data;
	GSGLOBAL *gsGlobal;
	ps2_ui_texture_t textures[UI_TEXTURE_MAX];
} ps2_ui_data_t;

static ps2_ui_data_t ps2_ui;

static int ensure_vram(ps2_ui_texture_t *tex);


/******************************************************************************
	Helpers
******************************************************************************/

/******************************************************************************
	Driver interface implementation
******************************************************************************/

/*------------------------------------------------------
	Init / Term
------------------------------------------------------*/

static void *ps2_ui_draw_init(void *video_data)
{
	int i;

	memset(&ps2_ui, 0, sizeof(ps2_ui_data_t));
	ps2_ui.video_data = video_data;
	ps2_ui.gsGlobal = (GSGLOBAL *)ps2_video_get_gsGlobal(video_data);

	/* Initialize texture slots with a 512x512 CPU staging buffer each. VRAM
	 * is allocated *lazily* by ensure_vram() once we know the real width/
	 * height; the GS has only ~4 MB total and pre-allocating four 512x512
	 * 16-bit textures would burn 2 MB we don't have after the framebuffer
	 * and game-sprite atlases stake their claim. The ui_draw layer always
	 * tells us the meaningful w/h via clearTexture or uploadTexture before
	 * the first drawSprite. */
	for (i = 0; i < UI_TEXTURE_MAX; i++)
	{
		ps2_ui_texture_t *tex = &ps2_ui.textures[i];
		GSTEXTURE *gst = &tex->texture;

		tex->width = 0;
		tex->height = 0;
		tex->pitch = 512;
		tex->format = UI_PIXFMT_4444;
		tex->buffer_valid = 0;
		tex->vram_valid = 0;

		tex->buffer = (uint16_t *)memalign(64, 512 * 512 * 2);
		if (!tex->buffer)
			return NULL;
		memset(tex->buffer, 0, 512 * 512 * 2);

		/* upload_buffer is the 32-bit ABGR8888 version we hand to the GS.
		 * The PS2 has no native 4444 format, so we expand from tex->buffer
		 * at upload time. Lazily allocated by ensure_vram(). */
		tex->upload_buffer = NULL;

		memset(gst, 0, sizeof(GSTEXTURE));
		gst->PSM = GS_PSM_CT32;
		gst->Filter = GS_FILTER_NEAREST;
		gst->Mem = NULL;        /* Filled when upload_buffer is allocated */
		gst->Vram = 0;          /* Filled by ensure_vram() */
	}

	/* The big-font slot is updated through getTextureBasePtr (no size hint
	 * comes through clearTexture/uploadTexture), so seed it with the same
	 * 512x48 the Desktop reference uses for the font scratch area. */
	ps2_ui.textures[UI_TEXTURE_FONT].width  = 512;
	ps2_ui.textures[UI_TEXTURE_FONT].height = 48;
	ensure_vram(&ps2_ui.textures[UI_TEXTURE_FONT]);

	return &ps2_ui;
}

/* Round w/h up to the next gsKit-friendly size (multiple of 8 minimum). */
static int round_tex_dim(int v) { return (v + 7) & ~7; }

static int ensure_vram(ps2_ui_texture_t *tex)
{
	if (!ps2_ui.gsGlobal) return 0;
	if (tex->width == 0 || tex->height == 0) return 0;

	int want_w = round_tex_dim(tex->width);
	int want_h = round_tex_dim(tex->height);

	if (tex->texture.Vram == 0 ||
	    tex->texture.Width  < want_w ||
	    tex->texture.Height < want_h)
	{
		tex->texture.Width  = want_w;
		tex->texture.Height = want_h;
		tex->texture.Vram = gsKit_vram_alloc(ps2_ui.gsGlobal,
			gsKit_texture_size(want_w, want_h, tex->texture.PSM),
			GSKIT_ALLOC_USERBUFFER);
		gsKit_setup_tbw(&tex->texture);

		/* (Re)allocate the 32-bit upload staging buffer. */
		if (tex->upload_buffer)
			free(tex->upload_buffer);
		tex->upload_buffer = (uint32_t *)memalign(64,
			(size_t)want_w * (size_t)want_h * 4);
		if (tex->upload_buffer)
		{
			memset(tex->upload_buffer, 0,
				(size_t)want_w * (size_t)want_h * 4);
			tex->texture.Mem = (u32 *)tex->upload_buffer;
		}
		tex->vram_valid = 0;
	}
	return tex->texture.Vram != 0 && tex->upload_buffer != NULL;
}

/* Convert one row of 16-bit ABGR4444 / ABGR1555 to 32-bit ABGR8888. */
static void convert_row_to_8888(uint32_t *dst, const uint16_t *src, int w, int format)
{
	int x;
	if (format == UI_PIXFMT_4444)
	{
		for (x = 0; x < w; x++)
		{
			uint16_t p = src[x];
			/* The GS uses 0x80 as 1.0 alpha for blending.  Expanding the
			 * 4-bit UI alpha to 0xFF makes source alpha almost 2.0, which
			 * produces dark/inverted-looking glyphs over the menu. */
			uint8_t a = (uint8_t)((((p >> 12) & 0xF) * 0x80) / 0xF);
			uint8_t b = ((p >> 8)  & 0xF) * 17;
			uint8_t g = ((p >> 4)  & 0xF) * 17;
			uint8_t r =  (p        & 0xF) * 17;
			/* GS CT32 layout: bytes are R, G, B, A (low->high). */
			dst[x] = ((uint32_t)a << 24) | ((uint32_t)b << 16)
			       | ((uint32_t)g << 8)  |  (uint32_t)r;
		}
	}
	else  /* UI_PIXFMT_5551 */
	{
		for (x = 0; x < w; x++)
		{
			uint16_t p = src[x];
			/* The UI's 5551 atlas uses bit 15 as a transparency marker:
			 * empty pixels are written as 0x8000 while glyph pixels come from
			 * MAKECOL15() with bit 15 clear. */
			uint8_t a = (p & 0x8000) ? 0 : 0x80;
			uint8_t b = (((p >> 10) & 0x1F) * 255) / 31;
			uint8_t g = (((p >> 5)  & 0x1F) * 255) / 31;
			uint8_t r = (( p        & 0x1F) * 255) / 31;
			dst[x] = ((uint32_t)a << 24) | ((uint32_t)b << 16)
			       | ((uint32_t)g << 8)  |  (uint32_t)r;
		}
	}
}

static void expand_buffer_to_upload(ps2_ui_texture_t *tex)
{
	if (!tex->upload_buffer || !tex->buffer) return;

	int w = tex->texture.Width;
	int h = tex->width  ? tex->height : 0;  /* sanity */
	int copy_h = tex->height < tex->texture.Height ? tex->height : tex->texture.Height;
	int copy_w = tex->width  < tex->texture.Width  ? tex->width  : tex->texture.Width;
	int y;

	for (y = 0; y < copy_h; y++)
	{
		convert_row_to_8888(
			tex->upload_buffer + (size_t)y * w,
			tex->buffer        + (size_t)y * tex->pitch,
			copy_w, tex->format);
		/* Zero-fill any padding to the rounded width. */
		if (copy_w < w)
			memset(tex->upload_buffer + (size_t)y * w + copy_w, 0,
				(w - copy_w) * 4);
	}
	(void)h;
}

static void ps2_ui_draw_term(void *data)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	int i;

	for (i = 0; i < UI_TEXTURE_MAX; i++)
	{
		if (d->textures[i].buffer)
			free(d->textures[i].buffer);
		if (d->textures[i].upload_buffer)
			free(d->textures[i].upload_buffer);
		/* VRAM cleanup is handled by gsKit */
	}
}

/*------------------------------------------------------
	Texture management
------------------------------------------------------*/

static void ps2_ui_draw_uploadTexture(void *data, int slot,
	const uint16_t *pixels, int w, int h, int pitch, int format, int swizzle)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	ps2_ui_texture_t *tex;
	int x, y;
	uint16_t *src, *dst;

	if (slot >= UI_TEXTURE_MAX)
		return;

	tex = &d->textures[slot];

	tex->format = format;
	tex->width = w;
	tex->height = h;
	tex->pitch = pitch;

	if (pixels)
	{
		for (y = 0; y < h; y++)
		{
			src = (uint16_t *)pixels + y * pitch;
			dst = tex->buffer + y * pitch;
			memcpy(dst, src, w * sizeof(uint16_t));
		}
		tex->buffer_valid = 1;
		tex->vram_valid = 0;
	}
	ensure_vram(tex);
}

static void ps2_ui_draw_clearTexture(void *data, int slot, int w, int h, int pitch)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	ps2_ui_texture_t *tex;
	int x, y;

	if (slot >= UI_TEXTURE_MAX)
		return;

	tex = &d->textures[slot];

	for (y = 0; y < h; y++)
		memset(tex->buffer + y * pitch, 0, w * sizeof(uint16_t));

	tex->width = w;
	tex->height = h;
	tex->pitch = pitch;
	tex->buffer_valid = 1;
	tex->vram_valid = 0;
	ensure_vram(tex);
}

static uint16_t *ps2_ui_draw_getTextureBasePtr(void *data, int slot)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;

	if (slot >= UI_TEXTURE_MAX)
		return NULL;

	/* The caller (ui_draw.c make_font_texture etc.) will write directly to
	 * this buffer, so VRAM is now stale. */
	d->textures[slot].buffer_valid = 1;
	d->textures[slot].vram_valid = 0;
	return d->textures[slot].buffer;
}

/*------------------------------------------------------
	Drawing primitives (delegate to video_driver)
------------------------------------------------------*/

static void ps2_ui_draw_drawSprite(void *data, int slot,
	int su, int sv, int sw, int sh,
	int dx, int dy, int dw, int dh,
	uint32_t color, int blend)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	ps2_ui_texture_t *tex;
	GSTEXTURE *gst;
	GSGLOBAL *gsGlobal = d->gsGlobal;
	int prev_alpha_enable;
	int prev_alpha_test;
	u64 prev_alpha_mode;
	u8 prev_pabe;

	if (slot >= UI_TEXTURE_MAX || !gsGlobal)
		return;

	tex = &d->textures[slot];
	gst = &tex->texture;
	if (!gst->Vram || !tex->buffer)
		return;

	/* Lazy upload: any path that mutates the CPU buffer (uploadTexture,
	 * clearTexture, getTextureBasePtr) clears vram_valid. Expand the
	 * 16-bit ABGR4444/1555 staging buffer into 32-bit ABGR8888 (PS2 has
	 * no native 4444 format) and push it over GIF. */
	/* UI_TEXTURE_FONT is a scratch texture: common/ui_draw.c obtains its CPU
	 * pointer once and rewrites it directly for every glyph/shadow.  There is
	 * no callback that can invalidate vram_valid after those writes, so it must
	 * be uploaded on every draw (same policy as the Desktop backend). */
	if (tex->buffer_valid && (!tex->vram_valid || slot == UI_TEXTURE_FONT))
	{
		expand_buffer_to_upload(tex);
		gsKit_texture_send_inline(gsGlobal, gst->Mem,
			gst->Width, gst->Height, gst->Vram,
			gst->PSM, gst->TBW, GS_CLUT_NONE);
		tex->vram_valid = 1;
	}

	/* Modulation tint for textured sprites. Most callers (font, icons) pass
	 * 0xFFFFFFFF for pass-through white; the texture itself carries the
	 * actual colors via make_font_texture. Use the gsKit-canonical 0x80 =
	 * identity in each channel so MODULATE doesn't darken or saturate. */
	(void)color;
	u64 gs_color = GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0);

	prev_alpha_enable = gsGlobal->PrimAlphaEnable;
	prev_alpha_test = gsGlobal->Test->ATE;
	prev_alpha_mode = gsGlobal->PrimAlpha;
	prev_pabe = gsGlobal->PABE;

	/* gsKit uses PrimAlphaEnable both for PRIM.ABE and TEX0.TCC.  Keep both
	 * enabled only for sprites that actually request alpha blending; opaque
	 * copies should neither blend nor consume texture alpha. */
	gsGlobal->PrimAlphaEnable = blend ? GS_SETTING_ON : GS_SETTING_OFF;

	/* Game rendering uses TEQUAL/AREF=0 for indexed CT16 textures.  UI CT32
	 * textures use their own alpha values, so keep the global alpha test out of
	 * UI sprite evaluation.  Transparent UI sprites are handled by blending. */
	gsKit_set_test(gsGlobal, GS_ATEST_OFF);

	if (blend)
		ui_enable_alpha_blend(gsGlobal);

	gsKit_prim_sprite_texture(gsGlobal, gst,
		(float)dx,         (float)dy,         (float)su,         (float)sv,
		(float)(dx + dw),  (float)(dy + dh),  (float)(su + sw),  (float)(sv + sh),
		0,                 gs_color);

	gsGlobal->PrimAlphaEnable = prev_alpha_enable;

	if (blend)
		gsKit_set_primalpha(gsGlobal, prev_alpha_mode, prev_pabe);

	gsKit_set_test(gsGlobal, prev_alpha_test ? GS_ATEST_ON : GS_ATEST_OFF);

	/* The font scratch buffer is rewritten between glyphs (always at UV 0,0),
	 * so its upload+draw must complete before the next glyph overwrites the
	 * same VRAM area. Static UI atlases do not need this per-sprite stall. */
	if (slot == UI_TEXTURE_FONT) {
		gsKit_queue_exec(gsGlobal);
		gsKit_finish();
	}
}

static void ps2_ui_draw_drawLine(void *data,
	int x1, int y1, int x2, int y2,
	uint32_t color)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	video_driver->drawUILine(d->video_data, x1, y1, x2, y2, color);
}

static void ps2_ui_draw_drawLineGradient(void *data,
	int x1, int y1, int x2, int y2,
	uint32_t color1, uint32_t color2)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	video_driver->drawUILineGradient(d->video_data, x1, y1, x2, y2, color1, color2);
}

static void ps2_ui_draw_drawRect(void *data,
	int x, int y, int w, int h,
	uint32_t color)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	video_driver->drawUIRect(d->video_data, x, y, w, h, color);
}

static void ps2_ui_draw_fillRect(void *data,
	int x, int y, int w, int h,
	uint32_t color)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	video_driver->fillUIRect(d->video_data, x, y, w, h, color);
}

static void ps2_ui_draw_fillRectGradient(void *data,
	int x, int y, int w, int h,
	uint32_t color1, uint32_t color2,
	int direction)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	video_driver->fillUIRectGradient(d->video_data, x, y, w, h, color1, color2, direction);
}

static void ps2_ui_draw_setScissor(void *data, int x, int y, int w, int h)
{
	ps2_ui_data_t *d = (ps2_ui_data_t *)data;
	GSGLOBAL *gsGlobal = d->gsGlobal;
	int left, top, right, bottom;

	if (!gsGlobal || w <= 0 || h <= 0)
		return;

	/* UI rectangles use x/y/width/height while the GS SCISSOR register uses
	 * inclusive min/max coordinates. */
	left = x < 0 ? 0 : x;
	top = y < 0 ? 0 : y;
	right = x + w - 1;
	bottom = y + h - 1;
	if (right >= SCR_WIDTH) right = SCR_WIDTH - 1;
	if (bottom >= SCR_HEIGHT) bottom = SCR_HEIGHT - 1;
	if (left > right || top > bottom)
		return;

	gsKit_set_scissor(gsGlobal,
		GS_SETREG_SCISSOR(left, right, top, bottom));
}


/******************************************************************************
	Driver instance
******************************************************************************/

const ui_draw_driver_t ps2_ui_draw_driver = {
	ps2_ui_draw_init,
	ps2_ui_draw_term,
	ps2_ui_draw_uploadTexture,
	ps2_ui_draw_clearTexture,
	ps2_ui_draw_getTextureBasePtr,
	ps2_ui_draw_drawSprite,
	ps2_ui_draw_drawLine,
	ps2_ui_draw_drawLineGradient,
	ps2_ui_draw_drawRect,
	ps2_ui_draw_fillRect,
	ps2_ui_draw_fillRectGradient,
	ps2_ui_draw_setScissor,
};
