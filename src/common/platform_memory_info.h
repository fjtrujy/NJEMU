/******************************************************************************

	platform_memory_info.h

	Normalized platform memory telemetry used by the runtime memory policy.

******************************************************************************/

#ifndef PLATFORM_MEMORY_INFO_H
#define PLATFORM_MEMORY_INFO_H

#include <stdbool.h>
#include <stdint.h>

enum {
	PLATFORM_MEMORY_CAP_QUERY_FREE = 1u << 0,
	PLATFORM_MEMORY_CAP_QUERY_LARGEST_BLOCK = 1u << 1,
};

enum {
	PLATFORM_MEMORY_FREE_IS_ESTIMATE = 1u << 0,
	PLATFORM_MEMORY_LARGEST_IS_PROBED = 1u << 1,
	PLATFORM_MEMORY_LARGEST_IS_ESTIMATE = 1u << 2,
};

typedef struct platform_memory_info {
	uint64_t physical_total_bytes;
	uint64_t budget_cap_bytes;
	uint64_t free_bytes;
	uint64_t largest_free_block_bytes;
	uint32_t capabilities;
	uint32_t reliability_flags;
} platform_memory_info_t;

/* Clamp internally inconsistent values without inventing unavailable telemetry. */
void platform_memory_info_normalize(platform_memory_info_t *info);

/* Apply explicit test overrides. NULL means "no override". Invalid values are
 * ignored and reported via the return value.
 */
bool platform_memory_info_apply_override_values(platform_memory_info_t *info,
	const char *budget_mb, const char *largest_block_mb);

/* Apply NJEMU_MEMORY_BUDGET_MB / NJEMU_MEMORY_LARGEST_BLOCK_MB. */
void platform_memory_info_apply_env_overrides(platform_memory_info_t *info);

/* Free memory after the platform-wide NJEMU budget cap, if any. */
uint64_t platform_memory_info_effective_budget(const platform_memory_info_t *info);

/* Compatibility value for the legacy platform_driver::availableRam callback. */
uint32_t platform_memory_info_available_u32(const platform_memory_info_t *info);

void platform_memory_info_log(const platform_memory_info_t *info);

#endif /* PLATFORM_MEMORY_INFO_H */
