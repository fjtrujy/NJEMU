#ifndef MVS_SOUND_OFFLOAD_PROFILE_H
#define MVS_SOUND_OFFLOAD_PROFILE_H

#include <stdint.h>

typedef enum mvs_sound_offload_profile_metric
{
	MVS_SOUND_OFFLOAD_PROFILE_M68000,
	MVS_SOUND_OFFLOAD_PROFILE_Z80,
	MVS_SOUND_OFFLOAD_PROFILE_SCHEDULER,
	MVS_SOUND_OFFLOAD_PROFILE_METRIC_COUNT
} mvs_sound_offload_profile_metric_t;

typedef enum mvs_sound_offload_profile_event
{
	MVS_SOUND_OFFLOAD_PROFILE_SOUND_COMMAND,
	MVS_SOUND_OFFLOAD_PROFILE_SOUND_LATCH_APPLY,
	MVS_SOUND_OFFLOAD_PROFILE_MAIN_STATUS_READ,
	MVS_SOUND_OFFLOAD_PROFILE_Z80_COMMAND_READ,
	MVS_SOUND_OFFLOAD_PROFILE_Z80_RESULT_WRITE,
	MVS_SOUND_OFFLOAD_PROFILE_YM_STATUS_A_READ,
	MVS_SOUND_OFFLOAD_PROFILE_YM_STATUS_B_READ,
	MVS_SOUND_OFFLOAD_PROFILE_YM_DATA_READ,
	MVS_SOUND_OFFLOAD_PROFILE_YM_TIMER_A,
	MVS_SOUND_OFFLOAD_PROFILE_YM_TIMER_B,
	MVS_SOUND_OFFLOAD_PROFILE_YM_IRQ_ASSERT,
	MVS_SOUND_OFFLOAD_PROFILE_YM_IRQ_CLEAR,
	MVS_SOUND_OFFLOAD_PROFILE_YM_TIMER_PREEMPT,
	MVS_SOUND_OFFLOAD_PROFILE_Z80_TIMER_PREEMPT_MAIN,
	MVS_SOUND_OFFLOAD_PROFILE_TIMER_SLICE,
	MVS_SOUND_OFFLOAD_PROFILE_EVENT_COUNT
} mvs_sound_offload_profile_event_t;

#ifdef NJEMU_SOUND_OFFLOAD_PROFILE
uint64_t mvs_sound_offload_profile_now_us(void);
void mvs_sound_offload_profile_reset(void);
void mvs_sound_offload_profile_add_time(mvs_sound_offload_profile_metric_t metric,
	uint64_t elapsed_us);
void mvs_sound_offload_profile_event(mvs_sound_offload_profile_event_t event);
void mvs_sound_offload_profile_frame_completed(void);
#else
static inline uint64_t mvs_sound_offload_profile_now_us(void)
{
	return 0;
}

static inline void mvs_sound_offload_profile_reset(void)
{
}

static inline void mvs_sound_offload_profile_add_time(mvs_sound_offload_profile_metric_t metric,
	uint64_t elapsed_us)
{
	(void)metric;
	(void)elapsed_us;
}

static inline void mvs_sound_offload_profile_event(mvs_sound_offload_profile_event_t event)
{
	(void)event;
}

static inline void mvs_sound_offload_profile_frame_completed(void)
{
}
#endif

#endif /* MVS_SOUND_OFFLOAD_PROFILE_H */
