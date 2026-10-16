#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>

#include "common/input_state.h"
#include "ps2/ps2_input_sample_cache.h"
#include "ps2/ps2_pad_presence.h"
#include "ps2/ps2_pad_packet.h"
#include "ps2/ps2_video_filter_policy.h"
#include "ps2/ps2_native_240p_policy.h"
#include "ps2/ps2_input_edge_latch.h"

static void test_single_transport_gap(void)
{
    ps2_input_sample_cache_t cache = {0};
    input_state_t sample = {0};
    input_state_t out = {0};

    sample.buttons = PLATFORM_PAD_B1 | PLATFORM_PAD_RIGHT;
    sample.lx = INPUT_ANALOG_NEUTRAL;
    sample.ly = INPUT_ANALOG_NEUTRAL;
    assert(!ps2_input_sample_cache_update(&cache, true, false, NULL, &out));
    assert(ps2_input_sample_cache_update(&cache, true, true, &sample, &out));
    assert(out.buttons == sample.buttons);
    assert(ps2_input_sample_cache_update(&cache, true, false, NULL, &out));
    assert(out.buttons == sample.buttons);
    assert(!ps2_input_sample_cache_update(&cache, true, false, NULL, &out));
    assert(ps2_input_sample_cache_update(&cache, true, true, &sample, &out));
    sample.buttons = 0;
    assert(ps2_input_sample_cache_update(&cache, true, true, &sample, &out));
    assert(out.buttons == 0);
}

static void test_disconnect_does_not_stick_buttons(void)
{
    ps2_input_sample_cache_t cache = {0};
    input_state_t sample = {0};
    input_state_t out = {0};

    sample.buttons = PLATFORM_PAD_B1;
    assert(ps2_input_sample_cache_update(&cache, true, true, &sample, &out));
    assert(!ps2_input_sample_cache_update(&cache, false, false, NULL, &out));
    assert(!ps2_input_sample_cache_update(&cache, true, false, NULL, &out));
}

static void test_invalid_pad_packet_is_not_all_buttons(void)
{
    ps2_input_sample_cache_t cache = {0};
    input_state_t sample = {0}, output = {0};
    const size_t button_end = 4;

    assert(!ps2_pad_packet_valid(3, button_end, 0x73, 0xffef));
    assert(!ps2_pad_packet_valid(4, button_end, 0, 0xffef));
    assert(!ps2_pad_packet_valid(4, button_end, 0x73, 0));
    assert(ps2_pad_packet_valid(4, button_end, 0x41, 0xffff));
    assert(ps2_pad_packet_valid(4, button_end, 0x73, 0xffef));

    sample.buttons = PLATFORM_PAD_B3 | PLATFORM_PAD_RIGHT;
    assert(ps2_input_sample_cache_update(&cache, true, true, &sample, &output));
    assert(ps2_input_sample_cache_update(&cache, true,
        ps2_pad_packet_valid(4, button_end, 0, 0),
        NULL, &output));
    assert(output.buttons == sample.buttons);
    assert(!ps2_input_sample_cache_update(&cache, true,
        ps2_pad_packet_valid(4, button_end, 0, 0),
        NULL, &output));
    sample.buttons = 0;
    assert(ps2_input_sample_cache_update(&cache, true, true, &sample, &output));
    assert(output.buttons == 0);
}

static void test_pad_command_does_not_change_controller_order(void)
{
    uint8_t port0_ready = 0;
    uint8_t port1_ready = 0;
    uint8_t just_opened = 0;

    /* A controller still configuring cannot displace an established pad. */
    assert(!ps2_pad_presence_update(&just_opened, true, false, true, false));
    assert(ps2_pad_presence_update(&port0_ready, true, true, false, false));
    assert(ps2_pad_presence_update(&port1_ready, true, true, false, false));

    /* The first controller must remain logical P1 while PADMAN is busy. */
    assert(ps2_pad_presence_update(&port0_ready, true, false, true, false));
    assert(ps2_pad_presence_update(&port1_ready, true, true, false, false));
    assert(ps2_pad_presence_update(&port0_ready, true, true, false, false));

    /* A real removal immediately drops it from the active list. */
    assert(!ps2_pad_presence_update(&port0_ready, true, false, false, true));
    assert(!ps2_pad_presence_update(&port0_ready, true, false, true, false));
    assert(ps2_pad_presence_update(&port1_ready, true, true, false, false));
    assert(!ps2_pad_presence_update(&port1_ready, false, false, false, false));

    /* A discovery/reconnect phase is not a harmless command transition. */
    assert(ps2_pad_presence_update(&port0_ready, true, true, false, false));
    assert(!ps2_pad_presence_update(&port0_ready, true, false, false, true));
    assert(!ps2_pad_presence_update(&port0_ready, true, false, true, false));
}

static void test_240p_filter_policy(void)
{
    /* A 384x224 image stretched for 240p non-square output stays sharp. */
    assert(ps2_presentation_uses_nearest_filter(1, 384, 224, 656, 224));
    assert(ps2_presentation_uses_nearest_filter(1, 384, 224, 384, 224));
    assert(ps2_presentation_uses_nearest_filter(0, 384, 224, 768, 448));
    assert(!ps2_presentation_uses_nearest_filter(0, 384, 224, 656, 224));
    assert(!ps2_presentation_uses_nearest_filter(0, 0, 224, 656, 224));
    assert(!ps2_presentation_uses_nearest_filter(1, 384, 0, 656, 224));
}

static void test_native_cps_progressive_output(void)
{
    assert(PS2_CPS_NATIVE_WIDTH == 384);
    assert(PS2_CPS_NATIVE_HEIGHT == 224);
    assert(ps2_use_native_cps_240p(1, 1, 1));
    assert(!ps2_use_native_cps_240p(1, 1, 0));
    assert(!ps2_use_native_cps_240p(1, 0, 1));
    assert(!ps2_use_native_cps_240p(0, 1, 1));
}

static void test_between_frame_punch_edges(void)
{
    ps2_input_edge_latch_t player1 = {0};
    ps2_input_edge_latch_t player2 = {0};

    /* A full down/up transition between polls still yields one punch. */
    ps2_input_edge_latch_record(&player1, 0);
    ps2_input_edge_latch_record(&player1, PLATFORM_PAD_B3);
    ps2_input_edge_latch_record(&player1, 0);
    assert(ps2_input_edge_latch_consume(&player1, 0, 1) == PLATFORM_PAD_B3);
    assert(ps2_input_edge_latch_consume(&player1, 0, 1) == 0);

    /* Repeated taps and other players have independent lifetimes. */
    ps2_input_edge_latch_record(&player1, PLATFORM_PAD_B3);
    ps2_input_edge_latch_record(&player1, 0);
    assert(ps2_input_edge_latch_consume(&player2, 0, 1) == 0);
    assert(ps2_input_edge_latch_consume(&player1, 0, 1) == PLATFORM_PAD_B3);

    /* A held button remains level-triggered, never synthesized repeatedly. */
    ps2_input_edge_latch_record(&player1, PLATFORM_PAD_RIGHT);
    assert(ps2_input_edge_latch_consume(&player1, PLATFORM_PAD_RIGHT, 1) ==
        PLATFORM_PAD_RIGHT);
    assert(ps2_input_edge_latch_consume(&player1, PLATFORM_PAD_RIGHT, 1) ==
        PLATFORM_PAD_RIGHT);
    assert(ps2_input_edge_latch_consume(&player1, 0, 1) == 0);

    /* A stale cached sample must not create a second edge. */
    ps2_input_edge_latch_record(&player1, PLATFORM_PAD_B1);
    ps2_input_edge_latch_record(&player1, 0);
    assert(ps2_input_edge_latch_consume(&player1, PLATFORM_PAD_B1, 0) ==
        PLATFORM_PAD_B1);
    assert(ps2_input_edge_latch_consume(&player1, 0, 1) == 0);

    /* Menu chords are never constructed from two non-overlapping taps. */
    ps2_input_edge_latch_record(&player1, PLATFORM_PAD_START);
    ps2_input_edge_latch_record(&player1, 0);
    ps2_input_edge_latch_record(&player1, PLATFORM_PAD_SELECT);
    ps2_input_edge_latch_record(&player1, 0);
    assert(ps2_input_edge_latch_consume(&player1, 0, 1) == 0);

    ps2_input_edge_latch_record(&player1, PLATFORM_PAD_LEFT);
    ps2_input_edge_latch_reset(&player1);
    assert(ps2_input_edge_latch_consume(&player1, 0, 1) == 0);

    /* Arming after loading starts from the current physical level only;
     * a browser/previous-game tap cannot be delivered a second time. */
    ps2_input_edge_latch_reset(&player1);
    assert(ps2_input_edge_latch_consume(&player1, PLATFORM_PAD_B1, 1) ==
        PLATFORM_PAD_B1);
    assert(ps2_input_edge_latch_consume(&player1, 0, 1) == 0);
}

int main(void)
{
    test_single_transport_gap();
    test_disconnect_does_not_stick_buttons();
    test_invalid_pad_packet_is_not_all_buttons();
    test_pad_command_does_not_change_controller_order();
    test_240p_filter_policy();
    test_native_cps_progressive_output();
    test_between_frame_punch_edges();
    return 0;
}
