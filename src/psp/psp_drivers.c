#include "common/audio_driver.h"
#include "common/cache_storage_driver.h"
#include "common/input_driver.h"
#include "common/platform_driver.h"
#include "common/power_driver.h"
#include "common/thread_driver.h"
#include "common/ticker_driver.h"
#include "common/ui_draw_driver.h"
#include "common/video_driver.h"

extern audio_driver_t audio_psp;
extern input_driver_t input_psp;
extern platform_driver_t platform_psp;
extern const power_driver_t power_psp;
extern thread_driver_t thread_psp;
extern ticker_driver_t ticker_psp;
extern video_driver_t video_psp;
extern const ui_draw_driver_t psp_ui_draw_driver;
extern const ui_draw_driver_t null_ui_draw_driver;

audio_driver_t *const audio_driver = &audio_psp;
const cache_storage_driver_t *const cache_storage_driver = NULL;
input_driver_t *const input_driver = &input_psp;
platform_driver_t *const platform_driver = &platform_psp;
const power_driver_t *const power_driver = &power_psp;
thread_driver_t *const thread_driver = &thread_psp;
ticker_driver_t *const ticker_driver = &ticker_psp;
video_driver_t *video_driver = &video_psp;

int video_backend_choice_count(void) { return 1; }
video_backend_choice_t video_backend_choice_at(int index)
{
    return index == 0 ? (video_backend_choice_t){ 1, "PSP GE" }
                      : (video_backend_choice_t){ -1, "" };
}
int video_backend_option_available(int id) { return id == 0 || id == 1; }
void video_backend_select(int id) { (void)id; video_driver = &video_psp; }

const ui_draw_driver_t *const ui_draw_driver = &psp_ui_draw_driver;
