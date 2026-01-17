/******************************************************************************

	psp.c

	PSP�ᥤ��

******************************************************************************/

#ifndef PSP_MAIN_H
#define PSP_MAIN_H

#include "emucfg.h"

#include <psptypes.h>
#include "include/osd_cpu.h"
#include <pspaudio.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>
#include <pspsdk.h>
#ifdef LARGE_MEMORY
#include <kubridge.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <sys/unistd.h>

#include "common/ticker_driver.h"
#include "common/input_driver.h"
#include "common/power_driver.h"
#include "common/video_driver.h"
#include "common/ui_text_driver.h"
#include "common/platform_driver.h"
#include "emumain.h"

#include "psp/config.h"
#include "psp/filer.h"
#include "psp/ui.h"
#include "psp/ui_draw.h"
#include "psp/ui_menu.h"
#include "psp/psp_video.h"
#include "psp/png.h"
#include "psp/psp_power.h"
#ifdef ADHOC
#include "psp/adhoc.h"
#endif
#include "SystemButtons.h"

#ifdef LARGE_MEMORY
#define PSP2K_MEM_TOP		0xa000000//0xa000000
#define PSP2K_MEM_BOTTOM	0xbffffff//0xbffffff
#define PSP2K_MEM_SIZE		0x2000000//0x2000000
#endif

/******************************************************************************
	PSPの定数
******************************************************************************/

#define REFRESH_RATE		(59.940059)		// (9000000Hz * 1) / (525 * 286)

#endif /* PSP_MAIN_H */
