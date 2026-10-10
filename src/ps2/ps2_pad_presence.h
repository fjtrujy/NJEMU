#ifndef NJEMU_PS2_PAD_PRESENCE_H
#define NJEMU_PS2_PAD_PRESENCE_H

#include <stdbool.h>
#include <stdint.h>

/* PADMAN can report a command-in-progress between two good samples.
 * Keep that already-established controller in its logical slot until
 * PADMAN reports a real disconnect/error. Never admit a pad whose first
 * observed state is only an in-progress command. */
static inline bool ps2_pad_presence_update(uint8_t *was_ready,
    bool opened, bool ready, bool command_busy, bool disconnected)
{
    if (!opened || disconnected) {
        *was_ready = 0;
        return false;
    }
    if (ready) {
        *was_ready = 1;
        return true;
    }
    return command_busy && *was_ready != 0;
}

#endif
