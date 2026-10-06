#ifndef COMMON_AUTO_FRAMESKIP_H
#define COMMON_AUTO_FRAMESKIP_H

#include <stdint.h>

#define AUTO_FRAMESKIP_LEVEL_COUNT 12

static inline float auto_frameskip_frame_rate(uint32_t frames, uint64_t elapsed_us)
{
	if (elapsed_us == 0)
		return 0.0f;

	return ((float)frames * 1000000.0f) / (float)elapsed_us;
}

static inline float auto_frameskip_speed_percent(uint32_t emulated_frames,
	uint64_t elapsed_us, float target_fps)
{
	if (target_fps <= 0.0f)
		return 0.0f;

	return (auto_frameskip_frame_rate(emulated_frames, elapsed_us) / target_fps) * 100.0f;
}

/* Preserve the historical NJEMU/MAME-style recovery policy while keeping its
 * input explicitly tied to emulation speed. Presentation FPS must never feed
 * this controller: skipped video frames are the output of the policy, not
 * evidence that emulation itself is running slowly. */
static inline void auto_frameskip_adjust_level(int *level, int *adjustment,
	float speed_percent)
{
	if (speed_percent >= 99.0f) {
		(*adjustment)++;
		if (*adjustment >= 3) {
			*adjustment = 0;
			if (*level > 0)
				(*level)--;
		}
		return;
	}

	if (speed_percent < 80.0f) {
		int penalty = (int)((90.0f - speed_percent) / 5.0f);
		if (penalty < 1)
			penalty = 1;
		*adjustment -= penalty;
	} else if (*level < 8) {
		(*adjustment)--;
	}

	while (*adjustment <= -2) {
		*adjustment += 2;
		if (*level < AUTO_FRAMESKIP_LEVEL_COUNT - 1)
			(*level)++;
	}
}

#endif /* COMMON_AUTO_FRAMESKIP_H */
