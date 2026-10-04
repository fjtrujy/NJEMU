#ifndef COMMON_FRAME_PACING_H
#define COMMON_FRAME_PACING_H

#include <stdbool.h>
#include <stdint.h>

#include "common/emulator_options.h"

/* Presentation and emulation pacing are independent controls:
 *
 *   VSync Off:      never wait for VBlank
 *   VSync On:       preserve the legacy behavior: always wait when uncapped, or use
 *                   VBlank only while safely ahead when the software limiter is on
 *   VSync Adaptive: wait for VBlank only while safely ahead of the emulated frame
 *                   deadline, even when the software limiter is off
 *
 * Frame limiting remains independent: it controls only software sleeping. Adaptive
 * VSync therefore gives uncapped emulation a tear-free fast path without forcing a
 * late frame to wait for a whole extra refresh interval.
 */
#define FRAME_PACING_VSYNC_GUARD_US 100u

static inline bool frame_pacing_should_sync_flip(bool limit_enabled,
		int vsync_mode, uint64_t now_us, uint64_t target_us)
{
	if (vsync_mode == VSYNC_MODE_OFF)
		return false;
	if (vsync_mode != VSYNC_MODE_ON && vsync_mode != VSYNC_MODE_ADAPTIVE)
		return false;
	if (vsync_mode == VSYNC_MODE_ON && !limit_enabled)
		return true;
	if (target_us <= now_us)
		return false;
	return target_us - now_us > FRAME_PACING_VSYNC_GUARD_US;
}

static inline uint64_t frame_pacing_sleep_us(bool limit_enabled,
	uint64_t now_us, uint64_t target_us)
{
	if (!limit_enabled || target_us <= now_us)
		return 0;
	return target_us - now_us;
}

#endif /* COMMON_FRAME_PACING_H */
