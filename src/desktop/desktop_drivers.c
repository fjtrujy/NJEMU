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

void desktop_video_select_backend(int backend)
{
#ifdef HAVE_VIDEO_BACKEND_OPENGL
    if (backend == VIDEO_BACKEND_OPENGL) {
        video_driver = &video_desktop_gl;
        return;
    }
#else
    (void)backend;
#endif
    video_driver = &video_desktop_sdl;
}

int desktop_video_backend_count(void)
{
#ifdef HAVE_VIDEO_BACKEND_OPENGL
    return 2;
#else
    return 1;
#endif
}

const ui_draw_driver_t *const ui_draw_driver = &desktop_ui_draw_driver;
