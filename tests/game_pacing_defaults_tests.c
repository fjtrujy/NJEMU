#include <assert.h>

#include "common/game_pacing_defaults.h"

int main(void)
{
#if defined(PSP) || defined(PS2)
    /* Both 60 Hz console backends default to synchronized flips without a
     * second software throttle, regardless of the emulated arcade system. */
    assert(NJEMU_DEFAULT_VSYNC == VSYNC_MODE_ON);
    assert(NJEMU_DEFAULT_FRAME_LIMIT == 0);
#elif defined(BUILD_CPS2)
    /* Desktop/Vita keep the CPS2-specific VSync-on default. */
    assert(NJEMU_DEFAULT_VSYNC == VSYNC_MODE_ON);
    assert(NJEMU_DEFAULT_FRAME_LIMIT == 1);
#else
    /* Other Desktop/Vita cores retain their historical defaults. */
    assert(NJEMU_DEFAULT_VSYNC == VSYNC_MODE_OFF);
    assert(NJEMU_DEFAULT_FRAME_LIMIT == 1);
#endif
    return 0;
}
