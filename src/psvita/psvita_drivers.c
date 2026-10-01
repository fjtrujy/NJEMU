#include "common/audio_driver.h"
#include "common/cache_storage_driver.h"
#include "common/input_driver.h"
#include "common/platform_driver.h"
#include "common/power_driver.h"
#include "common/thread_driver.h"
#include "common/ticker_driver.h"
#include "common/ui_draw_driver.h"
#include "common/video_driver.h"

extern audio_driver_t audio_psvita;
extern input_driver_t input_psvita;
extern platform_driver_t platform_psvita;
extern const power_driver_t power_psvita;
extern thread_driver_t thread_psvita;
extern ticker_driver_t ticker_psvita;
extern video_driver_t video_psvita;
extern const ui_draw_driver_t psvita_ui_draw_driver;
extern const ui_draw_driver_t null_ui_draw_driver;

audio_driver_t *const audio_driver = &audio_psvita;
const cache_storage_driver_t *const cache_storage_driver = NULL;
input_driver_t *const input_driver = &input_psvita;
platform_driver_t *const platform_driver = &platform_psvita;
const power_driver_t *const power_driver = &power_psvita;
thread_driver_t *const thread_driver = &thread_psvita;
ticker_driver_t *const ticker_driver = &ticker_psvita;
video_driver_t *video_driver = &video_psvita;

int video_backend_choice_count(void) { return 1; }
video_backend_choice_t video_backend_choice_at(int index)
{
    return index == 0 ? (video_backend_choice_t){ 1, "PS Vita" }
                      : (video_backend_choice_t){ -1, "" };
}
int video_backend_option_available(int id) { return id == 0 || id == 1; }
void video_backend_select(int id) { (void)id; video_driver = &video_psvita; }

const ui_draw_driver_t *const ui_draw_driver = &psvita_ui_draw_driver;
