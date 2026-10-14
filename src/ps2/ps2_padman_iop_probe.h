#ifndef NJEMU_PS2_PADMAN_IOP_PROBE_H
#define NJEMU_PS2_PADMAN_IOP_PROBE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Reserved bytes of a PS2SDK new-PADMAN 128-byte EE record.
 * Only the separately instrumented diagnostic IRX writes this layout.
 * Stock PADMAN never provides the signature; do not interpret it. */
enum {
    PS2_IOP_RECORD_SIZE = 128,
    PS2_IOP_COUNTER_OFFSET = 117,
    PS2_IOP_DMA_BUFFER_SIZE = 256
};

typedef struct ps2_padman_iop_probe {
    uint32_t acquired;
    uint32_t square_edges;
} ps2_padman_iop_probe_t;

static inline uint32_t ps2_padman_iop_u32le(const uint8_t *source)
{
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8) |
        ((uint32_t)source[2] << 16) | ((uint32_t)source[3] << 24);
}

static inline bool ps2_padman_iop_probe_decode(const uint8_t *dma,
    size_t length, ps2_padman_iop_probe_t *out)
{
    uint32_t first, second;
    size_t offset;
    const uint8_t *payload;

    if (!dma || !out || length < PS2_IOP_DMA_BUFFER_SIZE)
        return false;

    first = ps2_padman_iop_u32le(dma + 88);
    second = ps2_padman_iop_u32le(dma + PS2_IOP_RECORD_SIZE + 88);
    offset = (uint32_t)(second - first) < UINT32_C(0x80000000)
        ? PS2_IOP_RECORD_SIZE : 0;
    payload = dma + offset + PS2_IOP_COUNTER_OFFSET;
    if (payload[8] != 'N' || payload[9] != 'J' || payload[10] != 1)
        return false;

    out->acquired = ps2_padman_iop_u32le(payload);
    out->square_edges = ps2_padman_iop_u32le(payload + 4);
    return true;
}

#endif /* NJEMU_PS2_PADMAN_IOP_PROBE_H */
