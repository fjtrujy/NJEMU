#ifndef AUDIO_PRODUCER_DRIVER_H
#define AUDIO_PRODUCER_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*audio_producer_render_fn)(int16_t *buffer);

typedef struct audio_producer_driver
{
	const char *ident;
	bool (*init)(void);
	void (*shutdown)(void);
	void (*reset)(void);
	void (*render)(audio_producer_render_fn cpu_render, int16_t *buffer);
	bool (*isAvailable)(void);
} audio_producer_driver_t;

extern const audio_producer_driver_t *const audio_producer_driver;

#endif /* AUDIO_PRODUCER_DRIVER_H */
