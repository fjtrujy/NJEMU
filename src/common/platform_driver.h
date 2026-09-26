/******************************************************************************

	platform_driver.h

******************************************************************************/

#ifndef PLATFORM_DRIVER_H
#define PLATFORM_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "platform_memory_info.h"
#include "ui_language.h"

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
	int32_t (*getDevkitVersion)(void *data);
	bool (*getWlanSwitchState)(void *data);
	int (*getHardwareModel)(void *data);
	/* Captures normalized platform memory telemetry. Returns false when the
	 * platform cannot provide any useful memory information. R10 cache capacity
	 * is established by retained allocation probes; this is diagnostic data.
	 */
	bool (*queryMemoryInfo)(void *data, platform_memory_info_t *out);
	/* Maps the platform/OS language to NJEMU's supported translation set. */
	ui_language_t (*getSystemLanguage)(void *data);

} platform_driver_t;


extern platform_driver_t *const platform_driver;

extern void *platform_data;

#endif /* PLATFORM_DRIVER_H */
