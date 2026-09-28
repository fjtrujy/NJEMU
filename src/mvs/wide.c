#include "mvs/wide.h"

static const mvs_view_geometry_t native_view =
{
	24, 16, 328, 240,
	24, 16, 336, 240,
	16
};

/* Preserve the central 304x224 image. Hardware X=-40..359 is represented
 * by wrapped 9-bit coordinates with a +80 work-frame bias. One backdrop
 * scanline above the 224 active lines makes the source exactly 16:9. */
static const mvs_view_geometry_t wide_view =
{
	40, 15, 440, 240,
	40, 16, 440, 240,
	80
};

const mvs_view_geometry_t *mvs_view_geometry_for_mode(bool wide)
{
	return wide ? &wide_view : &native_view;
}

void mvs_wide_fit_output(int output_width, int output_height,
	int *width, int *height)
{
	if (output_width <= 0 || output_height <= 0)
	{
		*width = *height = 0;
		return;
	}
	if ((int64_t)output_width * 9 <= (int64_t)output_height * 16)
	{
		*width = output_width;
		*height = (int)((int64_t)output_width * 9 / 16);
	}
	else
	{
		*width = (int)((int64_t)output_height * 16 / 9);
		*height = output_height;
	}
	if (*width == 0) *width = 1;
	if (*height == 0) *height = 1;
}

