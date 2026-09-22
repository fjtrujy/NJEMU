#define NEWLIB_PORT_AWARE 1

#include "emumain.h"
#include "common/memory_sizes.h"

#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <fileXio_rpc.h>
#include <osd_config.h>
#include <stdlib.h>
#include <string.h>
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

static uint64_t ps2_probe_largest_block(uint64_t limit) {
	uint32_t low_blocks = 0;
	uint32_t high_blocks = (uint32_t)(limit / CACHE_BLOCK_SIZE);

	while (low_blocks < high_blocks) {
		uint32_t mid_blocks = low_blocks + (high_blocks - low_blocks + 1u) / 2u;
		size_t bytes = (size_t)mid_blocks * CACHE_BLOCK_SIZE;
		void *probe = malloc(bytes);
		if (probe != NULL) {
			free(probe);
			low_blocks = mid_blocks;
		} else {
			high_blocks = mid_blocks - 1u;
		}
	}

	return (uint64_t)low_blocks * CACHE_BLOCK_SIZE;
}

static bool ps2_queryMemoryInfo(void *data, platform_memory_info_t *out) {
	const uint32_t baseline_reservation = 4u * 1024u * 1024u;
	uint32_t total = (uint32_t)GetMemorySize();
	uint64_t policy_cap = total > baseline_reservation ? total - baseline_reservation : 0;
	uint64_t largest;
	(void)data;

	if (out == NULL) {
		return false;
	}
	memset(out, 0, sizeof(*out));

	largest = ps2_probe_largest_block(policy_cap);
	out->physical_total_bytes = total;
	out->budget_cap_bytes = policy_cap;
	/* PS2SDK has no live total-user-heap query here. The largest successful
	 * allocation is a conservative estimate for both planning dimensions.
	 */
	out->free_bytes = largest;
	out->largest_free_block_bytes = largest;
	out->capabilities = PLATFORM_MEMORY_CAP_QUERY_FREE | PLATFORM_MEMORY_CAP_QUERY_LARGEST_BLOCK;
	out->reliability_flags = PLATFORM_MEMORY_FREE_IS_ESTIMATE | PLATFORM_MEMORY_LARGEST_IS_PROBED;
	platform_memory_info_normalize(out);
	return total != 0;
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
	ps2_queryMemoryInfo,
	ps2_getSystemLanguage,
};
