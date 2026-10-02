#ifndef COMMON_YM2610_ADPCMA_JOB_H
#define COMMON_YM2610_ADPCMA_JOB_H

#include <stdint.h>

#define YM2610_ADPCMA_JOB_CHANNELS 6
#define YM2610_ADPCMA_JOB_MAX_SAMPLES 1472
#define YM2610_ADPCMA_JOB_MAX_SOURCE_BYTES 512

enum ym2610_adpcma_pan
{
	YM2610_ADPCMA_PAN_NONE = 0,
	YM2610_ADPCMA_PAN_RIGHT = 1,
	YM2610_ADPCMA_PAN_LEFT = 2,
	YM2610_ADPCMA_PAN_CENTER = 3,
};

typedef struct ym2610_adpcma_channel_job
{
	uint8_t flag;
	uint8_t flag_mask;
	uint8_t now_data;
	uint8_t pan;
	uint32_t now_addr;
	uint32_t now_step;
	uint32_t step;
	uint32_t end;
	int32_t adpcma_acc;
	int32_t adpcma_step;
	int32_t adpcma_out;
	int8_t vol_mul;
	uint8_t vol_shift;
	uint16_t source_size;
	uint32_t source_base_byte;
	uint32_t control_generation;
	uint8_t source[YM2610_ADPCMA_JOB_MAX_SOURCE_BYTES];
} ym2610_adpcma_channel_job_t;

typedef struct ym2610_adpcma_job
{
	uint32_t samples;
	uint8_t ended_mask;
	uint8_t error;
	uint16_t reserved;
	int16_t steps[49];
	int16_t step_inc[8];
	ym2610_adpcma_channel_job_t channel[YM2610_ADPCMA_JOB_CHANNELS];
	int32_t left[YM2610_ADPCMA_JOB_MAX_SAMPLES];
	int32_t right[YM2610_ADPCMA_JOB_MAX_SAMPLES];
} ym2610_adpcma_job_t;

void ym2610_adpcma_job_run(void *data);

#endif /* COMMON_YM2610_ADPCMA_JOB_H */
