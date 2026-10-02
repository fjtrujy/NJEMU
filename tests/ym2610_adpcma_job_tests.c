#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/ym2610_adpcma_job.h"

#define ADPCM_SHIFT 16
#define ADPCM_ADDRESS_MASK ((1u << 21) - 1u)

static const int steps[49] = {
	16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55,
	60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
	209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598,
	658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
};

static const int step_inc[8] = { -16, -16, -16, -16, 32, 80, 112, 144 };

static int reference_delta(int step, uint8_t nibble)
{
	int value = (2 * (nibble & 7) + 1) * steps[step >> 4] / 8;
	return (nibble & 8) ? -value : value;
}

static void reference_channel(ym2610_adpcma_job_t *job,
	ym2610_adpcma_channel_job_t *ch)
{
	uint32_t sample;

	if (!ch->flag)
		return;

	for (sample = 0; sample < job->samples; sample++)
	{
		uint32_t count;

		ch->now_step += ch->step;
		if (ch->now_step >= (1u << ADPCM_SHIFT))
		{
			count = ch->now_step >> ADPCM_SHIFT;
			ch->now_step &= (1u << ADPCM_SHIFT) - 1u;
			do
			{
				uint8_t nibble;
				uint32_t byte_addr;
				uint32_t index;

				if ((ch->now_addr & ADPCM_ADDRESS_MASK) ==
					((ch->end << 1) & ADPCM_ADDRESS_MASK))
				{
					ch->flag = 0;
					job->ended_mask |= ch->flag_mask;
					return;
				}
				if (ch->now_addr & 1u)
					nibble = ch->now_data & 0x0f;
				else
				{
					byte_addr = ch->now_addr >> 1;
					if (byte_addr < ch->source_base_byte)
						abort();
					index = byte_addr - ch->source_base_byte;
					if (index >= ch->source_size)
						abort();
					ch->now_data = ch->source[index];
					nibble = (ch->now_data >> 4) & 0x0f;
				}
				ch->now_addr++;
				ch->adpcma_acc += reference_delta(ch->adpcma_step, nibble);
				if (ch->adpcma_acc & 0x800)
					ch->adpcma_acc |= ~0xfff;
				else
					ch->adpcma_acc &= 0xfff;
				ch->adpcma_step += step_inc[nibble & 7];
				if (ch->adpcma_step > 48 * 16)
					ch->adpcma_step = 48 * 16;
				else if (ch->adpcma_step < 0)
					ch->adpcma_step = 0;
			} while (--count);
			ch->adpcma_out =
				((ch->adpcma_acc * ch->vol_mul) >> ch->vol_shift) & ~3;
		}
		if (ch->pan == YM2610_ADPCMA_PAN_LEFT || ch->pan == YM2610_ADPCMA_PAN_CENTER)
			job->left[sample] += ch->adpcma_out;
		if (ch->pan == YM2610_ADPCMA_PAN_RIGHT || ch->pan == YM2610_ADPCMA_PAN_CENTER)
			job->right[sample] += ch->adpcma_out;
	}
}

static void reference_run(ym2610_adpcma_job_t *job)
{
	int i;
	job->ended_mask = 0;
	job->error = 0;
	memset(job->left, 0, sizeof(job->left));
	memset(job->right, 0, sizeof(job->right));
	for (i = 0; i < YM2610_ADPCMA_JOB_CHANNELS; i++)
		reference_channel(job, &job->channel[i]);
}

static uint32_t rng_state = 0x13579bdfu;
static uint32_t rng32(void)
{
	rng_state = rng_state * 1664525u + 1013904223u;
	return rng_state;
}

static void make_case(ym2610_adpcma_job_t *job, int iteration)
{
	int c;
	memset(job, 0, sizeof(*job));
	job->samples = 1u + (rng32() % 128u);
	for (c = 0; c < 49; c++)
		job->steps[c] = (int16_t)steps[c];
	for (c = 0; c < 8; c++)
		job->step_inc[c] = (int16_t)step_inc[c];
	for (c = 0; c < YM2610_ADPCMA_JOB_CHANNELS; c++)
	{
		ym2610_adpcma_channel_job_t *ch = &job->channel[c];
		uint32_t i;
		uint32_t max_nibbles;
		ch->flag = (uint8_t)((rng32() & 3u) != 0);
		ch->flag_mask = (uint8_t)(1u << c);
		ch->now_addr = (rng32() % 64u) * 2u + ((iteration + c) & 1u);
		ch->now_step = rng32() & 0xffffu;
		ch->step = 0x4000u + (rng32() & 0xffffu);
		max_nibbles = 2u + (rng32() % 220u);
		ch->end = (ch->now_addr + max_nibbles) >> 1;
		ch->adpcma_acc = (int32_t)(rng32() & 0xfffu);
		if (ch->adpcma_acc & 0x800)
			ch->adpcma_acc |= ~0xfff;
		ch->adpcma_step = (int32_t)((rng32() % 49u) * 16u);
		ch->adpcma_out = (int32_t)(rng32() & 0x7ffu);
		ch->vol_mul = (int8_t)(1 + (rng32() % 15u));
		ch->vol_shift = (uint8_t)(1 + (rng32() % 8u));
		ch->pan = (uint8_t)(rng32() & 3u);
		ch->source_base_byte = ch->now_addr >> 1;
		ch->source_size = YM2610_ADPCMA_JOB_MAX_SOURCE_BYTES;
		ch->control_generation = rng32();
		for (i = 0; i < ch->source_size; i++)
			ch->source[i] = (uint8_t)rng32();
		if (ch->now_addr & 1u)
			ch->now_data = ch->source[0];
	}
}

static int compare_jobs(const ym2610_adpcma_job_t *expected,
	const ym2610_adpcma_job_t *actual, int iteration)
{
	int c;
	if (expected->ended_mask != actual->ended_mask || expected->error != actual->error ||
		memcmp(expected->left, actual->left, expected->samples * sizeof(expected->left[0])) ||
		memcmp(expected->right, actual->right, expected->samples * sizeof(expected->right[0])))
	{
		fprintf(stderr, "output mismatch at iteration %d\n", iteration);
		return 0;
	}
	for (c = 0; c < YM2610_ADPCMA_JOB_CHANNELS; c++)
	{
		const ym2610_adpcma_channel_job_t *e = &expected->channel[c];
		const ym2610_adpcma_channel_job_t *a = &actual->channel[c];
		if (e->flag != a->flag || e->now_data != a->now_data || e->now_addr != a->now_addr ||
			e->now_step != a->now_step || e->adpcma_acc != a->adpcma_acc ||
			e->adpcma_step != a->adpcma_step || e->adpcma_out != a->adpcma_out)
		{
			fprintf(stderr, "state mismatch iteration %d channel %d\n", iteration, c);
			return 0;
		}
	}
	return 1;
}

static int run_exact_case(ym2610_adpcma_job_t *input, int iteration)
{
	ym2610_adpcma_job_t expected = *input;
	ym2610_adpcma_job_t actual = *input;

	reference_run(&expected);
	ym2610_adpcma_job_run(&actual);
	return compare_jobs(&expected, &actual, iteration);
}

static int run_full_buffer_cases(void)
{
	int iteration;

	for (iteration = 0; iteration < 32; iteration++)
	{
		ym2610_adpcma_job_t input;
		int c;

		make_case(&input, iteration + 2000);
		input.samples = YM2610_ADPCMA_JOB_MAX_SAMPLES;
		for (c = 0; c < YM2610_ADPCMA_JOB_CHANNELS; c++)
		{
			ym2610_adpcma_channel_job_t *ch = &input.channel[c];
			/* Keep the worst-case 1472-sample decode inside the bounded source
			 * payload while still exercising fractional stepping. */
			ch->step = 0x4000u + (uint32_t)(c * 0x0800u);
			ch->end = (ch->now_addr + 900u) >> 1;
		}
		if (!run_exact_case(&input, iteration + 2000))
			return 0;
	}
	return 1;
}

static int run_end_boundary_case(void)
{
	ym2610_adpcma_job_t input;
	ym2610_adpcma_channel_job_t *ch;

	memset(&input, 0, sizeof(input));
	input.samples = 4;
	ch = &input.channel[0];
	ch->flag = 1;
	ch->flag_mask = 1;
	ch->pan = YM2610_ADPCMA_PAN_CENTER;
	ch->now_addr = 0x40;
	ch->end = ch->now_addr >> 1;
	ch->now_step = 0xffff;
	ch->step = 1;
	ch->source_base_byte = ch->now_addr >> 1;
	ch->source_size = 0;
	return run_exact_case(&input, 3000);
}

int main(void)
{
	int iteration;
	for (iteration = 0; iteration < 2000; iteration++)
	{
		ym2610_adpcma_job_t input;
		make_case(&input, iteration);
		if (!run_exact_case(&input, iteration))
			return 1;
	}
	if (!run_full_buffer_cases())
		return 1;
	if (!run_end_boundary_case())
		return 1;
	{
		ym2610_adpcma_job_t invalid;
		memset(&invalid, 0, sizeof(invalid));
		invalid.samples = YM2610_ADPCMA_JOB_MAX_SAMPLES + 1u;
		ym2610_adpcma_job_run(&invalid);
		if (!invalid.error)
		{
			fprintf(stderr, "oversized job was not rejected\n");
			return 1;
		}
	}
	printf("YM2610 ADPCM-A batch decoder: 2033 exact/boundary cases passed\n");
	return 0;
}
