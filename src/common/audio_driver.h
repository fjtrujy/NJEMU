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
	/* Optional native stream pause/mute hook. The common sound thread keeps
	 * feeding silent buffers so queued backends can drain stale audio safely. */
	void (*setPaused)(void *data, bool paused);
} audio_driver_t;


extern audio_driver_t *const audio_driver;

#endif /* AUDIO_DRIVER_H */
