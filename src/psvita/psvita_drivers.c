#include "common/audio_driver.h"
#include "common/cache_storage_driver.h"
#include "common/input_driver.h"
#include "common/platform_driver.h"
#include "common/power_driver.h"
#include "common/thread_driver.h"
#include "common/ticker_driver.h"
#include "common/ui_draw_driver.h"
#include "common/video_driver.h"
#include "common/emulator_options.h"

extern audio_driver_t audio_psvita;
extern input_driver_t input_psvita;
extern platform_driver_t platform_psvita;
extern const power_driver_t power_psvita;
extern thread_driver_t thread_psvita;
extern ticker_driver_t ticker_psvita;
extern video_driver_t video_psvita_gxm;
#ifdef HAVE_VIDEO_BACKEND_VITAGL
extern video_driver_t video_psvita_gl;
#endif
extern const ui_draw_driver_t psvita_ui_draw_driver;
extern const ui_draw_driver_t null_ui_draw_driver;

audio_driver_t *const audio_driver = &audio_psvita;
const cache_storage_driver_t *const cache_storage_driver = NULL;
input_driver_t *const input_driver = &input_psvita;
platform_driver_t *const platform_driver = &platform_psvita;
const power_driver_t *const power_driver = &power_psvita;
thread_driver_t *const thread_driver = &thread_psvita;
ticker_driver_t *const ticker_driver = &ticker_psvita;
video_driver_t *video_driver = &video_psvita_gxm;

int video_backend_choice_count(void)
{
#ifdef HAVE_VIDEO_BACKEND_VITAGL
    return 3; /* Auto, native GXM, vitaGL */
#else
    return 1;
#endif
}

video_backend_choice_t video_backend_choice_at(int index)
{
#ifdef HAVE_VIDEO_BACKEND_VITAGL
    static const video_backend_choice_t choices[] = {
        { VIDEO_BACKEND_AUTO, "Auto" },
        { VIDEO_BACKEND_NATIVE, "GXM" },
        { VIDEO_BACKEND_OPENGL, "VitaGL" },
    };
    if (index >= 0 && index < 3)
        return choices[index];
#else
    if (index == 0)
        return (video_backend_choice_t){ VIDEO_BACKEND_NATIVE, "GXM" };
#endif
    return (video_backend_choice_t){ -1, "" };
}

int video_backend_option_available(int id)
{
    if (id == VIDEO_BACKEND_AUTO)
        return 1;
    for (int i = 0; i < video_backend_choice_count(); i++)
        if (video_backend_choice_at(i).id == id)
            return 1;
    return 0;
}

void video_backend_select(int id)
{
#ifdef HAVE_VIDEO_BACKEND_VITAGL
    if (id == VIDEO_BACKEND_OPENGL) {
        video_driver = &video_psvita_gl;
        return;
    }
#else
    (void)id;
#endif
    /* Auto deliberately keeps the native GXM renderer as compatibility default. */
    video_driver = &video_psvita_gxm;
}


const ui_draw_driver_t *const ui_draw_driver = &psvita_ui_draw_driver;
