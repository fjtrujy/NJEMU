#ifndef NJEMU_PS2_PADMAN_FRAME_PROBE_H
#define NJEMU_PS2_PADMAN_FRAME_PROBE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Diagnostic for PS2SDK's "new" PADMAN/libpad DMA layout:
 * two 128-byte records, each with a 32-bit frame at offset 88.
 * Call only after padRead(), which performs SyncDCache() for both records.
 * This observes IOP->EE publications, not independent physical pad polls. */
enum {
    PS2_PADMAN_NEW_RECORD_SIZE = 128,
    PS2_PADMAN_NEW_FRAME_OFFSET = 88,
    PS2_PADMAN_NEW_DMA_SIZE = 256
};

typedef struct ps2_padman_frame_probe {
    uint32_t previous;
    uint32_t reads;
    uint32_t advances;
    uint32_t repeats;
    uint32_t skipped;
    uint32_t backwards;
    bool have_previous;
} ps2_padman_frame_probe_t;

static inline bool ps2_padman_dma_newest_frame(const uint8_t *dma,
    size_t size, uint32_t *out)
{
    uint32_t first, second;
    if (dma == NULL || out == NULL || size < PS2_PADMAN_NEW_DMA_SIZE)
        return false;
    memcpy(&first, dma + PS2_PADMAN_NEW_FRAME_OFFSET, sizeof(first));
    memcpy(&second, dma + PS2_PADMAN_NEW_RECORD_SIZE +
        PS2_PADMAN_NEW_FRAME_OFFSET, sizeof(second));
    /* Positive modulo-2^32 difference selects the newer record, even
     * when its counter wraps through zero. */
    *out = (uint32_t)(second - first) < UINT32_C(0x80000000) ?
        second : first;
    return true;
}

static inline void ps2_padman_frame_probe_observe(ps2_padman_frame_probe_t *probe,
    uint32_t frame)
{
    uint32_t difference;
    probe->reads++;
    if (!probe->have_previous) {
        probe->have_previous = true;
        probe->previous = frame;
        return;
    }
    difference = frame - probe->previous;
    if (difference == 0) {
        probe->repeats++;
    } else if (difference < UINT32_C(0x80000000)) {
        probe->advances++;
        probe->skipped += difference - 1;
        probe->previous = frame;
    } else {
        /* Unexpected rollback, including reset/reconnect: rebase so a
         * later window cannot report an enormous false skipped count. */
        probe->backwards++;
        probe->previous = frame;
    }
}

static inline void ps2_padman_frame_probe_reset_window(ps2_padman_frame_probe_t *probe)
{
    uint32_t previous = probe->previous;
    bool have_previous = probe->have_previous;
    memset(probe, 0, sizeof(*probe));
    probe->previous = previous;
    probe->have_previous = have_previous;
}

#endif /* NJEMU_PS2_PADMAN_FRAME_PROBE_H */
