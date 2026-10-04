#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pspkernel.h>
#include "common/audio_profile.h"
#include "common/runtime_paths.h"

#define AUDIO_PROFILE_WINDOW_BUFFERS 300u

typedef struct audio_profile_stats
{
	uint64_t total_us;
	uint32_t max_us;
	uint32_t count;
} audio_profile_stats_t;

static audio_profile_stats_t profile_stats[AUDIO_PROFILE_METRIC_COUNT];
static uint32_t completed_buffers;
static uint32_t configured_samples;
static uint32_t configured_frequency;
static uint32_t configured_channels;
static uint64_t expected_period_us;

static const char *const metric_names[AUDIO_PROFILE_METRIC_COUNT] = {
	"producer",
	"callback",
	"post",
	"me_wait",
	"output_block",
	"loop_period",
};

uint64_t audio_profile_now_us(void)
{
	return sceKernelGetSystemTimeWide();
}

void audio_profile_configure(uint32_t samples, uint32_t frequency, uint32_t channels)
{
	configured_samples = samples;
	configured_frequency = frequency;
	configured_channels = channels;
	expected_period_us = frequency ? ((uint64_t)samples * 1000000ULL) / frequency : 0;
	completed_buffers = 0;
	memset(profile_stats, 0, sizeof(profile_stats));
}

void audio_profile_add(audio_profile_metric_t metric, uint64_t elapsed_us)
{
	audio_profile_stats_t *stats;
	uint32_t sample;

	if ((unsigned int)metric >= AUDIO_PROFILE_METRIC_COUNT)
		return;

	stats = &profile_stats[metric];
	sample = elapsed_us > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_us;
	stats->total_us += sample;
	if (sample > stats->max_us)
		stats->max_us = sample;
	stats->count++;
}

static void audio_profile_report(void)
{
	char path[1024];
	char line[1024];
	char *cursor = line;
	size_t remaining = sizeof(line);
	int fd;
	int written;
	int i;

	written = snprintf(cursor, remaining,
		"[psp-audio] buffers=%lu samples=%lu frequency=%lu channels=%lu expected_period_us=%llu",
		(unsigned long)completed_buffers,
		(unsigned long)configured_samples,
		(unsigned long)configured_frequency,
		(unsigned long)configured_channels,
		(unsigned long long)expected_period_us);
	if (written < 0 || (size_t)written >= remaining)
		return;
	cursor += written;
	remaining -= (size_t)written;

	for (i = 0; i < AUDIO_PROFILE_METRIC_COUNT; i++)
	{
		const audio_profile_stats_t *stats = &profile_stats[i];
		uint64_t average = stats->count ? stats->total_us / stats->count : 0;

		written = snprintf(cursor, remaining,
			" %s_total=%llu %s_avg=%llu %s_max=%lu %s_n=%lu",
			metric_names[i], (unsigned long long)stats->total_us,
			metric_names[i], (unsigned long long)average,
			metric_names[i], (unsigned long)stats->max_us,
			metric_names[i], (unsigned long)stats->count);
		if (written < 0 || (size_t)written >= remaining)
			break;
		cursor += written;
		remaining -= (size_t)written;
	}

	if (remaining > 1)
		*cursor++ = '\n';

	snprintf(path, sizeof(path), "%spsp_audio_profile.log", launchDir);
	fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd >= 0)
	{
		write(fd, line, (size_t)(cursor - line));
		close(fd);
	}

	completed_buffers = 0;
	memset(profile_stats, 0, sizeof(profile_stats));
}

void audio_profile_buffer_completed(void)
{
	completed_buffers++;
	if (completed_buffers >= AUDIO_PROFILE_WINDOW_BUFFERS)
		audio_profile_report();
}
