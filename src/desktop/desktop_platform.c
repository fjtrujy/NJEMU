#include "emumain.h"
#include <SDL.h>
#include <string.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#define DESKTOP_RAM_CAP (256u * 1024u * 1024u)

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

static bool desktop_getWlanSwitchState(void *data) {
	return false;
}

static int desktop_getHardwareModel(void *data) {
	return 0;
}

static bool desktop_queryMemoryInfo(void *data, platform_memory_info_t *out) {
	uint64_t total = 0;
	uint64_t available = 0;
	(void)data;

	if (out == NULL) {
		return false;
	}
	memset(out, 0, sizeof(*out));

#if defined(__APPLE__)
	int mib[2] = { CTL_HW, HW_MEMSIZE };
	size_t len = sizeof(total);
	if (sysctl(mib, 2, &total, &len, NULL, 0) != 0) {
		total = 0;
	}
	{
		mach_port_t host = mach_host_self();
		vm_size_t page_size = 0;
		vm_statistics64_data_t stats;
		mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
		if (host_page_size(host, &page_size) == KERN_SUCCESS &&
			host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&stats, &count) == KERN_SUCCESS) {
			available = ((uint64_t)stats.free_count + (uint64_t)stats.inactive_count) * (uint64_t)page_size;
		}
		mach_port_deallocate(mach_task_self(), host);
	}
#elif defined(__linux__)
	long pages = sysconf(_SC_PHYS_PAGES);
	long available_pages = sysconf(_SC_AVPHYS_PAGES);
	long page_size = sysconf(_SC_PAGESIZE);
	if (pages > 0 && page_size > 0) {
		total = (uint64_t)pages * (uint64_t)page_size;
	}
	if (available_pages > 0 && page_size > 0) {
		available = (uint64_t)available_pages * (uint64_t)page_size;
	}
#endif

	if (total == 0) {
		total = DESKTOP_RAM_CAP;
	}
	if (available == 0) {
		available = total;
		out->reliability_flags |= PLATFORM_MEMORY_FREE_IS_ESTIMATE;
	}

	out->physical_total_bytes = total;
	out->budget_cap_bytes = DESKTOP_RAM_CAP;
	out->free_bytes = available;
	out->largest_free_block_bytes = available < DESKTOP_RAM_CAP ? available : DESKTOP_RAM_CAP;
	out->capabilities = PLATFORM_MEMORY_CAP_QUERY_FREE | PLATFORM_MEMORY_CAP_QUERY_LARGEST_BLOCK;
	out->reliability_flags |= PLATFORM_MEMORY_LARGEST_IS_ESTIMATE;
	platform_memory_info_normalize(out);
	return true;
}

static ui_language_t desktop_getSystemLanguage(void *data) {
	(void)data;
	/* Desktop historically always selected English. Keep that behaviour explicit. */
	return UI_LANG_ENGLISH;
}

platform_driver_t platform_desktop = {
	"desktop",
	desktop_init,
	desktop_free,
	desktop_main,
	desktop_startSystemButtons,
	desktop_getDevkitVersion,
	desktop_getWlanSwitchState,
	desktop_getHardwareModel,
	desktop_queryMemoryInfo,
	desktop_getSystemLanguage,
};
