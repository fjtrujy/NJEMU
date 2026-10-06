#include "common/qsound_mix_job.h"

void qsound_mix_job_run(void *data)
{
	qsound_mix_job_t *job = (qsound_mix_job_t *)data;
	uint32_t channel;
	uint32_t sample;

	job->error = 0;
	if (job->samples > QSOUND_MIX_JOB_MAX_SAMPLES)
	{
		job->error = 1;
		return;
	}

	for (sample = 0; sample < job->samples; sample++)
	{
		job->left[sample] = 0;
		job->right[sample] = 0;
	}

	for (channel = 0; channel < QSOUND_MIX_JOB_CHANNELS; channel++)
	{
		const qsound_mix_channel_job_t *source = &job->channel[channel];

		for (sample = 0; sample < job->samples; sample++)
		{
			int32_t value = source->sample[sample];

			job->left[sample] += (value * source->left_gain) >> 6;
			job->right[sample] += (value * source->right_gain) >> 6;
		}
	}
}
