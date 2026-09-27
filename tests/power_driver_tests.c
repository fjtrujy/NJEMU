#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/power_driver.h"

static int32_t last_performance_level = -1;

static bool test_query_battery(power_battery_status_t *status)
{
	status->percent = 42;
	status->charging = true;
	return true;
}

static void test_set_performance_level(int32_t level)
{
	last_performance_level = level;
}

static const power_driver_t test_power_driver = {
	.ident = "test",
	.capabilities = POWER_CAP_BATTERY | POWER_CAP_PERFORMANCE,
	.lowest_performance_level = PLATFORM_PERFORMANCE_LEVEL_1,
	.highest_performance_level = PLATFORM_PERFORMANCE_LEVEL_HIGHEST,
	.queryBatteryStatus = test_query_battery,
	.setPerformanceLevel = test_set_performance_level,
};

const power_driver_t *const power_driver = &test_power_driver;

static void test_capabilities(void)
{
	assert(power_has_capability(POWER_CAP_BATTERY));
	assert(power_has_capability(POWER_CAP_PERFORMANCE));
	assert(power_has_capability(POWER_CAP_BATTERY | POWER_CAP_PERFORMANCE));
	assert(!power_has_capability(1u << 7));
}

static void test_battery_status(void)
{
	power_battery_status_t status = {0};

	assert(!power_query_battery_status(NULL));
	assert(power_query_battery_status(&status));
	assert(status.percent == 42);
	assert(status.charging);
}

static void test_performance_levels(void)
{
	last_performance_level = -1;
	power_set_performance_level(PLATFORM_PERFORMANCE_LEVEL_2);
	assert(last_performance_level == PLATFORM_PERFORMANCE_LEVEL_2);

	power_set_performance_level(PLATFORM_PERFORMANCE_LEVEL_LOWEST);
	assert(last_performance_level == PLATFORM_PERFORMANCE_LEVEL_1);

	power_set_performance_level(PLATFORM_PERFORMANCE_LEVEL_COUNT + 10);
	assert(last_performance_level == PLATFORM_PERFORMANCE_LEVEL_HIGHEST);

	power_set_lowest_performance_level();
	assert(last_performance_level == PLATFORM_PERFORMANCE_LEVEL_1);

	assert(power_get_highest_performance_level() ==
		PLATFORM_PERFORMANCE_LEVEL_HIGHEST);
}

int main(void)
{
	test_capabilities();
	test_battery_status();
	test_performance_levels();
	return 0;
}
