/******************************************************************************

	audio_driver.h

******************************************************************************/

#ifndef AUDIO_DRIVER_H
#define AUDIO_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

typedef struct audio_driver
{
	/* Human-readable identifier. */
	const char *ident;
	/* Creates and initializes handle to audio driver.
	*
	* Returns: audio driver handle on success, otherwise NULL.
	**/
	void *(*init)(void);
	/* Stops and frees driver data. */
   	void (*free)(void *data);
	int32_t (*volumeMax)(void *data);
	bool (*chSRCReserve)(void *data, uint16_t samples, int32_t frequency, uint8_t channels);
	bool (*chReserve)(void *data, uint16_t samplecount, uint8_t channels);
	void (*srcOutputBlocking)(void *data, int32_t volume, void *buffer, uint32_t size);
	void (*outputPannedBlocking)(void *data, int leftvol, int rightvol, void *buffer, uint32_t size);
	void (*release)(void *data);
} audio_driver_t;


extern audio_driver_t audio_psp;
extern audio_driver_t audio_ps2;
extern audio_driver_t audio_desktop;
extern audio_driver_t audio_null;

extern audio_driver_t *audio_drivers[];

#define audio_driver audio_drivers[0]

#endif /* AUDIO_DRIVER_H */
