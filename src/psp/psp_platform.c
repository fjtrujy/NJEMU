/******************************************************************************

	psp.c

	PSPメイン

******************************************************************************/

#include <fcntl.h>
#include <pspsdk.h>
#include <pspctrl.h>
#include <pspimpose_driver.h>

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
	char prx_path[MAX_PATH];
#endif
} psp_platform_t;


/******************************************************************************
	グローバル関数
******************************************************************************/

/******************************************************************************
	ローカル関数
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
			char path[MAX_PATH];
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
			char path[MAX_PATH];
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
	コールバックスレッド作成
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
	コールバックスレッド設定
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
	Kernelモード main()
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

platform_driver_t platform_psp = {
	"psp",
	psp_init,
	psp_free,
	psp_main,
	psp_startSystemButtons,
	psp_getDevkitVersion,
};
