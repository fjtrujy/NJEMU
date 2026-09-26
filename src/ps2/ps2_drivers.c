#include "common/audio_driver.h"
#include "common/input_driver.h"
#include "common/platform_driver.h"
#include "common/power_driver.h"
#include "common/thread_driver.h"
#include "common/ticker_driver.h"
#include "common/ui_draw_driver.h"
#include "common/video_driver.h"

extern audio_driver_t audio_ps2;
extern input_driver_t input_ps2;
extern platform_driver_t platform_ps2;
extern power_driver_t power_ps2;
extern thread_driver_t thread_ps2;
extern ticker_driver_t ticker_ps2;
extern video_driver_t video_ps2;
extern const ui_draw_driver_t ps2_ui_draw_driver;
extern const ui_draw_driver_t null_ui_draw_driver;

audio_driver_t *const audio_driver = &audio_ps2;
input_driver_t *const input_driver = &input_ps2;
platform_driver_t *const platform_driver = &platform_ps2;
power_driver_t *const power_driver = &power_ps2;
thread_driver_t *const thread_driver = &thread_ps2;
ticker_driver_t *const ticker_driver = &ticker_ps2;
video_driver_t *const video_driver = &video_ps2;

#ifdef GUI
const ui_draw_driver_t *const ui_draw_driver = &ps2_ui_draw_driver;
#else
const ui_draw_driver_t *const ui_draw_driver = &null_ui_draw_driver;
#endif
