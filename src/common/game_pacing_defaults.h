#ifndef COMMON_GAME_PACING_DEFAULTS_H
#define COMMON_GAME_PACING_DEFAULTS_H

#include "common/emulator_options.h"

/* Consoles with fixed display timing default to synchronized presentation
 * without an additional software frame wait. Other platforms retain their
 * existing per-core defaults (CPS2 VSync on; all other cores VSync off).
 * These values apply only when creating/loading a game's default config. */
#if defined(PSP) || defined(PS2)
#define NJEMU_DEFAULT_VSYNC VSYNC_MODE_ON
#define NJEMU_DEFAULT_FRAME_LIMIT 0
#elif defined(BUILD_CPS2)
#define NJEMU_DEFAULT_VSYNC VSYNC_MODE_ON
#define NJEMU_DEFAULT_FRAME_LIMIT 1
#else
#define NJEMU_DEFAULT_VSYNC VSYNC_MODE_OFF
#define NJEMU_DEFAULT_FRAME_LIMIT 1
#endif

#endif /* COMMON_GAME_PACING_DEFAULTS_H */
