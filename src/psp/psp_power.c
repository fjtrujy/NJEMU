#include <psppower.h>
#include "common/power_driver.h"

static bool psp_queryBatteryStatus(power_battery_status_t *status) {
	status->percent = scePowerGetBatteryLifePercent();
	status->charging = scePowerIsBatteryCharging() != 0;
	return true;
}

static void psp_setPerformanceLevel(int32_t level) {
	switch (level) {
	case PLATFORM_PERFORMANCE_LEVEL_1: scePowerSetClockFrequency(266, 266, 133); break;
	case PLATFORM_PERFORMANCE_LEVEL_2: scePowerSetClockFrequency(300, 300, 150); break;
	case PLATFORM_PERFORMANCE_LEVEL_HIGHEST: scePowerSetClockFrequency(333, 333, 166); break;
	default: scePowerSetClockFrequency(222, 222, 111); break;
	}
}

const power_driver_t power_psp = {
	.ident = "psp",
	.capabilities = POWER_CAP_BATTERY | POWER_CAP_PERFORMANCE,
	.lowest_performance_level = PLATFORM_PERFORMANCE_LEVEL_LOWEST,
	.highest_performance_level = PLATFORM_PERFORMANCE_LEVEL_HIGHEST,
	.queryBatteryStatus = psp_queryBatteryStatus,
	.setPerformanceLevel = psp_setPerformanceLevel,
};
