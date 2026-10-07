#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common/okim6295_job.h"
#include "common/qsound_mix_job.h"

static int test_qsound_mix(void)
{
	qsound_mix_job_t job;

	memset(&job, 0, sizeof(job));
	job.samples = 3;
	job.channel[0].left_gain = 128;
	job.channel[0].right_gain = 64;
	job.channel[0].sample[0] = 1;
	job.channel[0].sample[1] = -2;
	job.channel[0].sample[2] = 3;
	qsound_mix_job_run(&job);

	if (job.error || job.left[0] != 2 || job.left[1] != -4 ||
		job.left[2] != 6 || job.right[0] != 1 || job.right[1] != -2 ||
		job.right[2] != 3)
	{
		fprintf(stderr, "QSound producer job mixer produced unexpected samples\n");
		return 0;
	}

	job.samples = QSOUND_MIX_JOB_MAX_SAMPLES + 1u;
	qsound_mix_job_run(&job);
	if (!job.error)
	{
		fprintf(stderr, "QSound producer job mixer accepted an oversized block\n");
		return 0;
	}
	return 1;
}

static int test_okim6295_decode(void)
{
	okim6295_job_t job;

	memset(&job, 0, sizeof(job));
	job.samples = 2;
	job.source_step = 1 << 12;
	job.stream_pos = 1 << 12;
	job.status = 1;
	job.diff_lookup[1] = 10;
	job.voice[0].count = 2;
	job.voice[0].signal = -2;
	job.voice[0].volume = 256;
	job.voice[0].source_size = 1;
	job.voice[0].source[0] = 0x11;
	okim6295_job_run(&job);

	if (job.error || job.output[0] != 0 || job.output[1] != 128 ||
		job.voice[0].offset != 2 || job.voice[0].count != 0 ||
		job.voice[0].signal != 18 || job.voice[0].step != 0 ||
		job.stream_pos != (1 << 12) || job.prev_sample != 128 ||
		job.curr_sample != 288 || job.status != 1)
	{
		fprintf(stderr, "OKIM6295 producer job decoder produced unexpected state\n");
		return 0;
	}

	job.samples = OKIM6295_JOB_MAX_SAMPLES + 1u;
	okim6295_job_run(&job);
	if (!job.error)
	{
		fprintf(stderr, "OKIM6295 producer job decoder accepted an oversized block\n");
		return 0;
	}
	return 1;
}

int main(void)
{
	if (!test_qsound_mix() || !test_okim6295_decode())
		return 1;

	printf("audio producer job tests passed\n");
	return 0;
}
