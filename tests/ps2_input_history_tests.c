#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "common/input_state.h"
#include "ps2/ps2_input_history.h"
#include "ps2/ps2_input_sample_cache.h"

#define B1 PLATFORM_PAD_B1
#define B2 PLATFORM_PAD_B2
#define RIGHT PLATFORM_PAD_RIGHT
#define DOWN PLATFORM_PAD_DOWN

static void short_press(void)
{
    ps2_input_history_t h = {0};

    ps2_input_history_set_live(&h, 0);
    ps2_input_history_record(&h, B1); /* t=0 ms */
    ps2_input_history_record(&h, 0);  /* t=5 ms */
    assert(ps2_input_history_consume(&h) == B1); /* t=16 */
    assert(ps2_input_history_consume(&h) == 0);  /* t=32 */
    printf("short press: 1/1 downs, first visible after 16 ms\n");
}

static void repeated_short_presses(void)
{
    ps2_input_history_t h = {0};
    uint32_t expected[] = {B1, 0, B1, 0};
    uint32_t prev = 0;
    unsigned i, visible_downs = 0;

    ps2_input_history_set_live(&h, 0);
    ps2_input_history_record(&h, B1); /* t=0 */
    ps2_input_history_record(&h, 0);  /* t=4 */
    ps2_input_history_record(&h, B1); /* t=8 */
    ps2_input_history_record(&h, 0);  /* t=12 */
    for (i = 0; i < sizeof(expected)/sizeof(expected[0]); i++) {
        uint32_t got = ps2_input_history_consume(&h); /* t=16,32,48,64 */
        assert(got == expected[i]);
        visible_downs += (got & B1) && !(prev & B1);
        prev = got;
    }
    assert(visible_downs == 2);
    printf("repeated presses: 2/2 downs, delivery at 16 and 48 ms\n");
}

static void motion_and_combinations(void)
{
    ps2_input_history_t h = {0};
    uint32_t expected[] = {DOWN, DOWN | RIGHT, RIGHT, RIGHT | B1, RIGHT};
    unsigned i;

    ps2_input_history_set_live(&h, 0);
    for (i = 0; i < sizeof(expected)/sizeof(expected[0]); i++)
        ps2_input_history_record(&h, expected[i]);
    for (i = 0; i < sizeof(expected)/sizeof(expected[0]); i++)
        assert(ps2_input_history_consume(&h) == expected[i]);

    ps2_input_history_set_live(&h, 0);
    ps2_input_history_record(&h, RIGHT);
    ps2_input_history_record(&h, RIGHT | B1);
    assert(ps2_input_history_consume(&h) == (RIGHT | B1));
    assert(ps2_input_history_consume(&h) == (RIGHT | B1));
    ps2_input_history_record(&h, RIGHT);
    assert(ps2_input_history_consume(&h) == RIGHT);
}

static void no_phantom_chords(void)
{
    ps2_input_history_t h = {0};

    ps2_input_history_set_live(&h, 0);
    ps2_input_history_record(&h, RIGHT);
    ps2_input_history_record(&h, 0);
    ps2_input_history_record(&h, DOWN);
    ps2_input_history_record(&h, 0);
    assert(ps2_input_history_consume(&h) == RIGHT);
    assert(ps2_input_history_consume(&h) == 0);
    assert(ps2_input_history_consume(&h) == DOWN);
    assert(ps2_input_history_consume(&h) == 0);
}

static void alternating_and_simultaneous(void)
{
    ps2_input_history_t h = {0};

    ps2_input_history_set_live(&h, 0);
    ps2_input_history_record(&h, B1);
    ps2_input_history_record(&h, 0);
    ps2_input_history_record(&h, B2);
    ps2_input_history_record(&h, 0);
    assert(ps2_input_history_consume(&h) == B1);
    assert(ps2_input_history_consume(&h) == 0);
    assert(ps2_input_history_consume(&h) == B2);
    assert(ps2_input_history_consume(&h) == 0);

    ps2_input_history_set_live(&h, 0);
    ps2_input_history_record(&h, B1 | B2);
    ps2_input_history_record(&h, 0);
    assert(ps2_input_history_consume(&h) == (B1 | B2));
    assert(ps2_input_history_consume(&h) == 0);
}

static void held_and_repressed(void)
{
    ps2_input_history_t h = {0};

    ps2_input_history_set_live(&h, 0);
    ps2_input_history_record(&h, B1);
    assert(ps2_input_history_consume(&h) == B1);
    assert(ps2_input_history_consume(&h) == B1);
    ps2_input_history_record(&h, 0);
    ps2_input_history_record(&h, B1);
    assert(ps2_input_history_consume(&h) == 0);
    assert(ps2_input_history_consume(&h) == B1);
    assert(ps2_input_history_consume(&h) == B1);
}

static void cached_invalid_and_disconnect(void)
{
    ps2_input_history_t h = {0};
    ps2_input_sample_cache_t cache = {0};
    input_state_t fresh = {0}, out = {0};

    fresh.buttons = B1;
    assert(ps2_input_sample_cache_update(&cache, true, true, &fresh, &out));
    ps2_input_history_set_live(&h, 0);
    ps2_input_history_record(&h, out.buttons);
    assert(ps2_input_sample_cache_update(&cache, true, false, NULL, &out));
    /* A cached packet must not record a new transition. */
    assert(h.count == 1);
    assert(ps2_input_history_consume(&h) == B1);
    assert(!ps2_input_sample_cache_update(&cache, true, false, NULL, &out));
    ps2_input_history_reset(&h);
    assert(ps2_input_history_consume(&h) == 0);

    assert(!ps2_input_sample_cache_update(&cache, false, false, NULL, &out));
    assert(!cache.valid);
}

static void multiplayer_menu_and_axes(void)
{
    ps2_input_history_t p1 = {0}, p2 = {0};
    input_state_t latest = {0};

    ps2_input_history_set_live(&p1, 0);
    ps2_input_history_set_live(&p2, 0);
    ps2_input_history_record(&p1, B1);
    ps2_input_history_record(&p1, 0);
    ps2_input_history_record(&p2, B2);
    ps2_input_history_record(&p2, 0);
    assert(ps2_input_history_consume(&p1) == B1);
    assert(ps2_input_history_consume(&p2) == B2);
    ps2_input_history_set_live(&p1, 0); /* leaving game for menu */
    assert(ps2_input_history_consume(&p1) == 0);
    ps2_input_history_reset(&p2);       /* disconnected */
    assert(ps2_input_history_consume(&p2) == 0);

    latest.buttons = PLATFORM_PAD_START | PLATFORM_PAD_SELECT;
    latest.lx = 0x20;
    latest.ly = 0xe0;
    latest.axis_flags = INPUT_AXIS_LX | INPUT_AXIS_LY;
    ps2_input_history_set_live(&p1, 0);
    ps2_input_history_record(&p1, B1);
    ps2_input_history_record(&p1, 0);
    latest.buttons = (latest.buttons & ~PS2_INPUT_EDGE_MASK) |
        ps2_input_history_consume(&p1);
    assert(latest.buttons == (PLATFORM_PAD_START | PLATFORM_PAD_SELECT | B1));
    assert(latest.lx == 0x20 && latest.ly == 0xe0);
}

static void bounded_backlogs(void)
{
    ps2_input_history_t h = {0};
    unsigned i;

    ps2_input_history_set_live(&h, 0);
    for (i = 0; i < PS2_INPUT_HISTORY_REPLAY_LIMIT + 1; i++)
        ps2_input_history_record(&h, (i & 1) ? 0 : B1);
    assert(h.count == PS2_INPUT_HISTORY_REPLAY_LIMIT + 1);
    assert(ps2_input_history_consume(&h) == h.observed);
    assert(h.count == 0 && h.backlog_resyncs == 1);

    ps2_input_history_set_live(&h, 0);
    for (i = 0; i < PS2_INPUT_HISTORY_CAPACITY + 1; i++)
        ps2_input_history_record(&h, (i & 1) ? 0 : B1);
    assert(h.overflows == 1);
    assert(h.count == 0 && h.presented == h.observed);
    assert(ps2_input_history_consume(&h) == h.observed);
}

static void worker_phase_latency_model(void)
{
    /* Idealized 60 Hz frame and 8 ms worker phases. A direct frame-boundary
     * padRead() can see a just-published PADMAN snapshot before the worker
     * has copied it; exclusive worker ownership may then add one frame.
     * This is a phase model, not a measured controller or IOP cadence. */
    unsigned phase, event_us, additional_frame_cases = 0, cases = 0;
    const unsigned frame_us = 16667;
    const unsigned worker_us = 8000;
    unsigned worst_extra_frames = 0;

    for (phase = 0; phase < worker_us; phase += 100) {
        unsigned last_worker = phase;
        while (last_worker + worker_us <= frame_us)
            last_worker += worker_us;
        for (event_us = 0; event_us <= frame_us; event_us += 100) {
            unsigned additional_frames = event_us > last_worker ? 1 : 0;
            additional_frame_cases += additional_frames;
            cases++;
            if (additional_frames > worst_extra_frames)
                worst_extra_frames = additional_frames;
        }
    }
    assert(worst_extra_frames == 1);
    printf("worker phase model: %.1f%% of publication/worker phases could add one frame (up to 16.7 ms)\n",
        100.0 * (double)additional_frame_cases / cases);
}

int main(void)
{
    short_press();
    repeated_short_presses();
    motion_and_combinations();
    no_phantom_chords();
    alternating_and_simultaneous();
    held_and_repressed();
    cached_invalid_and_disconnect();
    multiplayer_menu_and_axes();
    bounded_backlogs();
    worker_phase_latency_model();
    return 0;
}
