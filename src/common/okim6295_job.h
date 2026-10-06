#ifndef COMMON_OKIM6295_JOB_H
#define COMMON_OKIM6295_JOB_H

#include <stdint.h>

#define OKIM6295_JOB_VOICES 4u
#define OKIM6295_JOB_MAX_SAMPLES 1472u
#define OKIM6295_JOB_MAX_SOURCE_BYTES 512u
#define OKIM6295_JOB_DIFF_ENTRIES (49u * 16u)

typedef struct okim6295_voice_job
{
	uint32_t offset;
	int32_t sample;
	uint32_t count;
	int32_t signal;
	int32_t step;
	int32_t data;
	int32_t volume;
	uint32_t source_base_byte;
	uint16_t source_size;
	uint16_t reserved;
	uint8_t source[OKIM6295_JOB_MAX_SOURCE_BYTES];
} okim6295_voice_job_t;

typedef struct okim6295_job
{
	uint32_t samples;
	uint32_t control_generation;
	int32_t source_step;
	int32_t stream_pos;
	int32_t prev_sample;
	int32_t curr_sample;
	uint32_t status;
	uint8_t error;
	uint8_t reserved[3];
	int32_t diff_lookup[OKIM6295_JOB_DIFF_ENTRIES];
	okim6295_voice_job_t voice[OKIM6295_JOB_VOICES];
	int32_t output[OKIM6295_JOB_MAX_SAMPLES];
} okim6295_job_t;

void okim6295_job_run(void *data);

#endif /* COMMON_OKIM6295_JOB_H */
