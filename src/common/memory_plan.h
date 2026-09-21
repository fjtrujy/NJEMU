/******************************************************************************

	memory_plan.h

	Pure game-specific cache budget planner.

******************************************************************************/

#ifndef MEMORY_PLAN_H
#define MEMORY_PLAN_H

#include <stdbool.h>
#include <stdint.h>

#include "platform_memory_info.h"

#define MEMORY_PLAN_BLOCK_SIZE (64u * 1024u)

typedef enum memory_tier {
	MEMORY_TIER_CRITICAL = 0,
	MEMORY_TIER_LOW,
	MEMORY_TIER_MEDIUM,
	MEMORY_TIER_HIGH,
	MEMORY_TIER_VERY_HIGH,
	MEMORY_TIER_COUNT
} memory_tier_t;

typedef enum memory_plan_core {
	MEMORY_PLAN_CORE_CPS2 = 0,
	MEMORY_PLAN_CORE_MVS,
} memory_plan_core_t;

typedef struct game_memory_requirements {
	memory_plan_core_t core;
	uint64_t mandatory_late_allocations_bytes;
	uint64_t gfx_or_crom_bytes;
	uint64_t pcm_or_vrom_bytes;
} game_memory_requirements_t;

typedef struct memory_plan {
	memory_tier_t tier;
	uint64_t measured_free_bytes;
	uint64_t largest_free_block_bytes;
	uint64_t safety_reserve_bytes;
	uint64_t cacheable_budget_bytes;
	uint64_t gfx_cache_bytes;
	uint64_t pcm_cache_bytes;
	uint64_t unused_cacheable_bytes;
	bool gfx_fully_resident;
	bool pcm_fully_resident;
} memory_plan_t;

memory_tier_t memory_plan_tier_for_cacheable_budget(uint64_t cacheable_budget_bytes);
const char *memory_plan_tier_name(memory_tier_t tier);

bool memory_plan_build(const platform_memory_info_t *memory,
	const game_memory_requirements_t *game, memory_plan_t *out);

void memory_plan_log(const memory_plan_t *plan);

#endif /* MEMORY_PLAN_H */
