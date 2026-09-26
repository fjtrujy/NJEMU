#ifndef COMMON_VIDEO_GEOMETRY_H
#define COMMON_VIDEO_GEOMETRY_H

#include <stdint.h>

/* NJEMU's logical presentation space is shared by every backend. Physical
 * output dimensions remain platform/backend-owned. */
#define SCR_WIDTH 480
#define SCR_HEIGHT 272
#define BUF_WIDTH 512

/* Historical PSP refresh cadence used by the common frame limiter. */
#define REFRESH_RATE (59.940059)

static inline void video_scale_logical_size(int output_width, int output_height,
	int logical_width, int logical_height, int *scaled_width, int *scaled_height)
{
	if ((int64_t)output_width * SCR_HEIGHT <=
	    (int64_t)output_height * SCR_WIDTH)
	{
		*scaled_width = (logical_width * output_width + SCR_WIDTH / 2) / SCR_WIDTH;
		*scaled_height = (logical_height * output_width + SCR_WIDTH / 2) / SCR_WIDTH;
	}
	else
	{
		*scaled_width = (logical_width * output_height + SCR_HEIGHT / 2) / SCR_HEIGHT;
		*scaled_height = (logical_height * output_height + SCR_HEIGHT / 2) / SCR_HEIGHT;
	}
}

#endif /* COMMON_VIDEO_GEOMETRY_H */
