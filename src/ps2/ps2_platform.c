#define NEWLIB_PORT_AWARE 1

#include "emumain.h"
#include "common/memory_sizes.h"

#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <fileXio_rpc.h>
#include <osd_config.h>
#include <ps2_filesystem_driver.h>
#include <ps2_audio_driver.h>

typedef struct ps2_platform {
} ps2_platform_t;

static void reset_IOP()
{
    SifInitRpc(0);
	/* Comment this line if you don't wanna debug the output */
    while (!SifIopReset(NULL, 0)) {}
    while (!SifIopSync()) {}
}

static void prepare_IOP()
{
    reset_IOP();
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    sbv_patch_fileio();
}

static bool init_drivers()
{
	init_only_boot_ps2_filesystem_driver();
	if (init_audio_driver() != AUDIO_INIT_STATUS_OK) {
		deinit_only_boot_ps2_filesystem_driver();
		return false;
	}

	fileXioSetRWBufferSize(CACHE_BLOCK_SIZE); // Match cache block size for better performance
	return true;
}

static void deinit_drivers()
{
	deinit_audio_driver();
	deinit_only_boot_ps2_filesystem_driver();
}

static void *ps2_init(void) {
	ps2_platform_t *ps2 = (ps2_platform_t*)calloc(1, sizeof(ps2_platform_t));
	if (ps2 == NULL)
		return NULL;

    prepare_IOP();
	if (!init_drivers()) {
		free(ps2);
		return NULL;
	}

	return ps2;
}

static void ps2_free(void *data) {
	ps2_platform_t *ps2 = (ps2_platform_t*)data;

    deinit_drivers();

	free(ps2);
}

static void ps2_main(void *data, int argc, char *argv[]) {
	ps2_platform_t *ps2 = (ps2_platform_t*)data;
	char picture_dir[PATH_MAX];
	const char *system_dir;

	(void)ps2;
	(void)argc;
	(void)argv;

	if (snprintf(picture_dir, sizeof(picture_dir), "%sPICTURE", launchDir) >=
		(int)sizeof(picture_dir)) {
		screenshotDir[0] = '\0';
		return;
	}
	mkdir(picture_dir, 0777);

#if	(EMU_SYSTEM == CPS1)
	system_dir = "CPS1";
#endif
#if	(EMU_SYSTEM == CPS2)
	system_dir = "CPS2";
#endif
#if	(EMU_SYSTEM == MVS)
	system_dir = "MVS";
#endif
#if	(EMU_SYSTEM == NCDZ)
	system_dir = "NCDZ";
#endif

	if (snprintf(screenshotDir, sizeof(screenshotDir), "%s/%s",
		picture_dir, system_dir) >= (int)sizeof(screenshotDir)) {
		screenshotDir[0] = '\0';
	}
}

static bool ps2_startSystemButtons(void *data) {
return false;
}

static int32_t ps2_getDevkitVersion(void *data) {
	return 0;
}

static bool ps2_getWlanSwitchState(void *data) {
	return false;
}

static int ps2_getHardwareModel(void *data) {
	return 0;
}

static uint32_t ps2_availableRam(void *data) {
	/* PS2 has a fixed 32 MB main RAM. GetMemorySize() returns the physical
	 * total; we hold back a baseline for kernel, stacks, and late mallocs.
	 */
	const uint32_t baseline_reservation = 4u * 1024u * 1024u;
	uint32_t total = (uint32_t)GetMemorySize();
	if (total <= baseline_reservation) {
		return 0;
	}
	return total - baseline_reservation;
}

static ui_language_t ps2_getSystemLanguage(void *data) {
	(void)data;
	switch (configGetLanguage()) {
	case LANGUAGE_JAPANESE:
		return UI_LANG_JAPANESE;
	case LANGUAGE_SPANISH:
		return UI_LANG_SPANISH;
	case LANGUAGE_SIMPL_CHINESE:
		return UI_LANG_CHINESE_SIMPLIFIED;
	case LANGUAGE_TRAD_CHINESE:
		return UI_LANG_CHINESE_TRADITIONAL;
	default:
		return UI_LANG_ENGLISH;
	}
}

platform_driver_t platform_ps2 = {
	"ps2",
	ps2_init,
	ps2_free,
	ps2_main,
	ps2_startSystemButtons,
	ps2_getDevkitVersion,
	ps2_getWlanSwitchState,
	ps2_getHardwareModel,
	ps2_availableRam,
	ps2_getSystemLanguage,
};
