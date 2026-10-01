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
#include "desktop/desktop_video.h"

extern audio_driver_t audio_desktop;
extern input_driver_t input_desktop;
extern platform_driver_t platform_desktop;
extern thread_driver_t thread_desktop;
extern ticker_driver_t ticker_desktop;
extern video_driver_t video_desktop_sdl;
#ifdef HAVE_VIDEO_BACKEND_OPENGL
extern video_driver_t video_desktop_gl;
#endif
extern const ui_draw_driver_t desktop_ui_draw_driver;
extern const ui_draw_driver_t null_ui_draw_driver;

audio_driver_t *const audio_driver = &audio_desktop;
const cache_storage_driver_t *const cache_storage_driver = NULL;
input_driver_t *const input_driver = &input_desktop;
platform_driver_t *const platform_driver = &platform_desktop;
const power_driver_t *const power_driver = &power_unsupported;
thread_driver_t *const thread_driver = &thread_desktop;
ticker_driver_t *const ticker_driver = &ticker_desktop;
video_driver_t *video_driver = &video_desktop_sdl;

int video_backend_choice_count(void)
{
#ifdef HAVE_VIDEO_BACKEND_OPENGL
    return 3; /* Auto, SDL, OpenGL */
#else
    return 1;
#endif
}

video_backend_choice_t video_backend_choice_at(int index)
{
#ifdef HAVE_VIDEO_BACKEND_OPENGL
    static const video_backend_choice_t choices[] = {
        { VIDEO_BACKEND_AUTO, "Auto" },
        { VIDEO_BACKEND_NATIVE, "SDL" },
        { VIDEO_BACKEND_OPENGL, "OpenGL" },
    };
    if (index >= 0 && index < 3)
        return choices[index];
#else
    if (index == 0)
        return (video_backend_choice_t){ VIDEO_BACKEND_NATIVE, "SDL" };
#endif
    return (video_backend_choice_t){ -1, "" };
}

int video_backend_option_available(int id)
{
    for (int i = 0; i < video_backend_choice_count(); i++)
        if (video_backend_choice_at(i).id == id)
            return 1;
    return 0;
}

void video_backend_select(int backend)
{
#ifdef HAVE_VIDEO_BACKEND_OPENGL
    /* Auto deliberately keeps SDL as the conservative compatibility backend. */
    if (backend == VIDEO_BACKEND_OPENGL) {
        video_driver = &video_desktop_gl;
        return;
    }
#else
    (void)backend;
#endif
    video_driver = &video_desktop_sdl;
}


const ui_draw_driver_t *const ui_draw_driver = &desktop_ui_draw_driver;
