#ifndef NJEMU_PS2_INPUT_CAPTURE_H
#define NJEMU_PS2_INPUT_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "common/input_state.h"

/* Only game action buttons are latched. Movement and menu/system buttons
 * always represent the latest physical state; a short tap is delivered for
 * exactly one emulated frame. This intentionally cannot count multiple
 * independent taps on the same button within one emulation frame. */
#define PS2_INPUT_CAPTURE_PULSE_MASK \
    (PLATFORM_PAD_B1 | PLATFORM_PAD_B2 | PLATFORM_PAD_B3 | PLATFORM_PAD_B4)

typedef struct ps2_input_capture {
    input_state_t latest;
    uint32_t last_buttons;
    uint32_t pending_presses;
    uint32_t samples;
    uint32_t square_presses;
    uint8_t valid;
} ps2_input_capture_t;

static inline void ps2_input_capture_reset(ps2_input_capture_t *capture)
{
    capture->last_buttons = 0;
    capture->pending_presses = 0;
    capture->valid = 0;
    input_state_reset(&capture->latest);
}

static inline void ps2_input_capture_record(ps2_input_capture_t *capture,
    const input_state_t *sample)
{
    if ((sample->buttons & ~capture->last_buttons & PLATFORM_PAD_B3) != 0)
        capture->square_presses++;
    capture->pending_presses |=
        (sample->buttons & ~capture->last_buttons) & PS2_INPUT_CAPTURE_PULSE_MASK;
    capture->latest = *sample;
    capture->last_buttons = sample->buttons;
    capture->valid = 1;
    capture->samples++;
}

static inline bool ps2_input_capture_consume(ps2_input_capture_t *capture,
    input_state_t *out)
{
    if (!capture->valid)
        return false;
    *out = capture->latest;
    out->buttons |= capture->pending_presses;
    capture->pending_presses = 0;
    return true;
}

#endif /* NJEMU_PS2_INPUT_CAPTURE_H */
