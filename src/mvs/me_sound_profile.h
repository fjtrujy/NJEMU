#ifndef MVS_ME_SOUND_PROFILE_H
#define MVS_ME_SOUND_PROFILE_H

#include <stdint.h>

typedef enum mvs_me_sound_profile_metric
{
	MVS_ME_SOUND_PROFILE_M68000,
	MVS_ME_SOUND_PROFILE_Z80,
	MVS_ME_SOUND_PROFILE_SCHEDULER,
	MVS_ME_SOUND_PROFILE_METRIC_COUNT
} mvs_me_sound_profile_metric_t;

typedef enum mvs_me_sound_profile_event
{
	MVS_ME_SOUND_PROFILE_SOUND_COMMAND,
	MVS_ME_SOUND_PROFILE_SOUND_LATCH_APPLY,
	MVS_ME_SOUND_PROFILE_MAIN_STATUS_READ,
	MVS_ME_SOUND_PROFILE_Z80_COMMAND_READ,
	MVS_ME_SOUND_PROFILE_Z80_RESULT_WRITE,
	MVS_ME_SOUND_PROFILE_YM_STATUS_A_READ,
	MVS_ME_SOUND_PROFILE_YM_STATUS_B_READ,
	MVS_ME_SOUND_PROFILE_YM_DATA_READ,
	MVS_ME_SOUND_PROFILE_YM_TIMER_A,
	MVS_ME_SOUND_PROFILE_YM_TIMER_B,
	MVS_ME_SOUND_PROFILE_YM_IRQ_ASSERT,
	MVS_ME_SOUND_PROFILE_YM_IRQ_CLEAR,
	MVS_ME_SOUND_PROFILE_YM_TIMER_PREEMPT,
	MVS_ME_SOUND_PROFILE_Z80_TIMER_PREEMPT_MAIN,
	MVS_ME_SOUND_PROFILE_TIMER_SLICE,
	MVS_ME_SOUND_PROFILE_EVENT_COUNT
} mvs_me_sound_profile_event_t;

#ifdef PSP_ME_SOUND_PROFILE
uint64_t mvs_me_sound_profile_now_us(void);
void mvs_me_sound_profile_reset(void);
void mvs_me_sound_profile_add_time(mvs_me_sound_profile_metric_t metric,
	uint64_t elapsed_us);
void mvs_me_sound_profile_event(mvs_me_sound_profile_event_t event);
void mvs_me_sound_profile_frame_completed(void);
#else
static inline uint64_t mvs_me_sound_profile_now_us(void)
{
	return 0;
}

static inline void mvs_me_sound_profile_reset(void)
{
}

static inline void mvs_me_sound_profile_add_time(mvs_me_sound_profile_metric_t metric,
	uint64_t elapsed_us)
{
	(void)metric;
	(void)elapsed_us;
}

static inline void mvs_me_sound_profile_event(mvs_me_sound_profile_event_t event)
{
	(void)event;
}

static inline void mvs_me_sound_profile_frame_completed(void)
{
}
#endif

#endif /* MVS_ME_SOUND_PROFILE_H */
