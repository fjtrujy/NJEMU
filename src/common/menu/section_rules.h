#ifndef NJEMU_MENU_SECTION_RULES_H
#define NJEMU_MENU_SECTION_RULES_H

#include "common/ui_text_ids.h"

enum menu_gamecfg_section {
    MENU_CFG_VIDEO,
    MENU_CFG_AUDIO,
    MENU_CFG_EMULATION,
    MENU_CFG_PERFORMANCE
};

static inline int menu_setting_group_for_label(int label)
{
    switch (label) {
    case DISPLAY_MODE:
    case SCREEN_ORIENTATION:
    case WIDESCREEN:
    case VIDEO_SYNC:
    case AUTO_FRAMESKIP:
    case FRAMESKIP:
    case FRAME_LIMIT:
    case SHOW_FPS:
        return MENU_CFG_VIDEO;
    case ENABLE_SOUND:
    case SAMPLE_RATE:
    case SOUND_VOLUME:
    case ENABLE_CDDA:
    case CDDA_VOLUME:
        return MENU_CFG_AUDIO;
    case MACHINE_REGION:
    case MACHINE_MODE:
    case RASTER_EFFECTS:
    case EMULATE_LOAD_SCREEN:
    case CDROM_SPEED_LIMIT:
        return MENU_CFG_EMULATION;
    case CPU_CLOCK:
        return MENU_CFG_PERFORMANCE;
    default:
        return -1;
    }
}

static inline int menu_setting_order(int section, int label)
{
    if (section == MENU_CFG_VIDEO) {
        switch (label) {
        case DISPLAY_MODE: return 0;
        case SCREEN_ORIENTATION: return 1;
        case WIDESCREEN: return 1;  /* MVS only; orientation is CPS only */
        case VIDEO_BACKEND: return 2;
        case VIDEO_OUTPUT_MODE: return 3;
        case VIDEO_X_OFFSET: return 4;
        case VIDEO_Y_OFFSET: return 5;
        case VIDEO_FILTER: return 6;
        case VIDEO_SYNC: return 10;
        case FRAME_LIMIT: return 11;
        case AUTO_FRAMESKIP: return 12;
        case FRAMESKIP: return 13;
        case SHOW_FPS: return 20;
        case FPS_X_OFFSET: return 21;
        case FPS_Y_OFFSET: return 22;
        }
    } else if (section == MENU_CFG_AUDIO) {
        switch (label) {
        case ENABLE_SOUND: return 0;
        case SAMPLE_RATE: return 1;
        case SOUND_VOLUME: return 2;
        case AUDIO_PROCESSOR: return 3;
        case ENABLE_CDDA: return 4;
        case CDDA_VOLUME: return 5;
        }
    }
    return 100;
}

static inline int menu_has_performance_settings(int supports_cache, int supports_performance)
{
    return supports_cache || supports_performance;
}

#endif /* NJEMU_MENU_SECTION_RULES_H */
