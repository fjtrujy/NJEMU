/******************************************************************************

	power_driver.c

******************************************************************************/

#include <stddef.h>
#include "power_driver.h"

int platform_cpuclock;

power_driver_t power_null = {
	"null",
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

power_driver_t *power_drivers[] = {
#ifdef PSP
	&power_psp,
#endif
#ifdef PS2
	&power_ps2,
#endif
#ifdef DESKTOP
	&power_desktop,
#endif
	&power_null,
	NULL,
};
