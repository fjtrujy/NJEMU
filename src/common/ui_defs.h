#ifndef COMMON_UI_DEFS_H
#define COMMON_UI_DEFS_H

#include "emucfg.h"

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

#define FONTSIZE 14

#define FONT_UPARROW      "\x10"
#define FONT_DOWNARROW    "\x11"
#define FONT_LEFTARROW    "\x12"
#define FONT_RIGHTARROW   "\x13"
#define FONT_CIRCLE       "\x14"
#define FONT_CROSS        "\x15"
#define FONT_SQUARE       "\x16"
#define FONT_TRIANGLE     "\x17"
#define FONT_LTRIGGER     "\x18"
#define FONT_RTRIGGER     "\x19"
#define FONT_UPTRIANGLE   "\x1b"
#define FONT_DOWNTRIANGLE "\x1c"
#define FONT_LEFTTRIANGLE "\x1d"
#define FONT_RIGHTTRIANGLE "\x1e"

#define MAX_CHEATS 150
#define MAX_CHEAT_OPTION 140
#define MAX_CHEAT_VALUE 10

typedef struct {
    int address;
    int value;
} cheat_value_t;

typedef struct {
    char *label;
    int num_cheat_values;
    cheat_value_t *cheat_value[MAX_CHEAT_VALUE];
} cheat_option_t;

typedef struct {
    int curr_option;
    char *cheat_name;
    short int num_cheat_options;
    cheat_option_t *cheat_option[MAX_CHEAT_OPTION];
} gamecheat_t;

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
#define UI_COLOR(no) ui_palette[no].r, ui_palette[no].g, ui_palette[no].b

#endif /* COMMON_UI_DEFS_H */
