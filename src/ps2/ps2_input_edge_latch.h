#ifndef NJEMU_PS2_INPUT_EDGE_LATCH_H
#define NJEMU_PS2_INPUT_EDGE_LATCH_H

#include <stdint.h>
#include "common/input_state.h"

/* Game controls can be pressed and released between two emulation frames.
 * Capture transitions without synthesizing Start/Select menu combinations. */
#define PS2_INPUT_EDGE_MASK (PLATFORM_PAD_UP | PLATFORM_PAD_DOWN | \
    PLATFORM_PAD_LEFT | PLATFORM_PAD_RIGHT | PLATFORM_PAD_B1 | \
    PLATFORM_PAD_B2 | PLATFORM_PAD_B3 | PLATFORM_PAD_B4 | \
    PLATFORM_PAD_L | PLATFORM_PAD_R)

typedef struct ps2_input_edge_latch {
    uint32_t previous;
    uint32_t pending;
    uint8_t initialized;
} ps2_input_edge_latch_t;

static inline void ps2_input_edge_latch_reset(ps2_input_edge_latch_t *latch)
{
    latch->previous = 0;
    latch->pending = 0;
    latch->initialized = 0;
}

static inline void ps2_input_edge_latch_record(ps2_input_edge_latch_t *latch,
    uint32_t buttons)
{
    uint32_t level = buttons & PS2_INPUT_EDGE_MASK;
    if (latch->initialized)
        latch->pending |= level & ~latch->previous;
    latch->previous = level;
    latch->initialized = 1;
}

static inline uint32_t ps2_input_edge_latch_consume(
    ps2_input_edge_latch_t *latch, uint32_t buttons, int fresh)
{
    uint32_t combined;
    /* A cached transport fallback is not a new physical level. Recording it
     * after the sampler observed release would invent a second press. */
    if (fresh)
        ps2_input_edge_latch_record(latch, buttons);
    combined = buttons | latch->pending;
    latch->pending = 0;
    return combined;
}

#endif
