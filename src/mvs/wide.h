#ifndef MVS_WIDE_H
#define MVS_WIDE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mvs_view_geometry
{
	int source_left;
	int source_top;
	int source_right;
	int source_bottom;
	int render_left;
	int render_top;
	int render_right;
	int render_bottom;
	int x_bias;
} mvs_view_geometry_t;

const mvs_view_geometry_t *mvs_view_geometry_for_mode(bool wide);
void mvs_wide_fit_output(int output_width, int output_height,
	int *width, int *height);

#endif /* MVS_WIDE_H */
