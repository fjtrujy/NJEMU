#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include "common/input_driver.h"

/* External reference to the main loop control variable */
extern volatile int Loop;

/* Key states - track which keys are currently pressed */
static uint8_t key_states[SDL_NUM_SCANCODES];

typedef struct desktop_input {
	int initialized;
} desktop_input_t;

static void *desktop_init(void) {
	desktop_input_t *desktop = (desktop_input_t*)calloc(1, sizeof(desktop_input_t));
	memset(key_states, 0, sizeof(key_states));
	desktop->initialized = 1;
	return desktop;
}

static void desktop_free(void *data) {
	free(data);
}

static uint32_t desktop_poll(void *data) {
	uint32_t btnsData = 0;
	SDL_Event event;
	
	/* Process all pending events */
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_QUIT:
				/* Window close button clicked - exit the emulator */
				Loop = 0;  /* LOOP_EXIT */
				break;
				
			case SDL_KEYDOWN:
				if (event.key.keysym.scancode < SDL_NUM_SCANCODES) {
					key_states[event.key.keysym.scancode] = 1;
				}
				/* ESC key also exits */
				if (event.key.keysym.sym == SDLK_ESCAPE) {
					Loop = 0;  /* LOOP_EXIT */
				}
				break;
				
			case SDL_KEYUP:
				if (event.key.keysym.scancode < SDL_NUM_SCANCODES) {
					key_states[event.key.keysym.scancode] = 0;
				}
				break;
		}
	}
	
	/* Build button state from current key states */
	btnsData |= key_states[SDL_SCANCODE_UP] ? PLATFORM_PAD_UP : 0;
	btnsData |= key_states[SDL_SCANCODE_DOWN] ? PLATFORM_PAD_DOWN : 0;
	btnsData |= key_states[SDL_SCANCODE_LEFT] ? PLATFORM_PAD_LEFT : 0;
	btnsData |= key_states[SDL_SCANCODE_RIGHT] ? PLATFORM_PAD_RIGHT : 0;

	btnsData |= key_states[SDL_SCANCODE_Z] ? PLATFORM_PAD_B1 : 0;
	btnsData |= key_states[SDL_SCANCODE_X] ? PLATFORM_PAD_B2 : 0;
	btnsData |= key_states[SDL_SCANCODE_C] ? PLATFORM_PAD_B3 : 0;
	btnsData |= key_states[SDL_SCANCODE_S] ? PLATFORM_PAD_B4 : 0;

	btnsData |= key_states[SDL_SCANCODE_A] ? PLATFORM_PAD_L : 0;
	btnsData |= key_states[SDL_SCANCODE_D] ? PLATFORM_PAD_R : 0;

	btnsData |= key_states[SDL_SCANCODE_RETURN] ? PLATFORM_PAD_START : 0;
	btnsData |= key_states[SDL_SCANCODE_SPACE] ? PLATFORM_PAD_SELECT : 0;

	return btnsData;
}

#if (EMU_SYSTEM == MVS)
static uint32_t desktop_pollFatfursp(void *data) {
	uint32_t btnsData = 0;
	return btnsData;
}

static uint32_t desktop_pollAnalog(void *data) {
	uint32_t btnsData = 0;
	return btnsData;
}
#endif


input_driver_t input_desktop = {
	"desktop",
	desktop_init,
	desktop_free,
	desktop_poll,
#if (EMU_SYSTEM == MVS)
	desktop_pollFatfursp,
	desktop_pollAnalog,
#endif
};
