#include "common/audio_driver.h"
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
input_driver_t *const input_driver = &input_psp;
platform_driver_t *const platform_driver = &platform_psp;
const power_driver_t *const power_driver = &power_psp;
thread_driver_t *const thread_driver = &thread_psp;
ticker_driver_t *const ticker_driver = &ticker_psp;
video_driver_t *const video_driver = &video_psp;

#ifdef GUI
const ui_draw_driver_t *const ui_draw_driver = &psp_ui_draw_driver;
#else
const ui_draw_driver_t *const ui_draw_driver = &null_ui_draw_driver;
#endif
