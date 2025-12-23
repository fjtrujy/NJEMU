/******************************************************************************

	ui_text_driver.c

******************************************************************************/

#include <stddef.h>
#include "ui_text_driver.h"

void *ui_text_data;

ui_text_driver_t ui_text_null = {
	"null",
	NULL,
	NULL,
	NULL,
	NULL,
};

ui_text_driver_t *ui_text_drivers[] = {
#ifdef PSP
	&ui_text_psp,
#endif
#ifdef PS2
	&ui_text_ps2,
#endif
#ifdef DESKTOP
	&ui_text_desktop,
#endif
	&ui_text_null,
	NULL,
};
