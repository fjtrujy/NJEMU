#ifndef NJEMU_TEST_SF2_PORT_PROJECTION_H
#define NJEMU_TEST_SF2_PORT_PROJECTION_H

#include <stdint.h>
#include "common/input_state.h"

/* Diagnostic projection of CPS1 sf2 P1's active-low port values.
 * Default mapping comes from config/sf2.ini:
 *   X -> punch1, L1 -> punch2, Square -> punch3,
 *   Circle -> kick1, R1 -> kick2, Triangle -> kick3.
 * Compare with update_inputport1()/update_inputport2() in inptport.c.
 * The packed value retains port1 bits [7:0] and port2 bits [10:8,2:0].
 * This intentionally does not run the full game/emulator input path. */
static inline uint32_t sf2_probe_ports(uint32_t buttons)
{
    uint16_t p1 = 0xffffu;
    uint16_t p2 = 0xffffu;
    if (buttons & PLATFORM_PAD_RIGHT) p1 &= ~0x0001u;
    if (buttons & PLATFORM_PAD_LEFT) p1 &= ~0x0002u;
    if (buttons & PLATFORM_PAD_DOWN) p1 &= ~0x0004u;
    if (buttons & PLATFORM_PAD_UP) p1 &= ~0x0008u;
    if (buttons & PLATFORM_PAD_B2) p1 &= ~0x0010u;
    if (buttons & PLATFORM_PAD_L) p1 &= ~0x0020u;
    if (buttons & PLATFORM_PAD_B3) p1 &= ~0x0040u;
    if (buttons & PLATFORM_PAD_B1) p2 &= ~0x0101u;
    if (buttons & PLATFORM_PAD_R) p2 &= ~0x0202u;
    if (buttons & PLATFORM_PAD_B4) p2 &= ~0x0404u;
    return (p1 & 0x00ffu) | ((p2 & 0x0707u) << 8);
}

#endif
