/******************************************************************************

	ui_draw_driver.c

	UI draw driver global state and null (stub) driver.

******************************************************************************/

#include "common/ui_draw_driver.h"
#include <stddef.h>

/******************************************************************************
	Null Driver — used when GUI is not set or before platform init
******************************************************************************/

static void *null_init(void *video_data)
{
	return NULL;
}

static void null_term(void *data)
{
	(void)data;
}

static void null_uploadTexture(void *data, int slot, const uint16_t *pixels,
                               int w, int h, int pitch, int format, int swizzle)
{
	(void)data; (void)slot; (void)pixels;
	(void)w; (void)h; (void)pitch; (void)format; (void)swizzle;
}

static void null_clearTexture(void *data, int slot, int w, int h, int pitch)
{
	(void)data; (void)slot; (void)w; (void)h; (void)pitch;
}

static uint16_t *null_getTextureBasePtr(void *data, int slot)
{
	(void)data; (void)slot;
	return NULL;
}

static void null_drawSprite(void *data, int slot,
                            int su, int sv, int sw, int sh,
                            int dx, int dy, int dw, int dh,
                            uint32_t color, int blend)
{
	(void)data; (void)slot;
	(void)su; (void)sv; (void)sw; (void)sh;
	(void)dx; (void)dy; (void)dw; (void)dh;
	(void)color; (void)blend;
}

static void null_drawLine(void *data,
                          int x1, int y1, int x2, int y2,
                          uint32_t color)
{
	(void)data;
	(void)x1; (void)y1; (void)x2; (void)y2;
	(void)color;
}

static void null_drawLineGradient(void *data,
                                  int x1, int y1, int x2, int y2,
                                  uint32_t color1, uint32_t color2)
{
	(void)data;
	(void)x1; (void)y1; (void)x2; (void)y2;
	(void)color1; (void)color2;
}

static void null_drawRect(void *data,
                          int x, int y, int w, int h,
                          uint32_t color)
{
	(void)data;
	(void)x; (void)y; (void)w; (void)h;
	(void)color;
}

static void null_fillRect(void *data,
                          int x, int y, int w, int h,
                          uint32_t color)
{
	(void)data;
	(void)x; (void)y; (void)w; (void)h;
	(void)color;
}

static void null_fillRectGradient(void *data,
                                  int x, int y, int w, int h,
                                  uint32_t color1, uint32_t color2,
                                  int direction)
{
	(void)data;
	(void)x; (void)y; (void)w; (void)h;
	(void)color1; (void)color2; (void)direction;
}

static void null_setScissor(void *data, int x, int y, int w, int h)
{
	(void)data;
	(void)x; (void)y; (void)w; (void)h;
}

/******************************************************************************
	Null driver instance
******************************************************************************/

const ui_draw_driver_t null_ui_draw_driver = {
	null_init,
	null_term,
	null_uploadTexture,
	null_clearTexture,
	null_getTextureBasePtr,
	null_drawSprite,
	null_drawLine,
	null_drawLineGradient,
	null_drawRect,
	null_fillRect,
	null_fillRectGradient,
	null_setScissor,
};

/******************************************************************************
	Driver selection — compile-time array, same pattern as video_driver
******************************************************************************/

const ui_draw_driver_t *ui_draw_drivers[] = {
#if defined(GUI)
#  if defined(PSP)
	&psp_ui_draw_driver,
#  elif defined(PS2)
	&ps2_ui_draw_driver,
#  elif defined(DESKTOP)
	&desktop_ui_draw_driver,
#  endif
#endif
	&null_ui_draw_driver,
	NULL,
};

void *ui_draw_data = NULL;
