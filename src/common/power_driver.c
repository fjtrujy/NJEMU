/******************************************************************************

	power_driver.c

******************************************************************************/

#include <stddef.h>
#include "power_driver.h"

int platform_performance_level = PLATFORM_PERFORMANCE_LEVEL_HIGHEST;

const power_driver_t power_unsupported = {
	.ident = "unsupported",
};

bool power_has_capability(uint32_t capability)
{
	return power_driver != NULL &&
		(power_driver->capabilities & capability) == capability;
}

bool power_query_battery_status(power_battery_status_t *status)
{
	if (status == NULL)
		return false;

	status->percent = -1;
	status->charging = false;

	if (!power_has_capability(POWER_CAP_BATTERY) ||
		power_driver->queryBatteryStatus == NULL)
		return false;

	return power_driver->queryBatteryStatus(status);
}

void power_set_performance_level(int32_t level)
{
	if (!power_has_capability(POWER_CAP_PERFORMANCE) ||
		power_driver->setPerformanceLevel == NULL)
		return;

	if (level < power_driver->lowest_performance_level)
		level = power_driver->lowest_performance_level;
	if (level > power_driver->highest_performance_level)
		level = power_driver->highest_performance_level;

	power_driver->setPerformanceLevel(level);
}

void power_set_lowest_performance_level(void)
{
	if (!power_has_capability(POWER_CAP_PERFORMANCE))
		return;
	power_set_performance_level(power_driver->lowest_performance_level);
}

int32_t power_get_highest_performance_level(void)
{
	if (!power_has_capability(POWER_CAP_PERFORMANCE))
		return PLATFORM_PERFORMANCE_LEVEL_HIGHEST;
	return power_driver->highest_performance_level;
}
