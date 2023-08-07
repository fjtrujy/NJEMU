#include "emumain.h"

typedef struct ps2_platform {
} ps2_platform_t;

static void *ps2_init(void) {
	ps2_platform_t *ps2 = (ps2_platform_t*)calloc(1, sizeof(ps2_platform_t));
	return ps2;
}

static void ps2_free(void *data) {
	ps2_platform_t *ps2 = (ps2_platform_t*)data;
	free(ps2);
}

static void ps2_main(void *data, int argc, char *argv[]) {
	ps2_platform_t *ps2 = (ps2_platform_t*)data;
    
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

static bool ps2_startSystemButtons(void *data) {
return false;
}

static int32_t ps2_getDevkitVersion(void *data) {
	return 0;
}

platform_driver_t platform_ps2 = {
	"ps2",
	ps2_init,
	ps2_free,
	ps2_main,
	ps2_startSystemButtons,
	ps2_getDevkitVersion,
};
