#ifndef MVS_ME_SOUND_SHADOW_H
#define MVS_ME_SOUND_SHADOW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef PSP_ME_SOUND_COPROCESSOR
bool mvs_me_sound_shadow_command(uint8_t command, uint64_t emulated_time);
void mvs_me_sound_shadow_frame_completed(void);
#else
static inline bool mvs_me_sound_shadow_command(uint8_t command,
	uint64_t emulated_time)
{
	(void)command;
	(void)emulated_time;
	return true;
}

static inline void mvs_me_sound_shadow_frame_completed(void)
{
}
#endif

#endif /* MVS_ME_SOUND_SHADOW_H */
