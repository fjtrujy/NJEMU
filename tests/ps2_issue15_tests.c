#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>

#include "common/input_state.h"
#include "ps2/ps2_input_sample_cache.h"
#include "ps2/ps2_pad_presence.h"
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

int main(void)
{
    test_single_transport_gap();
    test_disconnect_does_not_stick_buttons();
    test_pad_command_does_not_change_controller_order();
    test_240p_filter_policy();
    return 0;
}
