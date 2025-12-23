#include <stdio.h>
#include <stdlib.h>
#include <SDL.h>
#include "common/input_driver.h"

typedef struct desktop_input {
} desktop_input_t;

static void *desktop_init(void) {
	desktop_input_t *desktop = (desktop_input_t*)calloc(1, sizeof(desktop_input_t));
	return desktop;
}

static void desktop_free(void *data) {
	free(data);
}

static uint32_t desktop_poll(void *data) {
	uint32_t btnsData = 0;
	SDL_Event event;
	SDL_PollEvent(&event);

	btnsData |= (event.key.keysym.sym == SDLK_UP) ? PLATFORM_PAD_UP : 0;
	btnsData |= (event.key.keysym.sym == SDLK_DOWN) ? PLATFORM_PAD_DOWN : 0;
	btnsData |= (event.key.keysym.sym == SDLK_LEFT) ? PLATFORM_PAD_LEFT : 0;
	btnsData |= (event.key.keysym.sym == SDLK_RIGHT) ? PLATFORM_PAD_RIGHT : 0;

	btnsData |= (event.key.keysym.sym == SDLK_z) ? PLATFORM_PAD_B1 : 0;
	btnsData |= (event.key.keysym.sym == SDLK_x) ? PLATFORM_PAD_B2 : 0;
	btnsData |= (event.key.keysym.sym == SDLK_c) ? PLATFORM_PAD_B3 : 0;
	btnsData |= (event.key.keysym.sym == SDLK_s) ? PLATFORM_PAD_B4 : 0;

	btnsData |= (event.key.keysym.sym == SDLK_a) ? PLATFORM_PAD_L : 0;
	btnsData |= (event.key.keysym.sym == SDLK_d) ? PLATFORM_PAD_R : 0;

	btnsData |= (event.key.keysym.sym == SDLK_RETURN) ? PLATFORM_PAD_START : 0;
	btnsData |= (event.key.keysym.sym == SDLK_SPACE) ? PLATFORM_PAD_SELECT : 0;

	return btnsData;
}

#if (EMU_SYSTEM == MVS)
static uint32_t desktop_pollFatfursp(void *data) {
	uint32_t btnsData = 0;
	return btnsData;
}

static uint32_t desktop_pollAnalog(void *data) {
	uint32_t btnsData;
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