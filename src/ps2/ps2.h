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
