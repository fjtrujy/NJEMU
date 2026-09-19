#define NEWLIB_PORT_AWARE 1

#include "emumain.h"
#include "common/memory_sizes.h"

#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <fileXio_rpc.h>
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
};
