#ifndef COMMON_CPS_ORIENTATION_H
#define COMMON_CPS_ORIENTATION_H

#include <stdbool.h>
#include <stdint.h>

#include "input_state.h"

/*
 * Presentation preference for games whose native raster is vertical.
 *
 * Values 0 and 1 intentionally preserve the historical RotateScreen config
 * semantics so existing per-game configuration files remain compatible.
 */
enum {
	CPS_SCREEN_ORIENTATION_ROTATED_DISPLAY = 0,
	CPS_SCREEN_ORIENTATION_UPRIGHT = 1,
	CPS_SCREEN_ORIENTATION_AUTO = 2,
	CPS_SCREEN_ORIENTATION_COUNT
};

static inline bool cps_orientation_rotates_to_upright(bool game_is_vertical,
	int screen_orientation)
{
	if (!game_is_vertical)
		return false;

	return screen_orientation != CPS_SCREEN_ORIENTATION_ROTATED_DISPLAY;
}

/*
 * Transform physical controls to match the presentation shown to the user.
 * A vertical game shown upright needs no 90-degree remap. A vertical game
 * shown sideways is intended for a physically rotated display/device, so the
 * controls are rotated to remain intuitive after the display is turned.
 */
static inline uint32_t cps_orientation_adjust_buttons(uint32_t buttons,
	bool game_is_vertical, bool rotates_to_upright, bool flip_screen)
{
	uint32_t adjusted;

	if (game_is_vertical && !rotates_to_upright) {
		adjusted = buttons &
			(PLATFORM_PAD_START | PLATFORM_PAD_SELECT |
			 PLATFORM_PAD_R | PLATFORM_PAD_L);

		if (buttons & PLATFORM_PAD_UP)    adjusted |= PLATFORM_PAD_LEFT;
		if (buttons & PLATFORM_PAD_DOWN)  adjusted |= PLATFORM_PAD_RIGHT;
		if (buttons & PLATFORM_PAD_RIGHT) adjusted |= PLATFORM_PAD_UP;
		if (buttons & PLATFORM_PAD_LEFT)  adjusted |= PLATFORM_PAD_DOWN;
		if (buttons & PLATFORM_PAD_B4)    adjusted |= PLATFORM_PAD_B3;
		if (buttons & PLATFORM_PAD_B1)    adjusted |= PLATFORM_PAD_B4;
		if (buttons & PLATFORM_PAD_B3)    adjusted |= PLATFORM_PAD_B2;
		if (buttons & PLATFORM_PAD_B2)    adjusted |= PLATFORM_PAD_B1;

		buttons = adjusted;
	}

	if (flip_screen) {
		adjusted = buttons & (PLATFORM_PAD_START | PLATFORM_PAD_SELECT);

		if (buttons & PLATFORM_PAD_UP)    adjusted |= PLATFORM_PAD_DOWN;
		if (buttons & PLATFORM_PAD_DOWN)  adjusted |= PLATFORM_PAD_UP;
		if (buttons & PLATFORM_PAD_RIGHT) adjusted |= PLATFORM_PAD_LEFT;
		if (buttons & PLATFORM_PAD_LEFT)  adjusted |= PLATFORM_PAD_RIGHT;
		if (buttons & PLATFORM_PAD_B3)    adjusted |= PLATFORM_PAD_B1;
		if (buttons & PLATFORM_PAD_B1)    adjusted |= PLATFORM_PAD_B3;
		if (buttons & PLATFORM_PAD_B4)    adjusted |= PLATFORM_PAD_B2;
		if (buttons & PLATFORM_PAD_B2)    adjusted |= PLATFORM_PAD_B4;
		if (buttons & PLATFORM_PAD_R)     adjusted |= PLATFORM_PAD_L;
		if (buttons & PLATFORM_PAD_L)     adjusted |= PLATFORM_PAD_R;

		buttons = adjusted;
	}

	return buttons;
}

#endif /* COMMON_CPS_ORIENTATION_H */
