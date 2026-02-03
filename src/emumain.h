/******************************************************************************

	emumain.h

	Emulation Core

******************************************************************************/

#ifndef EMUMAIN_H
#define EMUMAIN_H

#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include "emucfg.h"
#include "main_ui_draw.h"

enum
{
	LOOP_EXIT = 0,
	LOOP_BROWSER,
	LOOP_RESTART,
	LOOP_RESET,
	LOOP_EXEC
};

enum
{
	UI_PAL_TITLE = 0,
	UI_PAL_SELECT,
	UI_PAL_NORMAL,
	UI_PAL_INFO,
	UI_PAL_WARNING,
	UI_PAL_BG1,
	UI_PAL_BG2,
	UI_PAL_FRAME,
	UI_PAL_FILESEL1,
	UI_PAL_FILESEL2,
	UI_PAL_MAX
};

enum
{
	WP_LOGO = 0,
	WP_FILER,
	WP_GAMECFG,
	WP_KEYCFG,
	WP_STATE,
	WP_COLORCFG,
	WP_DIPSW,
	WP_CMDLIST,
	NUM_WALLPAPERS
};

enum
{
	ICON_CONFIG = 0,
	ICON_KEYCONFIG,
	ICON_FOLDER,
	ICON_SYSTEM,
	ICON_RETURN,
	ICON_EXIT,
	ICON_DIPSWITCH,
	ICON_CMDLIST,
	ICON_UPPERDIR,
	ICON_MEMSTICK,
	ICON_ZIPFILE,
	ICON_BATTERY1,
	ICON_BATTERY2,
	ICON_BATTERY3,
	ICON_BATTERY4,
	ICON_COMMANDDAT,
	ICON_COLORCFG,
	MAX_ICONS
};

enum
{
	BG_DEFAULT = 0,
	BG_USER,
	BG_LOGOONLY,
	BG_DISABLE,
	BG_MAX
};

#define FONTSIZE			14

#define FONT_UPARROW		"\x10"
#define FONT_DOWNARROW		"\x11"
#define FONT_LEFTARROW		"\x12"
#define FONT_RIGHTARROW		"\x13"
#define FONT_CIRCLE			"\x14"
#define FONT_CROSS			"\x15"
#define FONT_SQUARE			"\x16"
#define FONT_TRIANGLE		"\x17"
#define FONT_LTRIGGER		"\x18"
#define FONT_RTRIGGER		"\x19"
#define FONT_UPTRIANGLE		"\x1b"
#define FONT_DOWNTRIANGLE	"\x1c"
#define FONT_LEFTTRIANGLE	"\x1d"
#define FONT_RIGHTTRIANGLE	"\x1e"

#define MAX_CHEATS 150
#define MAX_CHEAT_OPTION 140
#define MAX_CHEAT_VALUE 10

typedef struct {
	//int cpu; //mem save
	int address;
	int value;
}cheat_value_t;


typedef struct {
	char *label;
	int num_cheat_values;
	cheat_value_t *cheat_value[MAX_CHEAT_VALUE];
}cheat_option_t;

typedef struct {
	//int type; //mem save
	int curr_option;
	char *cheat_name;
	short int num_cheat_options;
	cheat_option_t *cheat_option[MAX_CHEAT_OPTION];
}gamecheat_t;

enum
{
	HELP_FILEBROWSER = 0,
	HELP_MAINMENU,
#if (EMU_SYSTEM == MVS)
	HELP_SELECTBIOS,
#endif
	HELP_GAMECONFIG,
	HELP_KEYCONFIG,
#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == MVS)
	HELP_DIPSWITCH,
#endif
#ifdef SAVE_STATE
	HELP_STATE,
#endif
#ifdef COMMAND_LIST
	HELP_COMMANDLIST,
#endif
	HELP_CHEATCONFIG,
	HELP_NUM_MAX
};

typedef struct ui_palette_t
{
	int r;
	int g;
	int b;
} UI_PALETTE;

extern UI_PALETTE ui_palette[UI_PAL_MAX];
#define UI_COLOR(no)	ui_palette[no].r,ui_palette[no].g,ui_palette[no].b

#if defined(PSP)
#include "psp/psp.h"
#endif
#if defined(PS2)
#include "ps2/ps2.h"
#endif
#if defined(DESKTOP)
#include "desktop/desktop.h"
#endif

#include "include/osd_cpu.h"
#include "include/cpuintrf.h"
#include "include/memory.h"
#include "zip/zfile.h"
#include "common/filer.h"
#include "common/loadrom.h"
#include "common/state.h"
#include "common/sound.h"
#include "common/power_driver.h"
#include "common/ticker_driver.h"
#include "common/platform_driver.h"
#include "common/video_driver.h"
#include "common/ui_text_driver.h"
#include "common/input_driver.h"
#ifdef ADHOC
#include "common/adhoc.h"
#endif
#if USE_CACHE
#include "common/cache.h"
#endif
#ifdef COMMAND_LIST
#include "common/cmdlist.h"
#endif
#ifdef CHEAT
#include "common/cheat.h"
#endif
#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
#include "common/coin.h"
#endif
#if (EMU_SYSTEM == NCDZ)
#include "common/mp3.h"
#endif

#if (EMU_SYSTEM == CPS1)
#include "cps1/cps1.h"
#elif (EMU_SYSTEM == CPS2)
#include "cps2/cps2.h"
#elif (EMU_SYSTEM == MVS)
#include "mvs/mvs.h"
#elif (EMU_SYSTEM == NCDZ)
#include "ncdz/ncdz.h"
#endif

extern char launchDir[PATH_MAX];
extern char screenshotDir[PATH_MAX];
extern bool systembuttons_available;

extern char game_name[16];
extern char parent_name[16];
extern char game_dir[PATH_MAX];

#if USE_CACHE
extern char cache_parent_name[16];
extern char cache_dir[PATH_MAX];
#endif

extern layer_texture_info_t emu_layer_textures[];
extern uint8_t emu_layer_textures_count;
extern clut_info_t emu_clut_info;

extern int option_showfps;
extern int option_autoframeskip;
extern int option_frameskip;
extern int option_speedlimit;
extern int option_vsync;
extern int option_stretch;

extern int option_sound_enable;
extern int option_samplerate;
extern int option_sound_volume;

extern int machine_driver_type;
extern int machine_input_type;
extern int machine_init_type;
extern int machine_screen_type;
extern int machine_sound_type;

extern uint32_t frames_displayed;
extern int fatal_error;

extern volatile int Loop;
extern volatile int Sleep;

void emu_main(void);

void autoframeskip_reset(void);

uint8_t skip_this_frame(void);
void update_screen(void);

void fatalerror(const char *text, ...);
void show_fatal_error(void);

void save_snapshot(void);

void waitVBlank(void);

#endif /* EMUMAIN_H */
