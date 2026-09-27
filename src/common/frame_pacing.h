#ifndef COMMON_FRAME_PACING_H
#define COMMON_FRAME_PACING_H

#include <stdbool.h>
#include <stdint.h>

/* Presentation and emulation pacing are independent controls:
 *
 *   limit off, vsync off: present immediately; emulation is intentionally uncapped
 *   limit off, vsync on : wait for VBlank when presenting
 *   limit on,  vsync off: sleep to the emulated system's frame deadline
 *   limit on,  vsync on : use VBlank while comfortably ahead, then sleep only any
 *                         remaining time after that wait; never pay both waits using
 *                         the same stale timestamp
 *
 * When already at/near the software deadline, a limited+VSync frame presents
 * immediately instead of risking a whole extra refresh interval.
 */
#define FRAME_PACING_VSYNC_GUARD_US 100u

static inline bool frame_pacing_should_sync_flip(bool limit_enabled,
	bool vsync_enabled, uint64_t now_us, uint64_t target_us)
{
	if (!vsync_enabled)
		return false;
	if (!limit_enabled)
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
