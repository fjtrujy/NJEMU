#ifndef PSP_ME_SOUND_LIFECYCLE_H
#define PSP_ME_SOUND_LIFECYCLE_H

#include <stdbool.h>

typedef enum psp_me_sound_reset_action
{
	PSP_ME_SOUND_RESET_KEEP_CPU = 0,
	PSP_ME_SOUND_RESET_STOP_WORKER,
	PSP_ME_SOUND_RESET_RESET_WORKER,
	PSP_ME_SOUND_RESET_START_WORKER,
	PSP_ME_SOUND_RESET_RESTART_WORKER,
	PSP_ME_SOUND_RESET_DEFER_SUSPENDED
} psp_me_sound_reset_action_t;

static inline psp_me_sound_reset_action_t psp_me_sound_reset_action(
	bool mode_enabled, bool me_available, bool worker_running, bool suspended)
{
	if (!mode_enabled)
		return worker_running ? PSP_ME_SOUND_RESET_STOP_WORKER :
			PSP_ME_SOUND_RESET_KEEP_CPU;
	if (suspended)
		return worker_running ? PSP_ME_SOUND_RESET_STOP_WORKER :
			PSP_ME_SOUND_RESET_DEFER_SUSPENDED;
	if (me_available)
		return worker_running ? PSP_ME_SOUND_RESET_RESET_WORKER :
			PSP_ME_SOUND_RESET_START_WORKER;
	return worker_running ? PSP_ME_SOUND_RESET_RESTART_WORKER :
		PSP_ME_SOUND_RESET_START_WORKER;
}

#endif /* PSP_ME_SOUND_LIFECYCLE_H */
