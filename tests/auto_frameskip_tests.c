#include <assert.h>

#include "common/auto_frameskip.h"

static void test_presentation_skip_does_not_reduce_emulation_speed(void)
{
	const uint64_t elapsed_us = 200000;

	assert(auto_frameskip_frame_rate(6, elapsed_us) == 30.0f);
	assert(auto_frameskip_speed_percent(12, elapsed_us, 60.0f) == 100.0f);
}

static void test_on_time_recovery_reduces_skip_level_gradually(void)
{
	int level = 3;
	int adjustment = 0;

	auto_frameskip_adjust_level(&level, &adjustment, 100.0f);
	auto_frameskip_adjust_level(&level, &adjustment, 100.0f);
	assert(level == 3);
	auto_frameskip_adjust_level(&level, &adjustment, 100.0f);
	assert(level == 2);
	assert(adjustment == 0);
}

static void test_small_slowdown_requests_bounded_recovery(void)
{
	int level = 0;
	int adjustment = 0;

	auto_frameskip_adjust_level(&level, &adjustment, 95.0f);
	assert(level == 0);
	assert(adjustment == -1);
	auto_frameskip_adjust_level(&level, &adjustment, 95.0f);
	assert(level == 1);
	assert(adjustment == 0);
}

static void test_heavy_slowdown_never_exceeds_maximum_level(void)
{
	int level = AUTO_FRAMESKIP_LEVEL_COUNT - 2;
	int adjustment = 0;

	auto_frameskip_adjust_level(&level, &adjustment, 50.0f);
	assert(level == AUTO_FRAMESKIP_LEVEL_COUNT - 1);
	assert(adjustment > -2);
}

static void test_recovery_returns_to_no_skip_without_oscillation(void)
{
	int level = 1;
	int adjustment = 0;
	int i;

	for (i = 0; i < 3; i++)
		auto_frameskip_adjust_level(&level, &adjustment, 100.0f);
	assert(level == 0);
	assert(adjustment == 0);

	for (i = 0; i < 6; i++)
		auto_frameskip_adjust_level(&level, &adjustment, 100.0f);
	assert(level == 0);
	assert(adjustment == 0);
}

int main(void)
{
	test_presentation_skip_does_not_reduce_emulation_speed();
	test_on_time_recovery_reduces_skip_level_gradually();
	test_small_slowdown_requests_bounded_recovery();
	test_heavy_slowdown_never_exceeds_maximum_level();
	test_recovery_returns_to_no_skip_without_oscillation();
	return 0;
}
