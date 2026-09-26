#include "common/audio_driver.h"
#include "common/input_driver.h"
#include "common/platform_driver.h"
#include "common/power_driver.h"
#include "common/thread_driver.h"
#include "common/ticker_driver.h"
#include "common/ui_draw_driver.h"
#include "common/video_driver.h"

extern audio_driver_t audio_desktop;
extern input_driver_t input_desktop;
extern platform_driver_t platform_desktop;
extern power_driver_t power_desktop;
extern thread_driver_t thread_desktop;
extern ticker_driver_t ticker_desktop;
extern video_driver_t video_desktop;
extern const ui_draw_driver_t desktop_ui_draw_driver;
extern const ui_draw_driver_t null_ui_draw_driver;

audio_driver_t *const audio_driver = &audio_desktop;
input_driver_t *const input_driver = &input_desktop;
platform_driver_t *const platform_driver = &platform_desktop;
power_driver_t *const power_driver = &power_desktop;
thread_driver_t *const thread_driver = &thread_desktop;
ticker_driver_t *const ticker_driver = &ticker_desktop;
video_driver_t *const video_driver = &video_desktop;

#ifdef GUI
const ui_draw_driver_t *const ui_draw_driver = &desktop_ui_draw_driver;
#else
const ui_draw_driver_t *const ui_draw_driver = &null_ui_draw_driver;
#endif
