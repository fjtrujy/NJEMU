#ifndef AUDIO_PROFILE_H
#define AUDIO_PROFILE_H

#include <stdint.h>

typedef enum audio_profile_metric
{
	AUDIO_PROFILE_PRODUCER,
	AUDIO_PROFILE_CALLBACK,
	AUDIO_PROFILE_POST_PROCESS,
	AUDIO_PROFILE_ME_JOB_WAIT,
	AUDIO_PROFILE_OUTPUT_BLOCK,
	AUDIO_PROFILE_LOOP_PERIOD,
	AUDIO_PROFILE_METRIC_COUNT
} audio_profile_metric_t;

#ifdef PSP_AUDIO_PROFILE
uint64_t audio_profile_now_us(void);
void audio_profile_configure(uint32_t samples, uint32_t frequency, uint32_t channels);
void audio_profile_add(audio_profile_metric_t metric, uint64_t elapsed_us);
void audio_profile_buffer_completed(void);
#else
static inline uint64_t audio_profile_now_us(void)
{
	return 0;
}

static inline void audio_profile_configure(uint32_t samples, uint32_t frequency,
	uint32_t channels)
{
	(void)samples;
	(void)frequency;
	(void)channels;
}

static inline void audio_profile_add(audio_profile_metric_t metric,
	uint64_t elapsed_us)
{
	(void)metric;
	(void)elapsed_us;
}

static inline void audio_profile_buffer_completed(void)
{
}
#endif

#endif /* AUDIO_PROFILE_H */
