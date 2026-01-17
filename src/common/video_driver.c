/******************************************************************************

	video_driver.c

******************************************************************************/

#include <stddef.h>
#include "video_driver.h"

void *show_frame;
void *draw_frame;
void *work_frame;
void *tex_frame;

void *video_data;

video_driver_t video_null = {
	"null",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

video_driver_t *video_drivers[] = {
#ifdef PSP
	&video_psp,
#endif
#ifdef PS2
	&video_ps2,
#endif
#ifdef DESKTOP
	&video_desktop,
#endif
	&video_null,
	NULL,
};
