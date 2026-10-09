#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <audsrv.h>
#include <timer.h>

#include "common/audio_profile.h"
#include "ps2/ps2_audio_profile.h"

#define PS2_AUDIO_PROFILE_WINDOW_BUFFERS 240u

typedef struct audio_profile_stats
{
    uint64_t total_us;
    uint32_t max_us;
    uint32_t count;
} audio_profile_stats_t;

typedef struct audio_output_stats
{
    uint32_t wait_failures;
    uint32_t submit_failures;
    uint32_t short_submissions;
    uint32_t max_shortfall_bytes;
    uint32_t late_periods;
    uint32_t doubled_periods;
    uint32_t free_samples;
    uint32_t queued_samples;
    uint32_t queue_query_failures;
    uint32_t queued_empty_samples;
    uint32_t free_below_request_samples;
    uint32_t min_free;
    uint32_t max_free;
    uint32_t min_queued;
    uint32_t max_queued;
    uint64_t total_free;
    uint64_t total_queued;
    uint64_t total_shortfall_bytes;
    uint32_t retried_buffers;
    uint32_t recovered_buffers;
    uint32_t incomplete_buffers;
    uint32_t recovery_failures;
    uint32_t extra_play_calls;
    uint32_t zero_progress_calls;
    uint64_t recovered_bytes;
    uint64_t unaccepted_bytes;
} audio_output_stats_t;

static audio_profile_stats_t profile_stats[AUDIO_PROFILE_METRIC_COUNT];
static audio_output_stats_t output_stats;
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
    "audsrv_queue_query",
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
    memset(&output_stats, 0, sizeof(output_stats));
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
    if (metric == AUDIO_PROFILE_LOOP_PERIOD && expected_period_us != 0)
    {
        if (elapsed_us > expected_period_us)
            output_stats.late_periods++;
        if (elapsed_us > expected_period_us * 2)
            output_stats.doubled_periods++;
    }
}

void ps2_audio_profile_record_output(uint32_t requested_bytes, int wait_status,
    int submitted_bytes, int sampled_queue, int available_before,
    int queued_after)
{
    if (wait_status != AUDSRV_ERR_NOERROR)
        output_stats.wait_failures++;
    if (submitted_bytes < 0)
        output_stats.submit_failures++;
    else if ((uint32_t)submitted_bytes != requested_bytes)
    {
        uint32_t shortfall = submitted_bytes < (int)requested_bytes ?
            requested_bytes - (uint32_t)submitted_bytes : 0;
        output_stats.short_submissions++;
        output_stats.total_shortfall_bytes += shortfall;
        if (shortfall > output_stats.max_shortfall_bytes)
            output_stats.max_shortfall_bytes = shortfall;
    }

    if (!sampled_queue)
        return;

    if (available_before >= 0)
    {
        uint32_t free_bytes = (uint32_t)available_before;
        if (!output_stats.free_samples || free_bytes < output_stats.min_free)
            output_stats.min_free = free_bytes;
        if (free_bytes > output_stats.max_free)
            output_stats.max_free = free_bytes;
        output_stats.total_free += free_bytes;
        output_stats.free_samples++;
        if (free_bytes < requested_bytes)
            output_stats.free_below_request_samples++;
    }
    else
        output_stats.queue_query_failures++;

    if (queued_after >= 0)
    {
        uint32_t queued_bytes = (uint32_t)queued_after;
        if (!output_stats.queued_samples || queued_bytes < output_stats.min_queued)
            output_stats.min_queued = queued_bytes;
        if (queued_bytes > output_stats.max_queued)
            output_stats.max_queued = queued_bytes;
        output_stats.total_queued += queued_bytes;
        output_stats.queued_samples++;
        if (queued_bytes == 0)
            output_stats.queued_empty_samples++;
    }
    else
        output_stats.queue_query_failures++;
}

void ps2_audio_profile_record_delivery(const ps2_audio_submit_result_t *result)
{
    uint32_t original_accepted = result->first_submit_bytes > 0 ?
        (uint32_t)result->first_submit_bytes : 0;

    if (result->submit_calls > 1)
    {
        output_stats.retried_buffers++;
        output_stats.extra_play_calls += result->submit_calls - 1;
    }
    if (result->accepted_bytes > original_accepted)
        output_stats.recovered_bytes += result->accepted_bytes - original_accepted;
    if (result->accepted_bytes == result->requested_bytes &&
        original_accepted < result->requested_bytes)
        output_stats.recovered_buffers++;
    if (result->accepted_bytes < result->requested_bytes)
    {
        output_stats.incomplete_buffers++;
        output_stats.unaccepted_bytes +=
            result->requested_bytes - result->accepted_bytes;
    }
    if (result->error != PS2_AUDIO_SUBMIT_OK)
        output_stats.recovery_failures++;
    output_stats.zero_progress_calls += result->zero_progress_calls;
}

static void audio_profile_report(void)
{
    char line[1536];
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

    written = snprintf(line + used, sizeof(line) - used,
        " late_periods=%lu doubled_periods=%lu wait_failures=%lu"
        " submit_failures=%lu short_submissions=%lu"
        " shortfall_bytes=%llu max_shortfall_bytes=%lu"
        " free_n=%lu free_avg=%llu free_min=%lu free_max=%lu"
        " free_below_request=%lu queued_n=%lu queued_avg=%llu"
        " queued_min=%lu queued_max=%lu queued_empty=%lu query_failures=%lu"
        " retried_buffers=%lu recovered_buffers=%lu extra_play_calls=%lu"
        " recovered_bytes=%llu incomplete_buffers=%lu unaccepted_bytes=%llu"
        " recovery_failures=%lu zero_progress_calls=%lu",
        (unsigned long)output_stats.late_periods,
        (unsigned long)output_stats.doubled_periods,
        (unsigned long)output_stats.wait_failures,
        (unsigned long)output_stats.submit_failures,
        (unsigned long)output_stats.short_submissions,
        (unsigned long long)output_stats.total_shortfall_bytes,
        (unsigned long)output_stats.max_shortfall_bytes,
        (unsigned long)output_stats.free_samples,
        (unsigned long long)(output_stats.free_samples ?
            output_stats.total_free / output_stats.free_samples : 0),
        (unsigned long)output_stats.min_free,
        (unsigned long)output_stats.max_free,
        (unsigned long)output_stats.free_below_request_samples,
        (unsigned long)output_stats.queued_samples,
        (unsigned long long)(output_stats.queued_samples ?
            output_stats.total_queued / output_stats.queued_samples : 0),
        (unsigned long)output_stats.min_queued,
        (unsigned long)output_stats.max_queued,
        (unsigned long)output_stats.queued_empty_samples,
        (unsigned long)output_stats.queue_query_failures,
        (unsigned long)output_stats.retried_buffers,
        (unsigned long)output_stats.recovered_buffers,
        (unsigned long)output_stats.extra_play_calls,
        (unsigned long long)output_stats.recovered_bytes,
        (unsigned long)output_stats.incomplete_buffers,
        (unsigned long long)output_stats.unaccepted_bytes,
        (unsigned long)output_stats.recovery_failures,
        (unsigned long)output_stats.zero_progress_calls);
    if (written < 0 || (size_t)written >= sizeof(line) - used)
        return;
    used += (size_t)written;

    line[used] = '\0';
    printf("%s\n", line);
    completed_buffers = 0;
    memset(profile_stats, 0, sizeof(profile_stats));
    memset(&output_stats, 0, sizeof(output_stats));
}

void audio_profile_buffer_completed(void)
{
    completed_buffers++;
    if (completed_buffers >= PS2_AUDIO_PROFILE_WINDOW_BUFFERS)
        audio_profile_report();
}
