#include <assert.h>
#include <stddef.h>

#include "common/menu/section_rules.h"

static void check_category(const int *labels, size_t count, int category)
{
    for (size_t i = 0; i < count; ++i)
        assert(menu_setting_group_for_label(labels[i]) == category);
}

int main(void)
{
    static const int video[] = {
        DISPLAY_MODE, SCREEN_ORIENTATION, VIDEO_SYNC, FRAME_LIMIT,
        AUTO_FRAMESKIP, FRAMESKIP, SHOW_FPS
    };
    static const int audio[] = {
        ENABLE_SOUND, SAMPLE_RATE, SOUND_VOLUME, ENABLE_CDDA, CDDA_VOLUME
    };
    static const int emulation[] = {
        MACHINE_REGION, MACHINE_MODE, RASTER_EFFECTS,
        EMULATE_LOAD_SCREEN, CDROM_SPEED_LIMIT
    };
    static const int performance[] = { CPU_CLOCK };

    check_category(video, sizeof(video) / sizeof(video[0]), MENU_CFG_VIDEO);
    check_category(audio, sizeof(audio) / sizeof(audio[0]), MENU_CFG_AUDIO);
    check_category(emulation, sizeof(emulation) / sizeof(emulation[0]), MENU_CFG_EMULATION);
    check_category(performance, sizeof(performance) / sizeof(performance[0]),
                   MENU_CFG_PERFORMANCE);

    /* Controller rows live in the dedicated Controls editor, not another category. */
    assert(menu_setting_group_for_label(CONTROLLER) == -1);
    assert(menu_setting_group_for_label(RETURN_TO_MAIN_MENU) == -1);

    assert(menu_setting_order(MENU_CFG_VIDEO, DISPLAY_MODE) <
           menu_setting_order(MENU_CFG_VIDEO, VIDEO_BACKEND));
    assert(menu_setting_order(MENU_CFG_VIDEO, VIDEO_BACKEND) <
           menu_setting_order(MENU_CFG_VIDEO, VIDEO_SYNC));
    assert(menu_setting_order(MENU_CFG_VIDEO, FRAME_LIMIT) <
           menu_setting_order(MENU_CFG_VIDEO, AUTO_FRAMESKIP));
    assert(menu_setting_order(MENU_CFG_VIDEO, AUTO_FRAMESKIP) <
           menu_setting_order(MENU_CFG_VIDEO, FRAMESKIP));
    assert(menu_setting_order(MENU_CFG_VIDEO, FRAMESKIP) <
           menu_setting_order(MENU_CFG_VIDEO, FPS_X_OFFSET));
    assert(menu_setting_order(MENU_CFG_AUDIO, ENABLE_SOUND) <
           menu_setting_order(MENU_CFG_AUDIO, AUDIO_PROCESSOR));
    assert(menu_setting_order(MENU_CFG_AUDIO, AUDIO_PROCESSOR) <
           menu_setting_order(MENU_CFG_AUDIO, ENABLE_CDDA));

    assert(!menu_has_performance_settings(0, 0));
    assert(menu_has_performance_settings(0, 1));
    assert(menu_has_performance_settings(1, 0));
    assert(menu_has_performance_settings(1, 1));
    return 0;
}
