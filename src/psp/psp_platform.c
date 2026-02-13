/******************************************************************************

	psp.c

	PSP�ᥤ��

******************************************************************************/

#include <fcntl.h>
#include <limits.h>
#include <pspsdk.h>
#include <pspctrl.h>
#include <pspimpose_driver.h>
#include <pspwlan.h>

#include "SystemButtons.h"
#include "psp.h"


#ifdef KERNEL_MODE
PSP_MODULE_INFO(TARGET_STR, PSP_MODULE_KERNEL, VERSION_MAJOR, VERSION_MINOR);
PSP_MAIN_THREAD_ATTR(0);
#else
PSP_MODULE_INFO(TARGET_STR, PSP_MODULE_USER, VERSION_MAJOR, VERSION_MINOR);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
#endif

typedef struct psp_platform {
	SceUID modID;
	int32_t devkit_version;
#if SYSTEM_BUTTONS
	char prx_path[PATH_MAX];
#endif
} psp_platform_t;


/******************************************************************************
	�����`�Х��v��
******************************************************************************/

/******************************************************************************
	���`�����v��
******************************************************************************/

/*--------------------------------------------------------
	Power Callback
--------------------------------------------------------*/

static SceKernelCallbackFunction PowerCallback(int unknown, int pwrflags, void *arg)
{
	int cbid;

	if (pwrflags & PSP_POWER_CB_POWER_SWITCH)
	{
#if defined(LARGE_MEMORY) && ((EMU_SYSTEM == CPS2) || (EMU_SYSTEM == MVS))
		extern int32_t psp2k_mem_left;

		if (psp2k_mem_left < 0x400000)
		{
			char path[PATH_MAX];
			SceUID fd;

			sprintf(path, "%sresume.bin", launchDir);

			if ((fd = open(path, O_WRONLY|O_CREAT, 0777)) >= 0)
			{
				write(fd, (void *)(PSP2K_MEM_TOP + 0x1c00000), 0x400000);
				close(fd);
			}
		}
#endif
		Sleep = 1;
	}
	else if (pwrflags & PSP_POWER_CB_RESUME_COMPLETE)
	{
#if defined(LARGE_MEMORY) && ((EMU_SYSTEM == CPS2) || (EMU_SYSTEM == MVS))
		extern int32_t psp2k_mem_left;

		if (psp2k_mem_left < 0x400000)
		{
			char path[PATH_MAX];
			SceUID fd;

			sprintf(path, "%sresume.bin", launchDir);

			if ((fd = open(path, O_RDONLY, 0777)) >= 0)
			{
				read(fd, (void *)(PSP2K_MEM_TOP + 0x1c00000), 0x400000);
				close(fd);
			}
			remove(path);
		}
#endif
		Sleep = 0;
	}

	cbid = sceKernelCreateCallback("Power Callback", (void *)PowerCallback, NULL);

	scePowerRegisterCallback(0, cbid);

	return 0;
}

/*--------------------------------------------------------
	���`��Хå�����å�����
--------------------------------------------------------*/

static int CallbackThread(SceSize args, void *argp)
{
	int cbid;

	cbid = sceKernelCreateCallback("Power Callback", (void *)PowerCallback, NULL);
	scePowerRegisterCallback(0, cbid);

	sceKernelSleepThreadCB();

	return 0;
}


/*--------------------------------------------------------
	���`��Хå�����å��O��
--------------------------------------------------------*/

static int SetupCallbacks(void)
{
	SceUID thread_id = 0;

	thread_id = sceKernelCreateThread("Update Thread", CallbackThread, 0x12, 0xFA0, 0, NULL);
	if (thread_id >= 0)
	{
		sceKernelStartThread(thread_id, 0, 0);
	}

	Loop  = LOOP_EXIT;
	Sleep = 0;

	return thread_id;
}


/*--------------------------------------------------------
	Kernel��`�� main()
--------------------------------------------------------*/

// #ifdef KERNEL_MODE
// int main(int argc, char *argv[])
// {
// 	SceUID main_thread;

// 	pspSdkInstallNoPlainModuleCheckPatch();
// 	pspSdkInstallKernelLoadModulePatch();

// #ifdef ADHOC
// 	pspSdkLoadAdhocModules();
// #endif

// 	main_thread = sceKernelCreateThread(
// 						"User Mode Thread",
// 						user_main,
// 						0x11,
// 						256 * 1024,
// 						PSP_THREAD_ATTR_USER,
// 						NULL
// 					);

// 	sceKernelStartThread(main_thread, 0, 0);
// 	sceKernelWaitThreadEnd(main_thread, NULL);

// 	sceKernelExitGame();

// 	return 0;
// }
// #endif

static void *psp_init(void) {
	psp_platform_t *psp = (psp_platform_t*)calloc(1, sizeof(psp_platform_t));
	return psp;
}

static void psp_free(void *data) {
	psp_platform_t *psp = (psp_platform_t*)data;

#ifdef KERNEL_MODE
	sceKernelExitThread(0);
#else
	sceKernelExitGame();
#endif

	free(psp);
}

static void psp_main(void *data, int argc, char *argv[]) {
	psp_platform_t *psp = (psp_platform_t*)data;

	// Override launchDir from argv[0] for PPSSPP compatibility.
	// getcwd() may return "umd0:" which is not browsable, but argv[0]
	// contains the full path to EBOOT.PBP on the memory stick.
	if (argc > 0 && argv[0]) {
		char *last_slash = strrchr(argv[0], '/');
		if (last_slash) {
			size_t len = last_slash - argv[0] + 1;
			strncpy(launchDir, argv[0], len);
			launchDir[len] = '\0';
		}
	}

#if	(EMU_SYSTEM == CPS1)
	strcat(screenshotDir, "ms0:/PICTURE/CPS1");
#endif
#if	(EMU_SYSTEM == CPS2)
	strcat(screenshotDir, "ms0:/PICTURE/CPS2");
#endif
#if	(EMU_SYSTEM == MVS)
	strcat(screenshotDir, "ms0:/PICTURE/MVS");
#endif
#if	(EMU_SYSTEM == NCDZ)
	strcat(screenshotDir, "ms0:/PICTURE/NCDZ");
#endif

	psp->devkit_version = sceKernelDevkitVersion();

	SetupCallbacks();
}

static bool psp_startSystemButtons(void *data) {
	psp_platform_t *psp = (psp_platform_t*)data;
#if SYSTEM_BUTTONS
	sprintf(psp->prx_path, "%sSystemButtons.prx", launchDir);

	if ((psp->modID = pspSdkLoadStartModule(psp->prx_path, PSP_MEMORY_PARTITION_KERNEL)) >= 0)
	{
		initSystemButtons(devkit_version);
		return true;
	}
	else
#endif
	{
		return false;
	}
}

static int32_t psp_getDevkitVersion(void *data) {
	psp_platform_t *psp = (psp_platform_t*)data;
	return psp->devkit_version;
}

static bool psp_getWlanSwitchState(void *data) {
	return sceWlanGetSwitchState() != 0;
}

static int psp_getHardwareModel(void *data) {
#ifdef LARGE_MEMORY
	return kuKernelGetModel();
#else
	return 0;
#endif
}

platform_driver_t platform_psp = {
	"psp",
	psp_init,
	psp_free,
	psp_main,
	psp_startSystemButtons,
	psp_getDevkitVersion,
	psp_getWlanSwitchState,
	psp_getHardwareModel,
};
