#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "common/input_state.h"
#include "ps2/ps2_input_edge_latch.h"

/* This initial reproducer models only transitions already observed by an
 * ideal sampler. It does not imply stock PADMAN can acquire 4 ms pulses. */
static void reproduce_merged_repeated_taps(void)
{
    ps2_input_edge_latch_t old = {0};
    uint32_t first, second;
    unsigned visible_presses = 0;

    ps2_input_edge_latch_record(&old, 0);
    ps2_input_edge_latch_record(&old, PLATFORM_PAD_B1);
    ps2_input_edge_latch_record(&old, 0);
    ps2_input_edge_latch_record(&old, PLATFORM_PAD_B1);
    ps2_input_edge_latch_record(&old, 0);
    first = ps2_input_edge_latch_consume(&old, 0, 1);
    second = ps2_input_edge_latch_consume(&old, 0, 1);
    visible_presses += (first & PLATFORM_PAD_B1) != 0;
    visible_presses += ((second & PLATFORM_PAD_B1) != 0) &&
        ((first & PLATFORM_PAD_B1) == 0);
    assert(visible_presses == 1);
    printf("legacy repeat trace: 2 observed downs, %u game-visible down\n",
        visible_presses);
}

static void reproduce_phantom_direction_combination(void)
{
    ps2_input_edge_latch_t old = {0};
    uint32_t combined;

    ps2_input_edge_latch_record(&old, 0);
    ps2_input_edge_latch_record(&old, PLATFORM_PAD_RIGHT);
    ps2_input_edge_latch_record(&old, 0);
    ps2_input_edge_latch_record(&old, PLATFORM_PAD_DOWN);
    ps2_input_edge_latch_record(&old, 0);
    combined = ps2_input_edge_latch_consume(&old, 0, 1);
    assert((combined & (PLATFORM_PAD_RIGHT | PLATFORM_PAD_DOWN)) ==
        (PLATFORM_PAD_RIGHT | PLATFORM_PAD_DOWN));
    printf("legacy direction trace: synthesized simultaneous RIGHT+DOWN\n");
}

static void reproduce_reordered_samples(void)
{
    ps2_input_edge_latch_t old = {0};
    uint32_t stale, newer;

    ps2_input_edge_latch_record(&old, 0);
    stale = PLATFORM_PAD_B1;  /* main reads A, is preempted */
    newer = 0;               /* sampler reads B and records it */
    ps2_input_edge_latch_record(&old, newer);
    ps2_input_edge_latch_record(&old, stale);
    assert(ps2_input_edge_latch_consume(&old, stale, 1) ==
        PLATFORM_PAD_B1);
    printf("legacy interleaving: stale sample A applied after newer B\n");

    /* The inverse ownership order is also possible: the worker reads A
     * first, the main thread consumes newer B, then the worker records A. */
    ps2_input_edge_latch_reset(&old);
    ps2_input_edge_latch_record(&old, 0);
    stale = PLATFORM_PAD_B1; /* worker reads A, is preempted */
    newer = 0;              /* main reads B and consumes it */
    assert(ps2_input_edge_latch_consume(&old, newer, 1) == 0);
    ps2_input_edge_latch_record(&old, stale);
    assert(ps2_input_edge_latch_consume(&old, newer, 1) ==
        PLATFORM_PAD_B1);
}

int main(void)
{
    reproduce_merged_repeated_taps();
    reproduce_phantom_direction_combination();
    reproduce_reordered_samples();
    return 0;
}
