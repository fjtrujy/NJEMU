#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "common/frame_pacing.h"

static void test_uncapped_unsynced(void)
{
	assert(!frame_pacing_should_sync_flip(false, VSYNC_MODE_OFF, 1000, 5000));
	assert(!frame_pacing_should_sync_flip(false, VSYNC_MODE_COUNT, 1000, 5000));
	assert(frame_pacing_sleep_us(false, 1000, 5000) == 0);
}

static void test_uncapped_vsync(void)
{
	assert(frame_pacing_should_sync_flip(false, VSYNC_MODE_ON, 1000, 5000));
	assert(frame_pacing_should_sync_flip(false, VSYNC_MODE_ON, 5100, 5000));
	assert(frame_pacing_sleep_us(false, 1000, 5000) == 0);
}

static void test_uncapped_adaptive_vsync(void)
{
	assert(frame_pacing_should_sync_flip(false, VSYNC_MODE_ADAPTIVE, 1000, 5000));
	assert(!frame_pacing_should_sync_flip(false, VSYNC_MODE_ADAPTIVE, 4900, 5000));
	assert(!frame_pacing_should_sync_flip(false, VSYNC_MODE_ADAPTIVE, 4950, 5000));
	assert(!frame_pacing_should_sync_flip(false, VSYNC_MODE_ADAPTIVE, 5000, 5000));
	assert(!frame_pacing_should_sync_flip(false, VSYNC_MODE_ADAPTIVE, 5100, 5000));
	assert(frame_pacing_sleep_us(false, 1000, 5000) == 0);
}

static void test_limited_unsynced(void)
{
	assert(!frame_pacing_should_sync_flip(true, VSYNC_MODE_OFF, 1000, 5000));
	assert(frame_pacing_sleep_us(true, 1000, 5000) == 4000);
	assert(frame_pacing_sleep_us(true, 5000, 5000) == 0);
	assert(frame_pacing_sleep_us(true, 6000, 5000) == 0);
}

static void test_limited_vsync(void)
{
	assert(frame_pacing_should_sync_flip(true, VSYNC_MODE_ON, 1000, 5000));
	assert(!frame_pacing_should_sync_flip(true, VSYNC_MODE_ON, 4900, 5000));
	assert(!frame_pacing_should_sync_flip(true, VSYNC_MODE_ON, 4950, 5000));
	assert(!frame_pacing_should_sync_flip(true, VSYNC_MODE_ON, 5000, 5000));
	assert(!frame_pacing_should_sync_flip(true, VSYNC_MODE_ON, 5100, 5000));
	assert(frame_pacing_should_sync_flip(true, VSYNC_MODE_ADAPTIVE, 1000, 5000));
	assert(!frame_pacing_should_sync_flip(true, VSYNC_MODE_ADAPTIVE, 5100, 5000));

	/* Most importantly, after a blocking VBlank advances the clock past the
	 * deadline, there must be no second software wait. */
	assert(frame_pacing_sleep_us(true, 5100, 5000) == 0);
	assert(frame_pacing_sleep_us(true, 4800, 5000) == 200);
}

int main(void)
{
	test_uncapped_unsynced();
	test_uncapped_vsync();
	test_uncapped_adaptive_vsync();
	test_limited_unsynced();
	test_limited_vsync();
	return 0;
}
