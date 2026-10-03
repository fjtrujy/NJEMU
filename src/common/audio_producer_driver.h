#ifndef AUDIO_PRODUCER_DRIVER_H
#define AUDIO_PRODUCER_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*audio_producer_render_fn)(int16_t *buffer);
typedef void (*audio_producer_job_fn)(void *data);

typedef struct audio_producer_driver
{
	const char *ident;
	bool (*init)(void);
	void (*shutdown)(void);
	void (*reset)(void);
	void (*suspend)(void);
	void (*resume)(void);
	void (*render)(audio_producer_render_fn cpu_render, int16_t *buffer);
	bool (*isAvailable)(void);
	bool (*canRunJobs)(void);
	void *(*acquireJobBuffer)(uint32_t size, uint32_t alignment);
	bool (*submitJob)(audio_producer_job_fn job, void *data, uint32_t size);
	void (*waitJob)(void);
} audio_producer_driver_t;

extern const audio_producer_driver_t *const audio_producer_driver;
extern const audio_producer_driver_t audio_producer_cpu;

#endif /* AUDIO_PRODUCER_DRIVER_H */
