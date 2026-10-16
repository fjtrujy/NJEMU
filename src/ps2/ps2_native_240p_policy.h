#ifndef NJEMU_PS2_NATIVE_240P_POLICY_H
#define NJEMU_PS2_NATIVE_240P_POLICY_H

/* Scan out the CPS source as native GS pixels instead of stretching it
 * to the generic 704-pixel NTSC framebuffer. The GS magnifies pixels for
 * video output; the source-to-framebuffer copy stays exactly 1:1. */
#define PS2_CPS_NATIVE_WIDTH 384
#define PS2_CPS_NATIVE_HEIGHT 224

static inline int ps2_use_native_cps_240p(int is_cps,
    int output_is_240p, int display_is_native)
{
    return is_cps && output_is_240p && display_is_native;
}

#endif
