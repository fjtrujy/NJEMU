/******************************************************************************

	ps2.h


******************************************************************************/

#ifndef PS2_MAIN_H
#define PS2_MAIN_H

#define SCR_WIDTH			480
#define SCR_HEIGHT			272
#define BUF_WIDTH			512

#define REFRESH_RATE		(59.940059)		// (9000000Hz * 1) / (525 * 286)

#define FONTSIZE			14

/* Accessor used by ps2_ui_draw.c to reach gsGlobal inside ps2_video.c's
 * private ps2_video_t struct without exposing the full definition. The
 * caller casts to GSGLOBAL*. */
void *ps2_video_get_gsGlobal(void *video_data);

#endif /* PS2_MAIN_H */
