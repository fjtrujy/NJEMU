/******************************************************************************

	power_driver.h

******************************************************************************/

#ifndef POWER_DRIVER_H
#define POWER_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

enum
{
	POWER_CAP_BATTERY = 1u << 0,
	POWER_CAP_PERFORMANCE = 1u << 1,
};

enum
{
	PLATFORM_PERFORMANCE_LEVEL_LOWEST = 0,
	PLATFORM_PERFORMANCE_LEVEL_1,
	PLATFORM_PERFORMANCE_LEVEL_2,
	PLATFORM_PERFORMANCE_LEVEL_HIGHEST,
	PLATFORM_PERFORMANCE_LEVEL_COUNT
};

typedef struct power_battery_status
{
	int32_t percent;
	bool charging;
} power_battery_status_t;

typedef struct power_driver
{
	/* Human-readable identifier. */
	const char *ident;
	uint32_t capabilities;
	int32_t lowest_performance_level;
	int32_t highest_performance_level;
	bool (*queryBatteryStatus)(power_battery_status_t *status);
	void (*setPerformanceLevel)(int32_t level);

} power_driver_t;

extern int platform_performance_level;

extern const power_driver_t power_unsupported;
extern const power_driver_t *const power_driver;

bool power_has_capability(uint32_t capability);
bool power_query_battery_status(power_battery_status_t *status);
void power_set_performance_level(int32_t level);
void power_set_lowest_performance_level(void);
int32_t power_get_highest_performance_level(void);

#endif /* POWER_DRIVER_H */
