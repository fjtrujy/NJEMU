/******************************************************************************

	platform_driver.c

******************************************************************************/

#include <stddef.h>
#include "platform_driver.h"

platform_driver_t platform_null = {
	"null",
	NULL,
	NULL,
	NULL,
	NULL,
};

platform_driver_t *platform_drivers[] = {
#ifdef PSP
	&platform_psp,
#endif
#ifdef PS2
	&platform_ps2,
#endif
#ifdef DESKTOP
	&platform_desktop,
#endif
	&platform_null,
	NULL,
};
