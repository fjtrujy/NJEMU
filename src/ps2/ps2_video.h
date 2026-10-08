/******************************************************************************

	ps2_video.h

	PS2 video backend private interface

******************************************************************************/

#ifndef PS2_VIDEO_H
#define PS2_VIDEO_H

#include <stdint.h>

/* Keep GS objects private to the PS2 backend. The UI adapter needs only the
 * renderer context handle and therefore receives it opaquely. */
void *ps2_video_get_gsGlobal(void *video_data);

/* Copy a CT16 GS surface into CPU RAM. dst_pitch is expressed in pixels. */
int ps2_video_read_frame(void *video_data, int frame_index,
	int x, int y, int width, int height,
	uint16_t *dst, int dst_pitch);

#endif /* PS2_VIDEO_H */
