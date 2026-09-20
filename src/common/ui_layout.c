/******************************************************************************

	ui_layout.c

	Resolution-independent UI layout metrics.

******************************************************************************/

#include "common/ui_layout.h"

static ui_layout_metrics_t metrics = {
	480, 272,
	480, 272,
	0, 0,
	480, 272,
	1.0f
};

void ui_layout_init(int logical_width, int logical_height,
	int output_width, int output_height)
{
	float scale_x;
	float scale_y;

	if (logical_width <= 0)
		logical_width = 480;
	if (logical_height <= 0)
		logical_height = 272;
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
