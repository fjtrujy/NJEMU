#define NEWLIB_PORT_AWARE 1

#include "emucfg.h"
#include "common/memory_sizes.h"
#include "common/platform_driver.h"
#include "common/runtime_paths.h"
#include "ps2/ps2_cache_storage.h"

#include <kernel.h>
#include <sifrpc.h>
#include <iopcontrol.h>
#include <sbv_patches.h>
#include <osd_config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ps2_filesystem_driver.h>
#include <ps2_audio_driver.h>
#if defined(PS2_FAST_CACHE)
#include <ps2_cacheio_driver.h>
#endif
#include <ps2_boot_device.h>
#if defined(PS2_EXTERNAL_IRX_IMAGE)
#include <ps2_drivers_img.h>
#endif

#define PS2_MAIN_THREAD_PRIORITY 0x20

typedef struct ps2_platform {
} ps2_platform_t;

#if defined(PS2_FAST_CACHE)
static bool cacheio_driver_initialized;
static bool cacheio_driver_requested;
#endif

static void reset_IOP()
{
    SifInitRpc(0);
	/* Comment this line if you don't wanna debug the output */
    while (!SifIopReset(NULL, 0)) {}
    while (!SifIopSync()) {}
}

static bool stage_drivers_before_iop_reset(void)
{
#if defined(PS2_EXTERNAL_IRX_IMAGE)
	uint32_t application_requirements =
		PS2_DRIVER_REQ_AUDIO | PS2_DRIVER_REQ_JOYSTICK;
	int result;

#if defined(PS2_FAST_CACHE)
	{
		uint32_t boot_requirements = 0u;

		cacheio_driver_requested = false;
		if (ps2_drivers_img_requirements_for_current_boot(
			0u, &boot_requirements) == PS2_DRIVERS_IMG_OK &&
			(boot_requirements & (PS2_DRIVER_REQ_USB | PS2_DRIVER_REQ_MX4SIO)) != 0u) {
			application_requirements |= PS2_DRIVER_REQ_CACHEIO;
			cacheio_driver_requested = true;
		}
	}
#endif

	result = ps2_drivers_img_stage_default_for_current_boot(application_requirements);
	if (result != PS2_DRIVERS_IMG_OK) {
		printf("[ps2_drivers] IRX staging failed: %s (%d)\n",
			ps2_drivers_img_error_string(result), result);
		return false;
	}

	return true;
#else
#if defined(PS2_FAST_CACHE)
	char cwd[FILENAME_MAX];
	enum BootDeviceIDs boot_device = BOOT_DEVICE_UNKNOWN;

	if (getcwd(cwd, sizeof(cwd)) != NULL)
		boot_device = getBootDeviceID(cwd);
	cacheio_driver_requested =
		boot_device == BOOT_DEVICE_MASS ||
		boot_device == BOOT_DEVICE_MASS0 ||
		boot_device == BOOT_DEVICE_MASS1 ||
		boot_device == BOOT_DEVICE_MX4SIO ||
		boot_device == BOOT_DEVICE_MX4SIO0 ||
		boot_device == BOOT_DEVICE_MX4SIO1;
#endif
	return true;
#endif
}

static bool prepare_IOP()
{
	if (!stage_drivers_before_iop_reset())
		return false;

    reset_IOP();
    SifInitRpc(0);
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    sbv_patch_fileio();
	return true;
}

static bool init_drivers()
{
	init_only_boot_ps2_filesystem_driver();
#if defined(PS2_FAST_CACHE)
	if (cacheio_driver_requested) {
		enum CACHEIO_INIT_STATUS cacheio_status = init_cacheio_driver(false);
		if (cacheio_status == CACHEIO_INIT_STATUS_OK) {
			cacheio_driver_initialized = true;
			ps2_cache_storage_set_available(true);
		} else {
			ps2_cache_storage_set_available(false);
			printf("[cache-io] extent reader unavailable (%d); using POSIX fallback\n",
				(int)cacheio_status);
		}
	} else {
		ps2_cache_storage_set_available(false);
	}
#endif
	if (init_audio_driver() != AUDIO_INIT_STATUS_OK) {
#if defined(PS2_FAST_CACHE)
		if (cacheio_driver_initialized) {
			ps2_cache_storage_set_available(false);
			deinit_cacheio_driver(false);
			cacheio_driver_initialized = false;
		}
#endif
		deinit_only_boot_ps2_filesystem_driver();
		return false;
	}

	/* Keep fileXio's default 16 KiB RW buffer. Increasing it to the 64 KiB
	 * emulation cache block size has proved unreliable on real PS2 hardware,
	 * while the SDK default is sufficient for the streaming ZIP read path. */
	return true;
}

static void deinit_drivers()
{
	deinit_audio_driver();
#if defined(PS2_FAST_CACHE)
	if (cacheio_driver_initialized) {
		ps2_cache_storage_set_available(false);
		deinit_cacheio_driver(false);
		cacheio_driver_initialized = false;
	}
#endif
	deinit_only_boot_ps2_filesystem_driver();
}

static void *ps2_init(void) {
	ps2_platform_t *ps2 = (ps2_platform_t*)calloc(1, sizeof(ps2_platform_t));
	if (ps2 == NULL)
		return NULL;

	if (!prepare_IOP()) {
		free(ps2);
		return NULL;
	}
	if (!init_drivers()) {
#if defined(PS2_EXTERNAL_IRX_IMAGE)
		ps2_drivers_img_discard_staged();
		ps2_drivers_img_forget_source();
#endif
		free(ps2);
		return NULL;
	}

	return ps2;
}

static void ps2_free(void *data) {
	ps2_platform_t *ps2 = (ps2_platform_t*)data;

	deinit_drivers();
#if defined(PS2_EXTERNAL_IRX_IMAGE)
	ps2_drivers_img_discard_staged();
	ps2_drivers_img_forget_source();
#endif

	free(ps2);
}

static void ps2_main(void *data, int argc, char *argv[]) {
	ps2_platform_t *ps2 = (ps2_platform_t*)data;
	char picture_dir[PATH_MAX];
	const char *system_dir;
	int priority_result;

	(void)ps2;
	(void)argc;
	(void)argv;

	/* PS2SDK's InitThread() promotes the startup thread to priority 1. Restore
	 * an application priority so NJEMU's audio and worker threads can preempt
	 * the emulation loop even when neither VSync nor frame limiting blocks it. */
	priority_result = ChangeThreadPriority(GetThreadId(), PS2_MAIN_THREAD_PRIORITY);
	if (priority_result < 0)
		printf("Failed to set PS2 main thread priority: %d\n", priority_result);

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
	ps2_queryMemoryInfo,
	ps2_getSystemLanguage,
};
