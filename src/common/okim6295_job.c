#include "common/okim6295_job.h"

#define OKIM6295_FRAC_BIT 12
#define OKIM6295_FRAC_SIZE (1 << OKIM6295_FRAC_BIT)

static const int32_t okim6295_index_shift[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

void okim6295_job_run(void *data)
{
	okim6295_job_t *job = (okim6295_job_t *)data;
	int32_t prev_sample;
	int32_t curr_sample;
	int32_t stream_pos;
	uint32_t sample_index;

	job->error = 0;
	if (job->samples > OKIM6295_JOB_MAX_SAMPLES)
	{
		job->error = 1;
		return;
	}

	prev_sample = job->prev_sample;
	curr_sample = job->curr_sample;
	stream_pos = job->stream_pos;

	for (sample_index = 0; sample_index < job->samples; sample_index++)
	{
		int32_t signal;

		if (stream_pos >= OKIM6295_FRAC_SIZE)
		{
			prev_sample = curr_sample;

			do
			{
				uint32_t voice_index;

				curr_sample = 0;
				for (voice_index = 0; voice_index < OKIM6295_JOB_VOICES;
					voice_index++)
				{
					okim6295_voice_job_t *voice = &job->voice[voice_index];
					int nibble;
					uint32_t byte_addr;
					uint32_t source_index;

					if (!(job->status & (1u << voice_index)))
						continue;
					if (voice->count-- == 0)
					{
						job->status &= ~(1u << voice_index);
						continue;
					}

					byte_addr = voice->offset >> 1;
					if (byte_addr < voice->source_base_byte)
					{
						job->error = 1;
						return;
					}
					source_index = byte_addr - voice->source_base_byte;
					if (source_index >= voice->source_size)
					{
						job->error = 1;
						return;
					}

					if (voice->offset & 1u)
						nibble = voice->data & 0x0f;
					else
					{
						voice->data = voice->source[source_index];
						nibble = voice->data >> 4;
					}

					signal = voice->signal +
						job->diff_lookup[(voice->step << 4) + nibble];
					if (signal > 2047)
						signal = 2047;
					else if (signal < -2048)
						signal = -2048;
					voice->signal = signal;
					voice->sample = signal * voice->volume;

					voice->step += okim6295_index_shift[nibble & 7];
					if (voice->step > 48)
						voice->step = 48;
					else if (voice->step < 0)
						voice->step = 0;

					curr_sample += voice->sample >> 4;
					voice->offset++;
				}

				stream_pos -= OKIM6295_FRAC_SIZE;
			} while (stream_pos >= OKIM6295_FRAC_SIZE);
		}

		signal = prev_sample +
			(((curr_sample - prev_sample) * stream_pos) >> OKIM6295_FRAC_BIT);
		job->output[sample_index] = signal;
		stream_pos += job->source_step;
	}

	job->stream_pos = stream_pos;
	job->prev_sample = prev_sample;
	job->curr_sample = curr_sample;
}
