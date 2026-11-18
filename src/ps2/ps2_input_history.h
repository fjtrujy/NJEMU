#ifndef NJEMU_PS2_INPUT_HISTORY_H
#define NJEMU_PS2_INPUT_HISTORY_H

#include <stdint.h>
#include "ps2/ps2_input_edge_latch.h"

/* Ordered, complete gameplay-button levels. A level represents one validated
 * PADMAN snapshot, not an independently timed physical controller event. */
#define PS2_INPUT_HISTORY_CAPACITY 32u
#define PS2_INPUT_HISTORY_REPLAY_LIMIT 8u

typedef struct ps2_input_history {
    uint32_t states[PS2_INPUT_HISTORY_CAPACITY];
    uint32_t observed;
    uint32_t presented;
    uint32_t overflows;
    uint32_t backlog_resyncs;
    uint8_t head;
    uint8_t count;
    uint8_t initialized;
} ps2_input_history_t;

static inline void ps2_input_history_reset(ps2_input_history_t *history)
{
    history->observed = 0;
    history->presented = 0;
    history->head = 0;
    history->count = 0;
    history->initialized = 0;
}

/* UI and startup use the current snapshot, never historical gameplay edges. */
static inline void ps2_input_history_set_live(ps2_input_history_t *history,
    uint32_t buttons)
{
    history->observed = buttons & PS2_INPUT_EDGE_MASK;
    history->presented = history->observed;
    history->head = 0;
    history->count = 0;
    history->initialized = 1;
}

static inline void ps2_input_history_record(ps2_input_history_t *history,
    uint32_t buttons)
{
    uint32_t level = buttons & PS2_INPUT_EDGE_MASK;
    unsigned tail;

    if (!history->initialized) {
        ps2_input_history_set_live(history, level);
        return;
    }
    if (level == history->observed)
        return;

    history->observed = level;
    if (history->count == PS2_INPUT_HISTORY_CAPACITY) {
        /* A hopelessly stale replay is worse than losing the old history.
         * Immediately synchronize to the latest real button state. */
        history->overflows++;
        ps2_input_history_set_live(history, level);
        return;
    }

    tail = (history->head + history->count) % PS2_INPUT_HISTORY_CAPACITY;
    history->states[tail] = level;
    history->count++;
}

/* Called once for each emulation input poll. Preserve reversals (short pulses
 * and release/repress gaps), but collapse monotonic additions/removals to the
 * freshest state to avoid delaying ordinary chords and held controls. */
static inline uint32_t ps2_input_history_consume(ps2_input_history_t *history)
{
    uint32_t prior, changed_once = 0, changed_twice = 0;
    uint32_t result;
    unsigned i;

    if (!history->initialized)
        return 0;
    if (history->count > PS2_INPUT_HISTORY_REPLAY_LIMIT) {
        history->backlog_resyncs++;
        ps2_input_history_set_live(history, history->observed);
        return history->presented;
    }

    if (history->count == 0) {
        history->presented = history->observed;
        return history->presented;
    }

    prior = history->presented;
    for (i = 0; i < history->count; i++) {
        uint32_t next = history->states[
            (history->head + i) % PS2_INPUT_HISTORY_CAPACITY];
        uint32_t changes = prior ^ next;
        changed_twice |= changed_once & changes;
        changed_once |= changes;
        prior = next;
    }

    if (!changed_twice) {
        /* No edge would disappear by presenting the newest snapshot. */
        ps2_input_history_set_live(history, history->observed);
        return history->presented;
    }

    result = history->states[history->head];
    history->head = (history->head + 1u) % PS2_INPUT_HISTORY_CAPACITY;
    history->count--;
    history->presented = result;
    return result;
}

#endif /* NJEMU_PS2_INPUT_HISTORY_H */
