#include <assert.h>
#include <stdint.h>

#include "common/input_state.h"

static void test_reset(void)
{
	input_state_t state = { UINT32_MAX, 0, 0xff, 0xff };

	input_state_reset(&state);
	assert(state.buttons == 0);
	assert(state.lx == INPUT_ANALOG_NEUTRAL);
	assert(state.ly == INPUT_ANALOG_NEUTRAL);
	assert(state.axis_flags == 0);
}

static void test_digital_buttons_preserved(void)
{
	input_state_t state = {
		.buttons = PLATFORM_PAD_B1 | PLATFORM_PAD_START,
		.lx = INPUT_ANALOG_NEUTRAL,
		.ly = INPUT_ANALOG_NEUTRAL,
		.axis_flags = INPUT_AXIS_LX | INPUT_AXIS_LY,
	};

	assert(input_state_digital_buttons(&state) ==
		(PLATFORM_PAD_B1 | PLATFORM_PAD_START));
}

static void test_axis_thresholds(void)
{
	input_state_t state = {0};

	input_state_reset(&state);
	state.axis_flags = INPUT_AXIS_LX | INPUT_AXIS_LY;
	state.lx = INPUT_ANALOG_LOW_THRESHOLD;
	state.ly = INPUT_ANALOG_HIGH_THRESHOLD;
	assert(input_state_digital_buttons(&state) ==
		(PLATFORM_PAD_LEFT | PLATFORM_PAD_DOWN));

	state.lx = INPUT_ANALOG_HIGH_THRESHOLD;
	state.ly = INPUT_ANALOG_LOW_THRESHOLD;
	assert(input_state_digital_buttons(&state) ==
		(PLATFORM_PAD_RIGHT | PLATFORM_PAD_UP));
}

static void test_missing_axes_are_ignored(void)
{
	input_state_t state = {
		.buttons = PLATFORM_PAD_B2,
		.lx = 0,
		.ly = 0xff,
		.axis_flags = 0,
	};

	assert(input_state_digital_buttons(&state) == PLATFORM_PAD_B2);
}

int main(void)
{
	test_reset();
	test_digital_buttons_preserved();
	test_axis_thresholds();
	test_missing_axes_are_ignored();
	return 0;
}
