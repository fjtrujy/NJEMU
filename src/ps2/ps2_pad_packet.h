#ifndef NJEMU_PS2_PAD_PACKET_H
#define NJEMU_PS2_PAD_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A zero active-low button word means every button is pressed.
 * Mode zero is not a valid configured pad response. Do not promote
 * incomplete or uninitialized PADMAN packets into controller events. */
static inline bool ps2_pad_packet_valid(int read_length, size_t button_end,
    uint8_t mode, uint16_t active_low_buttons)
{
    return read_length >= (int)button_end &&
        mode != 0 && active_low_buttons != 0;
}

#endif /* NJEMU_PS2_PAD_PACKET_H */
