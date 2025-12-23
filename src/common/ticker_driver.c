/******************************************************************************

	ticker_driver.c

******************************************************************************/

#include <stddef.h>
#include "ticker_driver.h"

void *ticker_data;

ticker_driver_t ticker_null = {
	"null",
	NULL,
	NULL,
	NULL,
};

ticker_driver_t *ticker_drivers[] = {
#ifdef PSP
	&ticker_psp,
#endif
#ifdef PS2
	&ticker_ps2,
#endif
#ifdef DESKTOP
	&ticker_desktop,
#endif
	&ticker_null,
	NULL,
};
