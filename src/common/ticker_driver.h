/******************************************************************************

	ticker_driver.h

******************************************************************************/

#ifndef TICKER_DRIVER_H
#define TICKER_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <time.h>

typedef struct ticker_driver
{
	/* Human-readable identifier. */
	const char *ident;
	/* Creates and initializes handle to ticker driver.
	*
	* Returns: ticker driver handle on success, otherwise NULL.
	**/
	void *(*init)(void);
	/* Stops and frees driver data. */
   	void (*free)(void *data);
	uint64_t (*currentUs)(void *data);

} ticker_driver_t;

extern ticker_driver_t ticker_psp;
extern ticker_driver_t ticker_ps2;
extern ticker_driver_t ticker_desktop;
extern ticker_driver_t ticker_null;

extern ticker_driver_t *ticker_drivers[];
extern void *ticker_data;

#define ticker_driver ticker_drivers[0]

#endif /* TICKER_DRIVER_H */