#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>

#include "common/input_state.h"
#include "ps2/ps2_input_sample_cache.h"
#include "ps2/ps2_pad_presence.h"
#include "ps2/ps2_pad_packet.h"
#include "ps2/ps2_input_capture.h"
#include "ps2/ps2_padman_frame_probe.h"
#include "ps2/ps2_video_filter_policy.h"

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

static void test_async_capture_short_taps_and_chords(void)
{
    ps2_input_capture_t capture = {0};
    input_state_t source, output;
    input_state_reset(&source);
    ps2_input_capture_record(&capture, &source);
    assert(ps2_input_capture_consume(&capture, &output));
    assert(output.buttons == 0);

    /* Entire Square press/release happens between two game frames. */
    source.buttons = PLATFORM_PAD_RIGHT | PLATFORM_PAD_B3;
    ps2_input_capture_record(&capture, &source);
    source.buttons = PLATFORM_PAD_RIGHT;
    ps2_input_capture_record(&capture, &source);
    assert(ps2_input_capture_consume(&capture, &output));
    assert(output.buttons == (PLATFORM_PAD_RIGHT | PLATFORM_PAD_B3));
    assert(ps2_input_capture_consume(&capture, &output));
    assert(output.buttons == PLATFORM_PAD_RIGHT);

    /* A short two-button chord survives together, with no held direction. */
    source.buttons = PLATFORM_PAD_B3 | PLATFORM_PAD_B2;
    ps2_input_capture_record(&capture, &source);
    source.buttons = 0;
    ps2_input_capture_record(&capture, &source);
    assert(ps2_input_capture_consume(&capture, &output));
    assert(output.buttons == (PLATFORM_PAD_B3 | PLATFORM_PAD_B2));
    assert(ps2_input_capture_consume(&capture, &output));
    assert(output.buttons == 0);

    /* Never latch a transient menu combo or movement in the same way. */
    source.buttons = PLATFORM_PAD_SELECT | PLATFORM_PAD_START | PLATFORM_PAD_LEFT;
    ps2_input_capture_record(&capture, &source);
    source.buttons = 0;
    ps2_input_capture_record(&capture, &source);
    assert(ps2_input_capture_consume(&capture, &output));
    assert(output.buttons == 0);

    source.buttons = PLATFORM_PAD_B1;
    ps2_input_capture_record(&capture, &source);
    ps2_input_capture_reset(&capture);
    assert(!ps2_input_capture_consume(&capture, &output));
}

static void test_padman_dma_publication_cadence(void)
{
    uint8_t dma[PS2_PADMAN_NEW_DMA_SIZE] = {0};
    ps2_padman_frame_probe_t probe = {0};
    uint32_t frame;
    uint32_t first = 41, second = 42;

    assert(!ps2_padman_dma_newest_frame(dma, sizeof(dma) - 1, &frame));
    assert(!ps2_padman_dma_newest_frame(NULL, sizeof(dma), &frame));
    memcpy(dma + PS2_PADMAN_NEW_FRAME_OFFSET, &first, sizeof(first));
    memcpy(dma + PS2_PADMAN_NEW_RECORD_SIZE +
        PS2_PADMAN_NEW_FRAME_OFFSET, &second, sizeof(second));
    assert(ps2_padman_dma_newest_frame(dma, sizeof(dma), &frame));
    assert(frame == 42);

    ps2_padman_frame_probe_observe(&probe, frame);
    ps2_padman_frame_probe_observe(&probe, frame);
    assert(probe.reads == 2 && probe.repeats == 1 && probe.advances == 0);

    ps2_padman_frame_probe_observe(&probe, 45);
    assert(probe.advances == 1 && probe.skipped == 2);
    ps2_padman_frame_probe_reset_window(&probe);
    assert(probe.reads == 0 && probe.previous == 45);
    ps2_padman_frame_probe_observe(&probe, 46);
    assert(probe.advances == 1 && probe.skipped == 0);
    ps2_padman_frame_probe_observe(&probe, 44);
    assert(probe.backwards == 1 && probe.previous == 44);

    /* Difference arithmetic handles 32-bit sequence wrap correctly. */
    ps2_padman_frame_probe_reset_window(&probe);
    probe.previous = UINT32_MAX - 1;
    first = UINT32_MAX;
    second = 0;
    memcpy(dma + PS2_PADMAN_NEW_FRAME_OFFSET, &first, sizeof(first));
    memcpy(dma + PS2_PADMAN_NEW_RECORD_SIZE +
        PS2_PADMAN_NEW_FRAME_OFFSET, &second, sizeof(second));
    assert(ps2_padman_dma_newest_frame(dma, sizeof(dma), &frame));
    assert(frame == 0);
    ps2_padman_frame_probe_observe(&probe, frame);
    assert(probe.advances == 1 && probe.skipped == 1);
}

int main(void)
{
    test_single_transport_gap();
    test_disconnect_does_not_stick_buttons();
    test_invalid_pad_packet_is_not_all_buttons();
    test_pad_command_does_not_change_controller_order();
    test_240p_filter_policy();
    test_async_capture_short_taps_and_chords();
    test_padman_dma_publication_cadence();
    return 0;
}
