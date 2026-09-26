/******************************************************************************

	ui_draw_driver.c

	UI draw driver global state and null (stub) driver.

******************************************************************************/

#include "common/ui_draw_driver.h"
#include "common/ui_layout.h"
#include "common/video_driver.h"
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

static void null_getLogicalSize(void *data, int output_width, int output_height,
	int *logical_width, int *logical_height)
{
	(void)data;
	if (logical_width) *logical_width = output_width;
	if (logical_height) *logical_height = output_height;
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

static bool null_prepareTextureDraw(void *data, int slot,
                                    int su, int sv, int sw, int sh,
                                    ui_texture_draw_t *draw)
{
	(void)data; (void)slot;
	(void)su; (void)sv; (void)sw; (void)sh;
	(void)draw;
	return false;
}

static void null_finishTextureDraw(void *data, int slot)
{
	(void)data; (void)slot;
}

/******************************************************************************
	Null driver instance
******************************************************************************/

const ui_draw_driver_t null_ui_draw_driver = {
	null_init,
	null_term,
	null_getLogicalSize,
	0,
	null_uploadTexture,
	null_clearTexture,
	null_getTextureBasePtr,
	null_prepareTextureDraw,
	null_finishTextureDraw,
};

void *ui_draw_data = NULL;

void ui_draw_configure_layout(void)
{
	int output_width = 0;
	int output_height = 0;
	int logical_width = 0;
	int logical_height = 0;

	video_driver->getOutputSize(video_data, &output_width, &output_height);
	ui_draw_driver->getLogicalSize(ui_draw_data, output_width, output_height,
		&logical_width, &logical_height);
	ui_layout_init(logical_width, logical_height, output_width, output_height);
}

int ui_draw_has_capability(uint32_t capability)
{
	return (ui_draw_driver->capabilities & capability) != 0;
}
