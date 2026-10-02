#include "common/ym2610_adpcma_job.h"

#define ADPCM_SHIFT 16
#define ADPCM_ADDRESS_MASK ((1u << 21) - 1u)

static int adpcma_delta(const ym2610_adpcma_job_t *job, int step,
	uint8_t nibble)
{
	int value = (2 * (nibble & 0x07) + 1) * job->steps[step >> 4] / 8;
	return (nibble & 0x08) ? -value : value;
}

static void adpcma_decode_channel(ym2610_adpcma_job_t *job,
	ym2610_adpcma_channel_job_t *ch)
{
	uint32_t sample;

	if (!ch->flag)
		return;

	for (sample = 0; sample < job->samples; sample++)
	{
		uint32_t decode_steps;

		ch->now_step += ch->step;
		if (ch->now_step >= (1u << ADPCM_SHIFT))
		{
			decode_steps = ch->now_step >> ADPCM_SHIFT;
			ch->now_step &= (1u << ADPCM_SHIFT) - 1u;

			do
			{
				uint8_t nibble;

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
					uint32_t byte_addr = ch->now_addr >> 1;
					uint32_t source_index;

					if (byte_addr < ch->source_base_byte)
					{
						job->error = 1;
						return;
					}
					source_index = byte_addr - ch->source_base_byte;
					if (source_index >= ch->source_size)
					{
						job->error = 1;
						return;
					}

					ch->now_data = ch->source[source_index];
					nibble = (ch->now_data >> 4) & 0x0f;
				}

				ch->now_addr++;
				ch->adpcma_acc += adpcma_delta(job, ch->adpcma_step, nibble);
				if (ch->adpcma_acc & 0x800)
					ch->adpcma_acc |= ~0xfff;
				else
					ch->adpcma_acc &= 0xfff;

				ch->adpcma_step += job->step_inc[nibble & 7];
				if (ch->adpcma_step > 48 * 16)
					ch->adpcma_step = 48 * 16;
				else if (ch->adpcma_step < 0)
					ch->adpcma_step = 0;
			} while (--decode_steps);

			ch->adpcma_out =
				((ch->adpcma_acc * ch->vol_mul) >> ch->vol_shift) & ~3;
		}

		if (ch->pan == YM2610_ADPCMA_PAN_LEFT ||
			ch->pan == YM2610_ADPCMA_PAN_CENTER)
			job->left[sample] += ch->adpcma_out;
		if (ch->pan == YM2610_ADPCMA_PAN_RIGHT ||
			ch->pan == YM2610_ADPCMA_PAN_CENTER)
			job->right[sample] += ch->adpcma_out;
	}
}

void ym2610_adpcma_job_run(void *data)
{
	ym2610_adpcma_job_t *job = (ym2610_adpcma_job_t *)data;
	volatile int32_t *left = job->left;
	volatile int32_t *right = job->right;
	int channel;
	uint32_t sample;

	job->ended_mask = 0;
	job->error = 0;

	if (job->samples > YM2610_ADPCMA_JOB_MAX_SAMPLES)
	{
		job->error = 1;
		return;
	}

	/* This function runs on the Media Engine. Keep it self-contained rather
	 * than relying on Allegrex libc helpers whose ME execution semantics are not
	 * part of the producer contract. */
	for (sample = 0; sample < job->samples; sample++)
	{
		left[sample] = 0;
		right[sample] = 0;
	}

	for (channel = 0; channel < YM2610_ADPCMA_JOB_CHANNELS; channel++)
	{
		adpcma_decode_channel(job, &job->channel[channel]);
		if (job->error)
			return;
	}
}
