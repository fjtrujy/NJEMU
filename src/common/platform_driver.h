/******************************************************************************

	platform_driver.h

******************************************************************************/

#ifndef PLATFORM_DRIVER_H
#define PLATFORM_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct platform_driver
{
	/* Human-readable identifier. */
	const char *ident;
	/* Creates and initializes handle to platform driver.
	*
	* Returns: platform driver handle on success, otherwise NULL.
	**/
	void *(*init)(void);
	/* Stops and frees driver data. */
   	void (*free)(void *data);
	void (*main)(void *data, int argc, char *argv[]);
	bool (*startSystemButtons)(void *data);
	int32_t (*getDevkitVersion)(void *data);
	bool (*getWlanSwitchState)(void *data);
	int (*getHardwareModel)(void *data);

} platform_driver_t;


extern platform_driver_t platform_psp;
extern platform_driver_t platform_ps2;
extern platform_driver_t platform_desktop;
extern platform_driver_t platform_null;

extern platform_driver_t *platform_drivers[];

#define platform_driver platform_drivers[0]

extern void *platform_data;

#endif /* PLATFORM_DRIVER_H */