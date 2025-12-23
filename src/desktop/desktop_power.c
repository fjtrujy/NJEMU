#include <stdio.h>
#include <stdlib.h>
#include "common/power_driver.h"

typedef struct desktop_power {
} desktop_power_t;

static void *desktop_init(void) {
	desktop_power_t *desktop = (desktop_power_t*)calloc(1, sizeof(desktop_power_t));
	return desktop;
}

static void desktop_free(void *data) {
	desktop_power_t *desktop = (desktop_power_t*)data;
	free(desktop);
}

static int32_t desktop_batteryLifePercent(void *data) {
	return 100;
}

static bool desktop_IsBatteryCharging(void *data) {
	return true;
}

static void desktop_setCpuClock(void *data, int32_t clock) {
}

static void desktop_setLowestCpuClock(void *data) {
}

static int32_t desktop_getHighestCpuClock(void *data) {
	return 1;
}

power_driver_t power_desktop = {
	"desktop",
	desktop_init,
	desktop_free,
	desktop_batteryLifePercent,
	desktop_IsBatteryCharging,
	desktop_setCpuClock,
	desktop_setLowestCpuClock,
	desktop_getHighestCpuClock,
};