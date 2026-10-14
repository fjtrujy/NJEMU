#ifndef NJEMU_PS2_PADMAN_EDGE_GAP_H
#define NJEMU_PS2_PADMAN_EDGE_GAP_H

#include <stdbool.h>
#include <stdint.h>

/* Diagnostic-only comparison between cumulative IOP-observed Square edges
 * and the two Square states presented to consecutive CPS1 frames.
 * No sampled state or button timing is modified by this counter. */
typedef struct ps2_padman_edge_gap {
    uint32_t previous_iop_edges;
    uint32_t iop_edges;
    uint32_t ee_edges;
    uint32_t extra_edges;
    uint32_t multi_edge_frames;
    uint32_t invisible_cycles;
    uint32_t max_edges_per_frame;
    uint32_t discontinuities;
    bool have_previous;
    bool previous_pressed;
} ps2_padman_edge_gap_t;

static inline void ps2_padman_edge_gap_observe(ps2_padman_edge_gap_t *p,
    uint32_t iop_edges, bool square_pressed)
{
    uint32_t delta;
    uint32_t presented;

    if (!p->have_previous) {
        p->previous_iop_edges = iop_edges;
        p->previous_pressed = square_pressed;
        p->have_previous = true;
        return;
    }

    delta = iop_edges - p->previous_iop_edges;
    presented = p->previous_pressed != square_pressed ? 1u : 0u;
    p->previous_iop_edges = iop_edges;
    p->previous_pressed = square_pressed;

    /* Controller reconnects, counter resets, and long pauses cannot be
     * interpreted as contiguous sub-frame button transition sequences. */
    if (delta > 32) {
        p->discontinuities++;
        return;
    }

    p->iop_edges += delta;
    p->ee_edges += presented;
    if (delta > presented)
        p->extra_edges += delta - presented;
    if (delta >= 2)
        p->multi_edge_frames++;
    if (delta >= 2 && !presented)
        p->invisible_cycles += delta / 2;
    if (delta > p->max_edges_per_frame)
        p->max_edges_per_frame = delta;
}

static inline void ps2_padman_edge_gap_reset_window(ps2_padman_edge_gap_t *p)
{
    uint32_t previous = p->previous_iop_edges;
    bool pressed = p->previous_pressed;
    bool have = p->have_previous;

    *p = (ps2_padman_edge_gap_t){0};
    p->previous_iop_edges = previous;
    p->previous_pressed = pressed;
    p->have_previous = have;
}

#endif /* NJEMU_PS2_PADMAN_EDGE_GAP_H */
