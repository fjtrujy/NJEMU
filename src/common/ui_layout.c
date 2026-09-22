/******************************************************************************

	ui_layout.c

	Resolution-independent UI layout metrics.

******************************************************************************/

#include "common/ui_layout.h"

static ui_layout_metrics_t metrics = {
	UI_LAYOUT_BASE_WIDTH, UI_LAYOUT_BASE_HEIGHT,
	UI_LAYOUT_BASE_WIDTH, UI_LAYOUT_BASE_HEIGHT,
	0, 0,
	UI_LAYOUT_BASE_WIDTH, UI_LAYOUT_BASE_HEIGHT,
	1.0f
};

void ui_layout_init(int logical_width, int logical_height,
	int output_width, int output_height)
{
	float scale_x;
	float scale_y;

	if (logical_width <= 0)
		logical_width = UI_LAYOUT_BASE_WIDTH;
	if (logical_height <= 0)
		logical_height = UI_LAYOUT_BASE_HEIGHT;
	if (output_width <= 0)
		output_width = logical_width;
	if (output_height <= 0)
		output_height = logical_height;

	metrics.logical_width = logical_width;
	metrics.logical_height = logical_height;
	metrics.output_width = output_width;
	metrics.output_height = output_height;

	scale_x = (float)output_width / (float)logical_width;
	scale_y = (float)output_height / (float)logical_height;
	metrics.scale = scale_x < scale_y ? scale_x : scale_y;

	metrics.viewport_width =
		(int)((float)logical_width * metrics.scale + 0.5f);
	metrics.viewport_height =
		(int)((float)logical_height * metrics.scale + 0.5f);
	metrics.viewport_x = (output_width - metrics.viewport_width) / 2;
	metrics.viewport_y = (output_height - metrics.viewport_height) / 2;
}

void ui_layout_init_responsive(int output_width, int output_height)
{
#if defined(PS2)
	/* PS2 already exposes the native GS presentation size (normally 640x448
	 * NTSC or 640x512 PAL). Keep UI pixels 1:1 there: scaling the legacy
	 * 480x272 canvas made the 14px bitmap font land at fractional ~19px sizes
	 * and was the main reason text looked soft. Edge/center helpers still make
	 * the layout responsive because logical coordinates now are the output
	 * coordinates themselves. */
	ui_layout_init(output_width, output_height, output_width, output_height);
	return;
#else
	float scale_x;
	float scale_y;
	float scale;
	int logical_width;
	int logical_height;

	if (output_width <= 0)
		output_width = UI_LAYOUT_BASE_WIDTH;
	if (output_height <= 0)
		output_height = UI_LAYOUT_BASE_HEIGHT;

	/* 480x272 is the minimum design canvas, not a fixed screen size. Scale the
	 * baseline uniformly until one output axis is filled, then expose any extra
	 * space on the other axis as additional logical layout room. This keeps the
	 * PSP layout pixel-identical while making legacy center coordinates scale
	 * naturally on PS2/Desktop and still lets edge-anchored UI reflow. */
	scale_x = (float)output_width / (float)UI_LAYOUT_BASE_WIDTH;
	scale_y = (float)output_height / (float)UI_LAYOUT_BASE_HEIGHT;
	scale = scale_x < scale_y ? scale_x : scale_y;
	if (scale <= 0.0f)
		scale = 1.0f;

	logical_width = (int)((float)output_width / scale + 0.9999f);
	logical_height = (int)((float)output_height / scale + 0.9999f);
	if (logical_width < UI_LAYOUT_BASE_WIDTH)
		logical_width = UI_LAYOUT_BASE_WIDTH;
	if (logical_height < UI_LAYOUT_BASE_HEIGHT)
		logical_height = UI_LAYOUT_BASE_HEIGHT;

	/* Reuse the normal aspect-preserving transform so integer rounding can never
	 * make the logical canvas overdraw the physical output. Arbitrary aspect
	 * ratios may leave a single pixel of unused space on one axis. */
	ui_layout_init(logical_width, logical_height, output_width, output_height);
#endif
}

const ui_layout_metrics_t *ui_layout_get(void)
{
	return &metrics;
}

static int scaled_coord(int value)
{
	float scaled = (float)value * metrics.scale;
	return (int)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

int ui_layout_uses_output_transform(void)
{
	return metrics.viewport_x != 0 ||
		metrics.viewport_y != 0 ||
		metrics.viewport_width != metrics.logical_width ||
		metrics.viewport_height != metrics.logical_height;
}

void ui_layout_transform_point(int x, int y, int *out_x, int *out_y)
{
	if (out_x)
		*out_x = metrics.viewport_x + scaled_coord(x);
	if (out_y)
		*out_y = metrics.viewport_y + scaled_coord(y);
}

void ui_layout_transform_rect(int x, int y, int w, int h,
	int *out_x, int *out_y, int *out_w, int *out_h)
{
	int x0 = metrics.viewport_x + scaled_coord(x);
	int y0 = metrics.viewport_y + scaled_coord(y);
	int x1 = metrics.viewport_x + scaled_coord(x + w);
	int y1 = metrics.viewport_y + scaled_coord(y + h);

	if (out_x) *out_x = x0;
	if (out_y) *out_y = y0;
	if (out_w) *out_w = x1 - x0;
	if (out_h) *out_h = y1 - y0;
}
