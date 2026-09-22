/******************************************************************************

	ps2.h


******************************************************************************/

#ifndef PS2_MAIN_H
#define PS2_MAIN_H

#include <stdint.h>

#define SCR_WIDTH			480
#define SCR_HEIGHT			272
#define BUF_WIDTH			512

#define REFRESH_RATE		(59.940059)		// (9000000Hz * 1) / (525 * 286)

#define FONTSIZE			14

/* Scale a size expressed in the legacy 480x272 presentation space to the
 * largest uniform size that fits the active PS2 output. This keeps the
 * stretch presets portable while preserving their aspect ratio. */
static inline void ps2_scale_logical_size(int output_width, int output_height,
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

/* Accessor used by ps2_ui_draw.c to reach gsGlobal inside ps2_video.c's
 * private ps2_video_t struct without exposing the full definition. The
 * caller casts to GSGLOBAL*. */
void *ps2_video_get_gsGlobal(void *video_data);

/* Copy a CT16 GS surface into CPU RAM.  dst_pitch is expressed in pixels.
 * This is intentionally PS2-specific: GS frame buffers live in local VRAM
 * and cannot be exposed safely through video_driver->frameAddr(). */
int ps2_video_read_frame(void *video_data, int frame_index,
	int x, int y, int width, int height,
	uint16_t *dst, int dst_pitch);

#endif /* PS2_MAIN_H */
