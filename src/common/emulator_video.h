#ifndef COMMON_EMULATOR_VIDEO_H
#define COMMON_EMULATOR_VIDEO_H

#include <stdint.h>
#include "common/video_driver.h"

/* Per-emulated-system video description consumed by the platform video
 * backend during startup.  Each target defines these once in its core module. */
extern layer_texture_info_t emu_layer_textures[];
extern uint8_t emu_layer_textures_count;
extern clut_info_t emu_clut_info;

#endif /* COMMON_EMULATOR_VIDEO_H */
