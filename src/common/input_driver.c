/******************************************************************************

	input_driver.c

******************************************************************************/

#include <stddef.h>
#include <time.h>

#include "input_driver.h"
#include "ticker_driver.h"
#include "video_driver.h"

/******************************************************************************
	Local Variables
******************************************************************************/

static uint32_t pad;
static uint8_t pressed_check;
static uint8_t pressed_count;
static uint8_t pressed_delay;
static uint64_t curr_time;
static uint64_t prev_time;
static bool menu_combo_down;

void *input_info;


/******************************************************************************
	Global Functions
******************************************************************************/

/*--------------------------------------------------------
	Initialize Pad
--------------------------------------------------------*/

bool pad_init(void)
{
	pad = 0;
	pressed_check = 0;
	pressed_count = 0;
	pressed_delay = 0;
	menu_combo_down = false;
	input_info = input_driver->init();
	return input_info != NULL;
}

void pad_exit(void)
{
	input_driver->free(input_info);
	input_info = NULL;
}

void refresh_gamepads(void)
{
	if (input_info && input_driver->refresh)
		input_driver->refresh(input_info);
}

/*--------------------------------------------------------
	Get Number of Physical Controllers
--------------------------------------------------------*/

uint32_t gamepad_count(void)
{
	if (!input_info || !input_driver->controllerCount)
		return 0;

	return input_driver->controllerCount(input_info);
}


/*--------------------------------------------------------
	Get Pad Press State
--------------------------------------------------------*/

uint32_t poll_gamepad(void)
{
	return poll_gamepad_index(0);
}

bool sample_gamepad_index(uint32_t controller, input_state_t *state)
{
	if (state == NULL)
		return false;

	input_state_reset(state);

	if (!input_info || !input_driver->sample)
		return false;

	return input_driver->sample(input_info, controller, state);
}

uint32_t poll_gamepad_index(uint32_t controller)
{
	input_state_t state;

	if (!sample_gamepad_index(controller, &state))
		return 0;

	return input_state_digital_buttons(&state);
}


/*--------------------------------------------------------
	Update Pad Press Information
--------------------------------------------------------*/

void pad_update(void)
{
	uint32_t data;

	data = poll_gamepad();

	if (data)
	{
		if (!pressed_check)
		{
			pressed_check = 1;
			pressed_count = 0;
			pressed_delay = 8;
			prev_time = ticker_driver->currentUs(ticker_data);
		}
		else
		{
			int count;

			curr_time = ticker_driver->currentUs(ticker_data);
			count = (int)((curr_time - prev_time) / (CLOCKS_PER_SEC / 60));
			prev_time = curr_time;

			pressed_count += count;

			if (pressed_count > pressed_delay)
			{
				pressed_count = 0;
				if (pressed_delay > 2) pressed_delay -= 2;
			}
			else data = 0;
		}
	}
	else pressed_check = 0;

	pad = data;
}


/*--------------------------------------------------------
	Get Button Press State
--------------------------------------------------------*/

bool pad_pressed(uint32_t code)
{
	return (pad & code) != 0;
}

/*--------------------------------------------------------
	Get edge-triggered menu combo
--------------------------------------------------------*/

bool pad_menu_combo_pressed(uint32_t buttons)
{
	bool down = (buttons & PLATFORM_PAD_START) &&
		(buttons & PLATFORM_PAD_SELECT);
	bool pressed = down && !menu_combo_down;

	menu_combo_down = down;
	return pressed;
}


#define PLATFORM_PAD_ANY			\
	(								\
		PLATFORM_PAD_SELECT |	\
	 	PLATFORM_PAD_START |	\
	 	PLATFORM_PAD_UP |		\
	 	PLATFORM_PAD_RIGHT |	\
	 	PLATFORM_PAD_DOWN |	\
	 	PLATFORM_PAD_LEFT |	\
	 	PLATFORM_PAD_L |		\
	 	PLATFORM_PAD_R |		\
	 	PLATFORM_PAD_B4 |		\
	 	PLATFORM_PAD_B1 |		\
	 	PLATFORM_PAD_B2 |		\
	 	PLATFORM_PAD_B3 		\
	)

bool pad_pressed_any(void)
{
	return (pad & PLATFORM_PAD_ANY) != 0;
}

/*--------------------------------------------------------
	Wait Until Button Press State is Cleared
--------------------------------------------------------*/

void pad_wait_clear(void)
{
	while (poll_gamepad())
	{
		video_driver->waitVsync(video_data);
		if (!Loop) break;
	}

	pad = 0;
	pressed_check = 0;
}


/*--------------------------------------------------------
	Wait Until Any Button is Pressed
--------------------------------------------------------*/

void pad_wait_press(int msec)
{
	pad_wait_clear();

	if (msec == PAD_WAIT_INFINITY)
	{
		while (!poll_gamepad())
		{
			video_driver->waitVsync(video_data);
			if (!Loop) break;
		}
	}
	else
	{
		uint64_t target = ticker_driver->currentUs(ticker_data) + (msec * 1000);

		while (ticker_driver->currentUs(ticker_data) < target)
		{
			video_driver->waitVsync(video_data);
			if (poll_gamepad()) break;
			if (!Loop) break;
		}
	}

	pad_wait_clear();
}
