#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pspkernel.h>
#include "common/emulator_options.h"
#include "common/runtime_paths.h"
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
	memset(profile_stats, 0, sizeof(profile_stats));
	memset(profile_events, 0, sizeof(profile_events));
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
	int fd;
	int written;
	int i;

	written = snprintf(cursor, remaining,
		"[psp-me-sound] frames=%lu wall_us=%llu fps_milli=%llu audio_processor=%d",
		(unsigned long)completed_frames,
		(unsigned long long)wall_us,
		(unsigned long long)fps_milli,
		option_audio_processor);
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

	mvs_me_sound_profile_start_window();
	completed_frames++;
	if (completed_frames < MVS_ME_SOUND_PROFILE_WINDOW_FRAMES)
		return;

	now_us = mvs_me_sound_profile_now_us();
	mvs_me_sound_profile_report(now_us);
	completed_frames = 0;
	window_start_us = now_us;
	memset(profile_stats, 0, sizeof(profile_stats));
	memset(profile_events, 0, sizeof(profile_events));
}
