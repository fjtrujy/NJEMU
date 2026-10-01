#include "common/audio_driver.h"
#include "common/cache_storage_driver.h"
#include "common/input_driver.h"
#include "common/platform_driver.h"
#include "common/power_driver.h"
#include "common/thread_driver.h"
#include "common/ticker_driver.h"
#include "common/ui_draw_driver.h"
#include "common/video_driver.h"

extern audio_driver_t audio_ps2;
extern const cache_storage_driver_t cache_storage_ps2;
extern input_driver_t input_ps2;
extern platform_driver_t platform_ps2;
extern thread_driver_t thread_ps2;
extern ticker_driver_t ticker_ps2;
extern video_driver_t video_ps2;
extern const ui_draw_driver_t ps2_ui_draw_driver;
extern const ui_draw_driver_t null_ui_draw_driver;

audio_driver_t *const audio_driver = &audio_ps2;
const cache_storage_driver_t *const cache_storage_driver = &cache_storage_ps2;
input_driver_t *const input_driver = &input_ps2;
platform_driver_t *const platform_driver = &platform_ps2;
const power_driver_t *const power_driver = &power_unsupported;
thread_driver_t *const thread_driver = &thread_ps2;
ticker_driver_t *const ticker_driver = &ticker_ps2;
video_driver_t *video_driver = &video_ps2;

int video_backend_choice_count(void) { return 1; }
video_backend_choice_t video_backend_choice_at(int index)
{
    return index == 0 ? (video_backend_choice_t){ 1, "PS2 GS" }
                      : (video_backend_choice_t){ -1, "" };
}
int video_backend_option_available(int id) { return id == 0 || id == 1; }
void video_backend_select(int id) { (void)id; video_driver = &video_ps2; }

const ui_draw_driver_t *const ui_draw_driver = &ps2_ui_draw_driver;
