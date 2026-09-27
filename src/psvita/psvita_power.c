/******************************************************************************

	psvita_power.c

	PS Vita battery status and clock levels

******************************************************************************/

#include <psp2/power.h>
#include "common/power_driver.h"

static bool psvita_queryBatteryStatus(power_battery_status_t *status)
{
	status->percent = scePowerGetBatteryLifePercent();
	status->charging = scePowerIsBatteryCharging() != 0;
	return true;
}

/* ARM / bus / GPU / GPU crossbar clocks, from the system default to the maximum. */
static void psvita_setPerformanceLevel(int32_t level)
{
	int arm, bus, gpu, xbar;

	switch (level) {
	case PLATFORM_PERFORMANCE_LEVEL_1: arm = 366; bus = 166; gpu = 166; xbar = 111; break;
	case PLATFORM_PERFORMANCE_LEVEL_2: arm = 400; bus = 222; gpu = 222; xbar = 166; break;
	case PLATFORM_PERFORMANCE_LEVEL_HIGHEST: arm = 444; bus = 222; gpu = 222; xbar = 166; break;
	default: arm = 333; bus = 111; gpu = 111; xbar = 111; break;
	}
	scePowerSetArmClockFrequency(arm);
	scePowerSetBusClockFrequency(bus);
	scePowerSetGpuClockFrequency(gpu);
	scePowerSetGpuXbarClockFrequency(xbar);
}

const power_driver_t power_psvita = {
	.ident = "psvita",
	.capabilities = POWER_CAP_BATTERY | POWER_CAP_PERFORMANCE,
	.lowest_performance_level = PLATFORM_PERFORMANCE_LEVEL_LOWEST,
	.highest_performance_level = PLATFORM_PERFORMANCE_LEVEL_HIGHEST,
	.queryBatteryStatus = psvita_queryBatteryStatus,
	.setPerformanceLevel = psvita_setPerformanceLevel,
};
