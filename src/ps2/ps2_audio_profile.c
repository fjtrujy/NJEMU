#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <timer.h>

#include "common/audio_profile.h"

#define PS2_AUDIO_PROFILE_WINDOW_BUFFERS 240u

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
    "producer_job_wait",
    "me_prepare_wait",
    "me_pcm_fill",
    "me_render_submit",
    "me_render_wait",
    "output_block",
    "loop_period",
    "volume_mix",
    "audsrv_wait",
    "audsrv_submit",
};

uint64_t audio_profile_now_us(void)
{
    u32 seconds;
    u32 microseconds;
    TimerBusClock2USec(GetTimerSystemTime(), &seconds, &microseconds);
    return (uint64_t)seconds * 1000000ULL + microseconds;
}

void audio_profile_configure(uint32_t samples, uint32_t frequency, uint32_t channels)
{
    configured_samples = samples;
    configured_frequency = frequency;
    configured_channels = channels;
    expected_period_us = frequency ? (uint64_t)samples * 1000000ULL / frequency : 0;
    completed_buffers = 0;
    memset(profile_stats, 0, sizeof(profile_stats));
}

void audio_profile_add(audio_profile_metric_t metric, uint64_t elapsed_us)
{
    audio_profile_stats_t *stats;
    uint32_t elapsed;

    if ((unsigned int)metric >= AUDIO_PROFILE_METRIC_COUNT)
        return;

    stats = &profile_stats[metric];
    elapsed = elapsed_us > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_us;
    stats->total_us += elapsed;
    if (elapsed > stats->max_us)
        stats->max_us = elapsed;
    stats->count++;
}

static void audio_profile_report(void)
{
    char line[1024];
    size_t used;
    int written;
    unsigned int i;

    written = snprintf(line, sizeof(line),
        "[ps2-audio] buffers=%lu samples=%lu frequency=%lu channels=%lu expected_period_us=%llu",
        (unsigned long)completed_buffers,
        (unsigned long)configured_samples,
        (unsigned long)configured_frequency,
        (unsigned long)configured_channels,
        (unsigned long long)expected_period_us);
    if (written < 0 || (size_t)written >= sizeof(line))
        return;
    used = (size_t)written;

    for (i = 0; i < AUDIO_PROFILE_METRIC_COUNT; i++)
    {
        const audio_profile_stats_t *stats = &profile_stats[i];
        uint64_t average = stats->count ? stats->total_us / stats->count : 0;

        if (stats->count == 0)
            continue;

        written = snprintf(line + used, sizeof(line) - used,
            " %s_avg=%llu %s_max=%lu %s_n=%lu",
            metric_names[i], (unsigned long long)average,
            metric_names[i], (unsigned long)stats->max_us,
            metric_names[i], (unsigned long)stats->count);
        if (written < 0 || (size_t)written >= sizeof(line) - used)
            break;
        used += (size_t)written;
    }

    line[used] = '\0';
    printf("%s\n", line);
    completed_buffers = 0;
    memset(profile_stats, 0, sizeof(profile_stats));
}

void audio_profile_buffer_completed(void)
{
    completed_buffers++;
    if (completed_buffers >= PS2_AUDIO_PROFILE_WINDOW_BUFFERS)
        audio_profile_report();
}
