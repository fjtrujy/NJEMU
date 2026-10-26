#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "common/input_driver.h"
#include "common/emulator_options.h"

/* Keyboard and the first controller both operate player one; further pads
 * operate additional players. No gamepad is required for keyboard-only use. */
#define DESKTOP_MAX_CONTROLLERS 8

static uint8_t key_states[SDL_NUM_SCANCODES];

typedef struct desktop_input {
	SDL_GameController *controllers[DESKTOP_MAX_CONTROLLERS];
	SDL_JoystickID instance_ids[DESKTOP_MAX_CONTROLLERS];
	uint32_t count;
} desktop_input_t;

static void desktop_add_controller(desktop_input_t *desktop, int device_index)
{
	SDL_GameController *controller;
	SDL_Joystick *joystick;
	SDL_JoystickID id;
	uint32_t i;

	if (device_index < 0 || !SDL_IsGameController(device_index) ||
	    desktop->count == DESKTOP_MAX_CONTROLLERS)
		return;
	id = SDL_JoystickGetDeviceInstanceID(device_index);
	if (id < 0)
		return;
	for (i = 0; i < desktop->count; ++i)
		if (desktop->instance_ids[i] == id)
			return;

	controller = SDL_GameControllerOpen(device_index);
	if (!controller)
		return;
	joystick = SDL_GameControllerGetJoystick(controller);
	id = joystick ? SDL_JoystickInstanceID(joystick) : -1;
	if (id < 0) {
		SDL_GameControllerClose(controller);
		return;
	}
	i = desktop->count++;
	desktop->controllers[i] = controller;
	desktop->instance_ids[i] = id;
	printf("[desktop-input] controller %u connected: %s\n",
	       (unsigned)(i + 1),
	       SDL_GameControllerName(controller) ? SDL_GameControllerName(controller) : "unknown");
}

static void desktop_remove_controller(desktop_input_t *desktop, SDL_JoystickID id)
{
	uint32_t i, j;
	for (i = 0; i < desktop->count; ++i) {
		if (desktop->instance_ids[i] != id)
			continue;
		SDL_GameControllerClose(desktop->controllers[i]);
		for (j = i + 1; j < desktop->count; ++j) {
			desktop->controllers[j - 1] = desktop->controllers[j];
			desktop->instance_ids[j - 1] = desktop->instance_ids[j];
		}
		--desktop->count;
		desktop->controllers[desktop->count] = NULL;
		printf("[desktop-input] controller disconnected\n");
		return;
	}
}

static void desktop_refresh(void *data)
{
	desktop_input_t *desktop = (desktop_input_t *)data;
	uint32_t i = 0;
	int device;
	if (!desktop)
		return;
	/* Expensive discovery runs only at initialization or explicit refresh. */
	SDL_PumpEvents();
	while (i < desktop->count) {
		if (!SDL_GameControllerGetAttached(desktop->controllers[i]))
			desktop_remove_controller(desktop, desktop->instance_ids[i]);
		else
			++i;
	}
	for (device = 0; device < SDL_NumJoysticks(); ++device)
		desktop_add_controller(desktop, device);
}

static void *desktop_init(void)
{
	desktop_input_t *desktop = (desktop_input_t *)calloc(1, sizeof(*desktop));
	if (!desktop)
		return NULL;
	memset(key_states, 0, sizeof(key_states));
	SDL_GameControllerEventState(SDL_ENABLE);
	desktop_refresh(desktop);
	return desktop;
}

static void desktop_free(void *data)
{
	desktop_input_t *desktop = (desktop_input_t *)data;
	if (!desktop)
		return;
	while (desktop->count)
		desktop_remove_controller(desktop, desktop->instance_ids[0]);
	free(desktop);
}

static uint32_t desktop_controllerCount(void *data)
{
	const desktop_input_t *desktop = (const desktop_input_t *)data;
	if (!desktop)
		return 0;
	if (option_desktop_p1_input == DESKTOP_P1_INPUT_KEYBOARD)
		return desktop->count + 1;
	return desktop->count ? desktop->count : 1;
}

static void desktop_process_events(desktop_input_t *desktop)
{
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
		case SDL_QUIT:
			Loop = 0; /* LOOP_EXIT */
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			if ((unsigned)event.key.keysym.scancode < SDL_NUM_SCANCODES)
				key_states[event.key.keysym.scancode] = event.type == SDL_KEYDOWN;
			if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
				Loop = 0;
			break;
		case SDL_WINDOWEVENT:
			if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
				memset(key_states, 0, sizeof(key_states));
			break;
		case SDL_CONTROLLERDEVICEADDED:
			desktop_add_controller(desktop, event.cdevice.which);
			break;
		case SDL_CONTROLLERDEVICEREMOVED:
			desktop_remove_controller(desktop, event.cdevice.which);
			break;
		default:
			break;
		}
	}
}

static uint32_t desktop_keyboard_buttons(void)
{
	uint32_t buttons = 0;
	buttons |= key_states[SDL_SCANCODE_UP] ? PLATFORM_PAD_UP : 0;
	buttons |= key_states[SDL_SCANCODE_DOWN] ? PLATFORM_PAD_DOWN : 0;
	buttons |= key_states[SDL_SCANCODE_LEFT] ? PLATFORM_PAD_LEFT : 0;
	buttons |= key_states[SDL_SCANCODE_RIGHT] ? PLATFORM_PAD_RIGHT : 0;
	buttons |= key_states[SDL_SCANCODE_Z] ? PLATFORM_PAD_B1 : 0;
	buttons |= key_states[SDL_SCANCODE_X] ? PLATFORM_PAD_B2 : 0;
	buttons |= key_states[SDL_SCANCODE_C] ? PLATFORM_PAD_B3 : 0;
	buttons |= key_states[SDL_SCANCODE_S] ? PLATFORM_PAD_B4 : 0;
	buttons |= key_states[SDL_SCANCODE_A] ? PLATFORM_PAD_L : 0;
	buttons |= key_states[SDL_SCANCODE_D] ? PLATFORM_PAD_R : 0;
	buttons |= key_states[SDL_SCANCODE_RETURN] ? PLATFORM_PAD_START : 0;
	buttons |= key_states[SDL_SCANCODE_SPACE] ? PLATFORM_PAD_SELECT : 0;
	return buttons;
}

static uint32_t desktop_controller_buttons(SDL_GameController *pad)
{
	uint32_t buttons = 0;
#define PAD_BUTTON(sdl, njemu) \
	buttons |= SDL_GameControllerGetButton(pad, (sdl)) ? (njemu) : 0
	PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_UP, PLATFORM_PAD_UP);
	PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_DOWN, PLATFORM_PAD_DOWN);
	PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_LEFT, PLATFORM_PAD_LEFT);
	PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PLATFORM_PAD_RIGHT);
	PAD_BUTTON(SDL_CONTROLLER_BUTTON_B, PLATFORM_PAD_B1);
	PAD_BUTTON(SDL_CONTROLLER_BUTTON_A, PLATFORM_PAD_B2);
	PAD_BUTTON(SDL_CONTROLLER_BUTTON_X, PLATFORM_PAD_B3);
	PAD_BUTTON(SDL_CONTROLLER_BUTTON_Y, PLATFORM_PAD_B4);
	PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PLATFORM_PAD_L);
	PAD_BUTTON(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PLATFORM_PAD_R);
	PAD_BUTTON(SDL_CONTROLLER_BUTTON_BACK, PLATFORM_PAD_SELECT);
	PAD_BUTTON(SDL_CONTROLLER_BUTTON_START, PLATFORM_PAD_START);
#undef PAD_BUTTON
	return buttons;
}

static bool desktop_sample(void *data, uint32_t controller, input_state_t *state)
{
	desktop_input_t *desktop = (desktop_input_t *)data;
	SDL_GameController *pad;
	uint32_t gamepad_index = controller;
	Sint16 lx, ly;
	if (!desktop || !state)
		return false;
	/* P1 consumes hotplug and keyboard events before sampling other players. */
	if (controller == 0)
		desktop_process_events(desktop);
	if (controller >= desktop_controllerCount(desktop))
		return false;
	input_state_reset(state);
	if (controller == 0 &&
	    (option_desktop_p1_input != DESKTOP_P1_INPUT_CONTROLLER || !desktop->count))
		state->buttons = desktop_keyboard_buttons();
	if (option_desktop_p1_input == DESKTOP_P1_INPUT_KEYBOARD) {
		if (controller == 0)
			return true;
		gamepad_index = controller - 1;
	}
	if (gamepad_index >= desktop->count)
		return true;
	pad = desktop->controllers[gamepad_index];
	state->buttons |= desktop_controller_buttons(pad);
	lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
	ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
	state->lx = (uint8_t)(((int32_t)lx + 32768) >> 8);
	state->ly = (uint8_t)(((int32_t)ly + 32768) >> 8);
	state->axis_flags = INPUT_AXIS_LX | INPUT_AXIS_LY;
	return true;
}

input_driver_t input_desktop = {
	"desktop",
	desktop_init,
	desktop_free,
	desktop_refresh,
	desktop_controllerCount,
	desktop_sample,
	NULL,
	NULL,
};
