#include "common/audio_driver.h"
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
extern const ui_draw_driver_t null_ui_draw_driver;

audio_driver_t *const audio_driver = &audio_psvita;
input_driver_t *const input_driver = &input_psvita;
platform_driver_t *const platform_driver = &platform_psvita;
const power_driver_t *const power_driver = &power_psvita;
thread_driver_t *const thread_driver = &thread_psvita;
ticker_driver_t *const ticker_driver = &ticker_psvita;
video_driver_t *const video_driver = &video_psvita;

/* The Vita backend has no UI texture adapter yet: GUI builds are rejected by CMake. */
const ui_draw_driver_t *const ui_draw_driver = &null_ui_draw_driver;
