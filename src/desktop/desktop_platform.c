#include "emumain.h"
#include <SDL.h>

typedef struct desktop_platform {
} desktop_platform_t;

static void *desktop_init(void) {
	desktop_platform_t *desktop = (desktop_platform_t*)calloc(1, sizeof(desktop_platform_t));

	// Initialize SDL for video, audio, and controller subsystems
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) {
        printf("SDL initialization failed: %s\n", SDL_GetError());
		free(desktop);
        return NULL;
    }

	return desktop;
}

static void desktop_free(void *data) {
	desktop_platform_t *desktop = (desktop_platform_t*)data;

    SDL_Quit();

	free(desktop);
}

static void desktop_main(void *data, int argc, char *argv[]) {
	desktop_platform_t *desktop = (desktop_platform_t*)data;
    
	getcwd(screenshotDir, sizeof(screenshotDir));
    strcat(screenshotDir, "/PICTURE");
    mkdir(screenshotDir, 0777);
#if	(EMU_SYSTEM == CPS1)
	strcat(screenshotDir, "/CPS1");
#endif
#if	(EMU_SYSTEM == CPS2)
	strcat(screenshotDir, "/CPS2");
#endif
#if	(EMU_SYSTEM == MVS)
	strcat(screenshotDir, "/MVS");
#endif
#if	(EMU_SYSTEM == NCDZ)
	strcat(screenshotDir, "/NCDZ");
#endif
}

static bool desktop_startSystemButtons(void *data) {
return false;
}

static int32_t desktop_getDevkitVersion(void *data) {
	return 0;
}

platform_driver_t platform_desktop = {
	"desktop",
	desktop_init,
	desktop_free,
	desktop_main,
	desktop_startSystemButtons,
	desktop_getDevkitVersion,
};
