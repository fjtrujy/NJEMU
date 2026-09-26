/******************************************************************************

	input_state.c

******************************************************************************/

#include <stddef.h>

#include "input_state.h"

void input_state_reset(input_state_t *state)
{
	if (state == NULL)
		return;

	state->buttons = 0;
	state->lx = INPUT_ANALOG_NEUTRAL;
	state->ly = INPUT_ANALOG_NEUTRAL;
	state->axis_flags = 0;
}

uint32_t input_state_digital_buttons(const input_state_t *state)
{
	uint32_t buttons;

	if (state == NULL)
		return 0;

	buttons = state->buttons;
	if (state->axis_flags & INPUT_AXIS_LY)
	{
		if (state->ly >= INPUT_ANALOG_HIGH_THRESHOLD)
			buttons |= PLATFORM_PAD_DOWN;
		if (state->ly <= INPUT_ANALOG_LOW_THRESHOLD)
			buttons |= PLATFORM_PAD_UP;
	}
	if (state->axis_flags & INPUT_AXIS_LX)
	{
		if (state->lx <= INPUT_ANALOG_LOW_THRESHOLD)
			buttons |= PLATFORM_PAD_LEFT;
		if (state->lx >= INPUT_ANALOG_HIGH_THRESHOLD)
			buttons |= PLATFORM_PAD_RIGHT;
	}

	return buttons;
}
