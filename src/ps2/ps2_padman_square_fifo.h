#ifndef NJEMU_PS2_PADMAN_SQUARE_FIFO_H
#define NJEMU_PS2_PADMAN_SQUARE_FIFO_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Experimental one-button transport FIFO: a Square edge count uniquely
 * determines its alternating press/release sequence. Do not use for other
 * buttons without an actual ordered IOP event stream. */
enum { PS2_IOP_SQUARE_FIFO_CAPACITY = 8 };

typedef struct ps2_padman_square_fifo {
    uint32_t last_iop_edges;
    uint32_t emitted_presses;
    uint32_t recovered_presses;
    uint32_t overflows;
    uint32_t resyncs;
    uint8_t next;
    uint8_t pending;
    uint8_t max_pending;
    bool initialized;
    bool last_physical;
    bool last_emitted;
    bool transitions[PS2_IOP_SQUARE_FIFO_CAPACITY];
} ps2_padman_square_fifo_t;

static inline void ps2_padman_square_fifo_reset(ps2_padman_square_fifo_t *q)
{
    memset(q, 0, sizeof(*q));
}

/* One CPS1 frame may present only one button state. Replaying the FIFO
 * deliberately stretches sub-frame physical transitions across frames.
 * This may differ from original CPS1 behavior and must remain opt-in. */
static inline bool ps2_padman_square_fifo_sample(
    ps2_padman_square_fifo_t *q, uint32_t iop_edges, bool physical)
{
    uint32_t delta;
    bool next_physical;
    uint32_t i;

    if (!q->initialized) {
        q->initialized = true;
        q->last_iop_edges = iop_edges;
        q->last_physical = physical;
        q->last_emitted = physical;
        return physical;
    }

    delta = iop_edges - q->last_iop_edges;
    q->last_iop_edges = iop_edges;

    /* Driver reset/disconnect, malformed data or overfull backlog:
     * drop history and resynchronize rather than synthesize stale input. */
    if (delta > PS2_IOP_SQUARE_FIFO_CAPACITY ||
        q->pending + delta > PS2_IOP_SQUARE_FIFO_CAPACITY) {
        q->overflows++;
        q->pending = 0;
        q->next = 0;
        q->last_physical = physical;
        q->last_emitted = physical;
        return physical;
    }

    next_physical = q->last_physical;
    for (i = 0; i < delta; ++i) {
        next_physical = !next_physical;
        q->transitions[(q->next + q->pending) %
            PS2_IOP_SQUARE_FIFO_CAPACITY] = next_physical;
        q->pending++;
    }
    q->last_physical = physical;

    if (next_physical != physical) {
        q->resyncs++;
        q->pending = 0;
        q->next = 0;
        q->last_emitted = physical;
        return physical;
    }

    if (q->pending > q->max_pending)
        q->max_pending = q->pending;
    if (q->pending > 0) {
        bool output = q->transitions[q->next];
        q->next = (q->next + 1) % PS2_IOP_SQUARE_FIFO_CAPACITY;
        q->pending--;
        if (output && !q->last_emitted) {
            q->emitted_presses++;
            if (!physical)
                q->recovered_presses++;
        }
        q->last_emitted = output;
        return output;
    }

    q->last_emitted = physical;
    return physical;
}

/* Preserve pending events and last state across per-window profile reports. */
static inline void ps2_padman_square_fifo_reset_stats(
    ps2_padman_square_fifo_t *q)
{
    q->emitted_presses = 0;
    q->recovered_presses = 0;
    q->overflows = 0;
    q->resyncs = 0;
    q->max_pending = q->pending;
}

#endif /* NJEMU_PS2_PADMAN_SQUARE_FIFO_H */
