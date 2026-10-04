#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pspkernel.h>
#include "common/audio_producer_driver.h"
#include "common/emulator_options.h"
#include "common/runtime_paths.h"
#include "mvs/me_sound_shadow.h"
#include "mvs/me_sound_profile.h"

#define MVS_ME_SOUND_PROFILE_WINDOW_FRAMES 300u

typedef struct mvs_me_sound_profile_stats
{
	uint64_t total_us;
	uint32_t max_us;
	uint32_t count;
} mvs_me_sound_profile_stats_t;

static mvs_me_sound_profile_stats_t
	profile_stats[MVS_ME_SOUND_PROFILE_METRIC_COUNT];
static uint32_t profile_events[MVS_ME_SOUND_PROFILE_EVENT_COUNT];
static uint32_t completed_frames;
static uint64_t window_start_us;
static uint64_t last_frame_completed_us;
static uint32_t frame_samples_us[MVS_ME_SOUND_PROFILE_WINDOW_FRAMES];
static uint32_t frame_sorted_us[MVS_ME_SOUND_PROFILE_WINDOW_FRAMES];
static uint32_t frame_sample_count;

static const char *const metric_names[MVS_ME_SOUND_PROFILE_METRIC_COUNT] = {
	"m68000",
	"z80",
	"scheduler",
};

static const char *const event_names[MVS_ME_SOUND_PROFILE_EVENT_COUNT] = {
	"sound_cmd",
	"sound_latch",
	"main_status_read",
	"z80_cmd_read",
	"z80_result_write",
	"ym_status_a_read",
	"ym_status_b_read",
	"ym_data_read",
	"ym_timer_a",
	"ym_timer_b",
	"ym_irq_assert",
	"ym_irq_clear",
	"ym_timer_preempt",
	"z80_preempt_main",
	"timer_slice",
};

uint64_t mvs_me_sound_profile_now_us(void)
{
	return sceKernelGetSystemTimeWide();
}

static void mvs_me_sound_profile_start_window(void)
{
	if (window_start_us == 0)
		window_start_us = mvs_me_sound_profile_now_us();
}

void mvs_me_sound_profile_reset(void)
{
	completed_frames = 0;
	window_start_us = 0;
	last_frame_completed_us = 0;
	frame_sample_count = 0;
	memset(profile_stats, 0, sizeof(profile_stats));
	memset(profile_events, 0, sizeof(profile_events));
	memset(frame_samples_us, 0, sizeof(frame_samples_us));
}

void mvs_me_sound_profile_add_time(mvs_me_sound_profile_metric_t metric,
	uint64_t elapsed_us)
{
	mvs_me_sound_profile_stats_t *stats;
	uint32_t sample;

	if ((unsigned int)metric >= MVS_ME_SOUND_PROFILE_METRIC_COUNT)
		return;

	mvs_me_sound_profile_start_window();
	stats = &profile_stats[metric];
	sample = elapsed_us > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_us;
	stats->total_us += sample;
	if (sample > stats->max_us)
		stats->max_us = sample;
	stats->count++;
}

void mvs_me_sound_profile_event(mvs_me_sound_profile_event_t event)
{
	if ((unsigned int)event >= MVS_ME_SOUND_PROFILE_EVENT_COUNT)
		return;

	mvs_me_sound_profile_start_window();
	if (profile_events[event] != UINT32_MAX)
		profile_events[event]++;
}

static void mvs_me_sound_profile_report(uint64_t now_us)
{
	char path[1024];
	char line[2048];
	char *cursor = line;
	size_t remaining = sizeof(line);
	uint64_t wall_us = now_us - window_start_us;
	uint64_t fps_milli = wall_us ?
		((uint64_t)completed_frames * 1000000000ULL) / wall_us : 0;
	uint64_t frame_total_us = 0;
	uint32_t frame_avg_us = 0;
	uint32_t frame_p50_us = 0;
	uint32_t frame_p95_us = 0;
	uint32_t frame_p99_us = 0;
	uint32_t frame_max_us = 0;
	uint32_t me_available = 0;
	uint32_t me_coprocessor = 0;
	uint32_t me_authoritative = 0;
	int fd;
	int written;
	int i;
	uint32_t j;

	if (frame_sample_count != 0)
	{
		memcpy(frame_sorted_us, frame_samples_us,
			frame_sample_count * sizeof(frame_sorted_us[0]));
		for (i = 1; i < (int)frame_sample_count; i++)
		{
			uint32_t value = frame_sorted_us[i];
			j = (uint32_t)i;
			while (j != 0 && frame_sorted_us[j - 1] > value)
			{
				frame_sorted_us[j] = frame_sorted_us[j - 1];
				j--;
			}
			frame_sorted_us[j] = value;
		}
		for (j = 0; j < frame_sample_count; j++)
			frame_total_us += frame_samples_us[j];
		frame_avg_us = (uint32_t)(frame_total_us / frame_sample_count);
		frame_p50_us = frame_sorted_us[((50u * frame_sample_count + 99u) / 100u) - 1u];
		frame_p95_us = frame_sorted_us[((95u * frame_sample_count + 99u) / 100u) - 1u];
		frame_p99_us = frame_sorted_us[((99u * frame_sample_count + 99u) / 100u) - 1u];
		frame_max_us = frame_sorted_us[frame_sample_count - 1u];
	}
#ifdef PSP_ME_AUDIO
	me_available = audio_producer_driver->isAvailable() ? 1u : 0u;
#endif
#ifdef PSP_ME_SOUND_COPROCESSOR
	me_coprocessor = 1u;
	me_authoritative = mvs_me_sound_shadow_authoritative() ? 1u : 0u;
#endif

	written = snprintf(cursor, remaining,
		"[psp-me-sound] frames=%lu wall_us=%llu fps_milli=%llu "
		"frame_n=%lu frame_avg_us=%lu frame_p50_us=%lu frame_p95_us=%lu "
		"frame_p99_us=%lu frame_max_us=%lu audio_processor=%d me_available=%lu "
		"me_coprocessor=%lu me_authoritative=%lu",
		(unsigned long)completed_frames,
		(unsigned long long)wall_us,
		(unsigned long long)fps_milli,
		(unsigned long)frame_sample_count,
		(unsigned long)frame_avg_us,
		(unsigned long)frame_p50_us,
		(unsigned long)frame_p95_us,
		(unsigned long)frame_p99_us,
		(unsigned long)frame_max_us,
		option_audio_processor,
		(unsigned long)me_available,
		(unsigned long)me_coprocessor,
		(unsigned long)me_authoritative);
	if (written < 0 || (size_t)written >= remaining)
		return;
	cursor += written;
	remaining -= (size_t)written;

	for (i = 0; i < MVS_ME_SOUND_PROFILE_METRIC_COUNT; i++)
	{
		const mvs_me_sound_profile_stats_t *stats = &profile_stats[i];
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

	for (i = 0; i < MVS_ME_SOUND_PROFILE_EVENT_COUNT; i++)
	{
		written = snprintf(cursor, remaining, " %s=%lu",
			event_names[i], (unsigned long)profile_events[i]);
		if (written < 0 || (size_t)written >= remaining)
			break;
		cursor += written;
		remaining -= (size_t)written;
	}

	if (remaining > 1)
		*cursor++ = '\n';

	snprintf(path, sizeof(path), "%spsp_me_sound_profile.log", launchDir);
	fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd >= 0)
	{
		write(fd, line, (size_t)(cursor - line));
		close(fd);
	}
}

void mvs_me_sound_profile_frame_completed(void)
{
	uint64_t now_us;
	uint64_t frame_us;

	mvs_me_sound_profile_start_window();
	now_us = mvs_me_sound_profile_now_us();
	if (last_frame_completed_us == 0)
		last_frame_completed_us = window_start_us;
	frame_us = now_us - last_frame_completed_us;
	last_frame_completed_us = now_us;
	if (frame_sample_count < MVS_ME_SOUND_PROFILE_WINDOW_FRAMES)
	{
		frame_samples_us[frame_sample_count++] =
			frame_us > UINT32_MAX ? UINT32_MAX : (uint32_t)frame_us;
	}
	completed_frames++;
	if (completed_frames < MVS_ME_SOUND_PROFILE_WINDOW_FRAMES)
		return;

	mvs_me_sound_profile_report(now_us);
	completed_frames = 0;
	window_start_us = now_us;
	frame_sample_count = 0;
	memset(profile_stats, 0, sizeof(profile_stats));
	memset(profile_events, 0, sizeof(profile_events));
}
