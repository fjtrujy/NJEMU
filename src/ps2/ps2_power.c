#include <stdio.h>
#include <stdlib.h>
#include "common/power_driver.h"

typedef struct ps2_power {
} ps2_power_t;

static void *ps2_init(void) {
	ps2_power_t *ps2 = (ps2_power_t*)calloc(1, sizeof(ps2_power_t));
	return ps2;
}

static void ps2_free(void *data) {
	ps2_power_t *ps2 = (ps2_power_t*)data;
	free(ps2);
}

static int32_t ps2_batteryLifePercent(void *data) {
	return 100;
}

static bool ps2_IsBatteryCharging(void *data) {
	return true;
}

static void ps2_setCpuClock(void *data, int32_t clock) {
}

static void ps2_setLowestCpuClock(void *data) {
}

static int32_t ps2_getHighestCpuClock(void *data) {
	return 1;
}

power_driver_t power_ps2 = {
	"ps2",
	ps2_init,
	ps2_free,
	ps2_batteryLifePercent,
	ps2_IsBatteryCharging,
	ps2_setCpuClock,
	ps2_setLowestCpuClock,
	ps2_getHighestCpuClock,
};