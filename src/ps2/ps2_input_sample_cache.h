#ifndef NJEMU_PS2_INPUT_SAMPLE_CACHE_H
#define NJEMU_PS2_INPUT_SAMPLE_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common/input_state.h"

/* Bridge one missed PADMAN read without turning a transient read failure into
 * an artificial button release. Never replay samples after a disconnect or
 * more than one consecutive failure. */
typedef struct ps2_input_sample_cache {
    input_state_t last_good;
    uint8_t valid;
    uint8_t missed;
} ps2_input_sample_cache_t;

static inline bool ps2_input_sample_cache_update(ps2_input_sample_cache_t *cache,
    bool connected, bool fresh_valid, const input_state_t *fresh, input_state_t *out)
{
    if (!connected) {
        cache->valid = 0;
        cache->missed = 0;
        return false;
    }
    if (fresh_valid && fresh != NULL) {
        cache->last_good = *fresh;
        cache->valid = 1;
        cache->missed = 0;
        *out = *fresh;
        return true;
    }
    if (cache->valid && cache->missed == 0) {
        cache->missed = 1;
        *out = cache->last_good;
        return true;
    }
    cache->valid = 0;
    return false;
}

#endif
