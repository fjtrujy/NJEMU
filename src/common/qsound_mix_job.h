#ifndef COMMON_QSOUND_MIX_JOB_H
#define COMMON_QSOUND_MIX_JOB_H

#include <stdint.h>

#define QSOUND_MIX_JOB_CHANNELS 16u
#define QSOUND_MIX_JOB_MAX_SAMPLES 1472u

typedef struct qsound_mix_channel_job
{
	int32_t left_gain;
	int32_t right_gain;
	int8_t sample[QSOUND_MIX_JOB_MAX_SAMPLES];
} qsound_mix_channel_job_t;

typedef struct qsound_mix_job
{
	uint32_t samples;
	uint8_t error;
	uint8_t reserved[3];
	qsound_mix_channel_job_t channel[QSOUND_MIX_JOB_CHANNELS];
	int32_t left[QSOUND_MIX_JOB_MAX_SAMPLES];
	int32_t right[QSOUND_MIX_JOB_MAX_SAMPLES];
} qsound_mix_job_t;

void qsound_mix_job_run(void *data);

#endif /* COMMON_QSOUND_MIX_JOB_H */
