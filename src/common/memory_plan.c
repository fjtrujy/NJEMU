/******************************************************************************

	memory_plan.c

******************************************************************************/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory_plan.h"

#define KIB_BYTES (1024ull)
#define MIB_BYTES (1024ull * 1024ull)

typedef struct cache_region_policy {
	uint32_t floor_kb;
	uint32_t weight;
	uint32_t cap_kb;
} cache_region_policy_t;

typedef struct memory_tier_policy {
	const char *name;
	uint32_t min_cacheable_mb;
	uint32_t safety_reserve_kb;
	cache_region_policy_t cps2_gfx;
	cache_region_policy_t mvs_crom;
	cache_region_policy_t mvs_pcm;
} memory_tier_policy_t;

typedef struct region_target {
	uint64_t source_bytes;
	uint64_t source_ceiling;
	uint64_t contiguous_ceiling;
	uint64_t policy_ceiling;
	uint64_t target;
	uint32_t weight;
} region_target_t;

static const memory_tier_policy_t tier_policies[MEMORY_TIER_COUNT] = {
	[MEMORY_TIER_CRITICAL] = {
		.name = "CRITICAL",
		.min_cacheable_mb = 0,
		.safety_reserve_kb = 1024,
		.cps2_gfx = { 2048, 1, 0 },
		.mvs_crom = { 2048, 4, 0 },
		.mvs_pcm = { 0, 1, 1024 },
	},
	[MEMORY_TIER_LOW] = {
		.name = "LOW",
		.min_cacheable_mb = 6,
		.safety_reserve_kb = 2048,
		.cps2_gfx = { 4096, 1, 0 },
		.mvs_crom = { 4096, 3, 0 },
		.mvs_pcm = { 1024, 1, 3072 },
	},
	[MEMORY_TIER_MEDIUM] = {
		.name = "MEDIUM",
		.min_cacheable_mb = 12,
		.safety_reserve_kb = 2048,
		.cps2_gfx = { 8192, 1, 0 },
		.mvs_crom = { 8192, 4, 0 },
		.mvs_pcm = { 2048, 1, 3072 },
	},
	[MEMORY_TIER_HIGH] = {
		.name = "HIGH",
		.min_cacheable_mb = 24,
		.safety_reserve_kb = 2048,
		.cps2_gfx = { 8192, 1, 0 },
		.mvs_crom = { 12288, 5, 0 },
		.mvs_pcm = { 3072, 1, 3072 },
	},
	[MEMORY_TIER_VERY_HIGH] = {
		.name = "VERY_HIGH",
		.min_cacheable_mb = 40,
		.safety_reserve_kb = 4096,
		.cps2_gfx = { 8192, 1, 0 },
		.mvs_crom = { 12288, 5, 0 },
		.mvs_pcm = { 3072, 1, 0 },
	},
};

static uint64_t align_down_block(uint64_t bytes) {
	return bytes - (bytes % MEMORY_PLAN_BLOCK_SIZE);
}

static uint64_t align_up_block(uint64_t bytes) {
	uint64_t remainder;
	if (bytes == 0) {
		return 0;
	}
	remainder = bytes % MEMORY_PLAN_BLOCK_SIZE;
	if (remainder == 0) {
		return bytes;
	}
	if (bytes > UINT64_MAX - (MEMORY_PLAN_BLOCK_SIZE - remainder)) {
		return UINT64_MAX - (UINT64_MAX % MEMORY_PLAN_BLOCK_SIZE);
	}
	return bytes + MEMORY_PLAN_BLOCK_SIZE - remainder;
}

static uint64_t min_u64(uint64_t a, uint64_t b) {
	return a < b ? a : b;
}

static uint64_t max_u64(uint64_t a, uint64_t b) {
	return a > b ? a : b;
}

memory_tier_t memory_plan_tier_for_cacheable_budget(uint64_t cacheable_budget_bytes) {
	memory_tier_t tier = MEMORY_TIER_CRITICAL;
	int i;
	for (i = 0; i < MEMORY_TIER_COUNT; ++i) {
		if (cacheable_budget_bytes >= (uint64_t)tier_policies[i].min_cacheable_mb * MIB_BYTES) {
			tier = (memory_tier_t)i;
		}
	}
	return tier;
}

const char *memory_plan_tier_name(memory_tier_t tier) {
	if (tier < 0 || tier >= MEMORY_TIER_COUNT) {
		return "UNKNOWN";
	}
	return tier_policies[tier].name;
}

static memory_tier_t select_tier_and_budget(uint64_t post_mandatory_bytes,
	uint64_t *safety_reserve, uint64_t *cacheable_budget) {
	int i;

	for (i = MEMORY_TIER_COUNT - 1; i >= 0; --i) {
		uint64_t reserve = (uint64_t)tier_policies[i].safety_reserve_kb * KIB_BYTES;
		uint64_t budget = post_mandatory_bytes > reserve ? post_mandatory_bytes - reserve : 0;
		uint64_t minimum = (uint64_t)tier_policies[i].min_cacheable_mb * MIB_BYTES;
		if (budget >= minimum) {
			*safety_reserve = reserve;
			*cacheable_budget = align_down_block(budget);
			return (memory_tier_t)i;
		}
	}

	*safety_reserve = (uint64_t)tier_policies[MEMORY_TIER_CRITICAL].safety_reserve_kb * KIB_BYTES;
	*cacheable_budget = 0;
	return MEMORY_TIER_CRITICAL;
}

static void region_init(region_target_t *region, uint64_t source_bytes,
	const cache_region_policy_t *policy, uint64_t contiguous_limit) {
	uint64_t policy_cap;

	memset(region, 0, sizeof(*region));
	region->source_bytes = source_bytes;
	region->source_ceiling = align_up_block(source_bytes);
	policy_cap = policy->cap_kb == 0 ? region->source_ceiling : (uint64_t)policy->cap_kb * KIB_BYTES;
	region->contiguous_ceiling = min_u64(region->source_ceiling, contiguous_limit);
	region->policy_ceiling = min_u64(region->contiguous_ceiling, align_down_block(policy_cap));
	region->weight = policy->weight;
}

static bool apply_floor(region_target_t *region, const cache_region_policy_t *policy,
	uint64_t *remaining) {
	uint64_t floor;

	if (region->source_ceiling == 0) {
		return true;
	}

	floor = min_u64((uint64_t)policy->floor_kb * KIB_BYTES, region->source_ceiling);
	floor = align_up_block(floor);
	if (floor > region->policy_ceiling || floor > *remaining) {
		return false;
	}

	region->target = floor;
	*remaining -= floor;
	return true;
}

static bool region_can_grow(const region_target_t *region) {
	return region->weight != 0 && region->target < region->policy_ceiling;
}

static void give_blocks(region_target_t *region, uint32_t blocks, uint64_t *remaining) {
	while (blocks-- != 0 && *remaining >= MEMORY_PLAN_BLOCK_SIZE && region_can_grow(region)) {
		region->target += MEMORY_PLAN_BLOCK_SIZE;
		*remaining -= MEMORY_PLAN_BLOCK_SIZE;
	}
}

static void distribute_weighted(region_target_t *gfx, region_target_t *pcm, uint64_t *remaining) {
	while (*remaining >= MEMORY_PLAN_BLOCK_SIZE && (region_can_grow(gfx) || region_can_grow(pcm))) {
		uint64_t before = *remaining;
		give_blocks(gfx, gfx->weight, remaining);
		give_blocks(pcm, pcm->weight, remaining);
		if (*remaining == before) {
			break;
		}
	}
}

static void spill_remaining(region_target_t *primary, region_target_t *secondary,
	uint64_t *remaining) {
	/* Tier caps guide the weighted working-set split. They must not strand safe
	 * memory once the primary GFX/C-ROM region has absorbed everything it can.
	 * The spill pass therefore grows to the real source/contiguous ceiling. */
	while (*remaining >= MEMORY_PLAN_BLOCK_SIZE && primary->target < primary->contiguous_ceiling) {
		primary->target += MEMORY_PLAN_BLOCK_SIZE;
		*remaining -= MEMORY_PLAN_BLOCK_SIZE;
	}
	while (*remaining >= MEMORY_PLAN_BLOCK_SIZE && secondary->target < secondary->contiguous_ceiling) {
		secondary->target += MEMORY_PLAN_BLOCK_SIZE;
		*remaining -= MEMORY_PLAN_BLOCK_SIZE;
	}
}

bool memory_plan_build(const platform_memory_info_t *memory,
	const game_memory_requirements_t *game, memory_plan_t *out) {
	const memory_tier_policy_t *policy;
	const cache_region_policy_t *gfx_policy;
	const cache_region_policy_t *pcm_policy;
	region_target_t gfx;
	region_target_t pcm;
	uint64_t effective_free;
	uint64_t post_mandatory;
	uint64_t contiguous_limit;
	uint64_t remaining;
	memory_tier_t tier;

	if (memory == NULL || game == NULL || out == NULL ||
		(game->core != MEMORY_PLAN_CORE_CPS2 && game->core != MEMORY_PLAN_CORE_MVS)) {
		return false;
	}

	memset(out, 0, sizeof(*out));
	effective_free = platform_memory_info_effective_budget(memory);
	out->measured_free_bytes = effective_free;
	out->largest_free_block_bytes = memory->largest_free_block_bytes;

	if (effective_free <= game->mandatory_late_allocations_bytes) {
		return false;
	}
	post_mandatory = effective_free - game->mandatory_late_allocations_bytes;
	tier = select_tier_and_budget(post_mandatory, &out->safety_reserve_bytes,
		&out->cacheable_budget_bytes);
	out->tier = tier;
	policy = &tier_policies[tier];
	remaining = out->cacheable_budget_bytes;
	if (game->core == MEMORY_PLAN_CORE_CPS2) {
		gfx_policy = &policy->cps2_gfx;
		pcm_policy = NULL;
	} else {
		gfx_policy = &policy->mvs_crom;
		pcm_policy = &policy->mvs_pcm;
	}

	contiguous_limit = memory->largest_free_block_bytes != 0 ?
		align_down_block(memory->largest_free_block_bytes) : out->cacheable_budget_bytes;
	contiguous_limit = min_u64(contiguous_limit, out->cacheable_budget_bytes);

	region_init(&gfx, game->gfx_or_crom_bytes, gfx_policy, contiguous_limit);
	if (game->core == MEMORY_PLAN_CORE_MVS) {
		region_init(&pcm, game->pcm_or_vrom_bytes, pcm_policy, contiguous_limit);
	} else {
		memset(&pcm, 0, sizeof(pcm));
	}

	if (!apply_floor(&gfx, gfx_policy, &remaining) ||
		(game->core == MEMORY_PLAN_CORE_MVS && !apply_floor(&pcm, pcm_policy, &remaining))) {
		return false;
	}

	distribute_weighted(&gfx, &pcm, &remaining);
	spill_remaining(&gfx, &pcm, &remaining);

	out->gfx_cache_bytes = gfx.target;
	out->pcm_cache_bytes = pcm.target;
	out->unused_cacheable_bytes = remaining;
	out->gfx_fully_resident = gfx.source_bytes == 0 || gfx.target >= gfx.source_bytes;
	out->pcm_fully_resident = game->core != MEMORY_PLAN_CORE_MVS ||
		pcm.source_bytes == 0 || pcm.target >= pcm.source_bytes;
	return true;
}

static bool parse_probe_override_mb(const char *name, uint64_t *out_bytes) {
	const char *value = getenv(name);
	char *end = NULL;
	unsigned long long mb;

	if (value == NULL || *value == '\0' || out_bytes == NULL) {
		return false;
	}

	errno = 0;
	mb = strtoull(value, &end, 10);
	if (errno == ERANGE || end == value || *end != '\0' ||
		mb > (unsigned long long)(UINT64_MAX / MIB_BYTES)) {
		printf("[memory_probe] warning: invalid %s; expected integer MiB\n", name);
		return false;
	}

	*out_bytes = (uint64_t)mb * MIB_BYTES;
	return true;
}

void memory_probe_constraints_default(memory_probe_constraints_t *constraints) {
	uint64_t override_bytes;

	if (constraints == NULL) {
		return;
	}

	constraints->total_cap_bytes = MEMORY_PLAN_PROBE_MAX_BYTES;
	constraints->single_block_cap_bytes = 0;
	if (parse_probe_override_mb("NJEMU_MEMORY_BUDGET_MB", &override_bytes)) {
		constraints->total_cap_bytes = min_u64(override_bytes, MEMORY_PLAN_PROBE_MAX_BYTES);
	}
	if (parse_probe_override_mb("NJEMU_MEMORY_LARGEST_BLOCK_MB", &override_bytes)) {
		constraints->single_block_cap_bytes = min_u64(override_bytes, MEMORY_PLAN_PROBE_MAX_BYTES);
	}
}

static uint64_t shape_total_cap(const memory_probe_constraints_t *constraints) {
	uint64_t cap = MEMORY_PLAN_PROBE_MAX_BYTES;

	if (constraints != NULL && constraints->total_cap_bytes != 0) {
		cap = min_u64(cap, constraints->total_cap_bytes);
	}
	return align_down_block(cap);
}

static uint64_t shape_single_cap(const memory_probe_constraints_t *constraints,
	uint64_t total_cap) {
	uint64_t cap = total_cap;

	if (constraints != NULL && constraints->single_block_cap_bytes != 0) {
		cap = min_u64(cap, constraints->single_block_cap_bytes);
	}
	return align_down_block(cap);
}

static bool shape_sizes_fit_constraints(uint64_t gfx_bytes, uint64_t pcm_bytes,
	uint64_t reserve_bytes, uint64_t total_cap, uint64_t single_cap) {
	if (gfx_bytes > single_cap || pcm_bytes > single_cap || reserve_bytes > single_cap) {
		return false;
	}
	if (gfx_bytes > total_cap || pcm_bytes > total_cap - gfx_bytes) {
		return false;
	}
	return reserve_bytes <= total_cap - gfx_bytes - pcm_bytes;
}

static bool probe_shape_once(uint64_t gfx_bytes, uint64_t pcm_bytes,
	uint64_t reserve_bytes, uint64_t total_cap, uint64_t single_cap) {
	void *gfx = NULL;
	void *pcm = NULL;
	void *reserve = NULL;
	bool ok = false;

	if (!shape_sizes_fit_constraints(gfx_bytes, pcm_bytes, reserve_bytes,
		total_cap, single_cap)) {
		return false;
	}

	if (gfx_bytes != 0 && (gfx = malloc((size_t)gfx_bytes)) == NULL) {
		goto done;
	}
	if (pcm_bytes != 0 && (pcm = malloc((size_t)pcm_bytes)) == NULL) {
		goto done;
	}
	if (reserve_bytes != 0 && (reserve = malloc((size_t)reserve_bytes)) == NULL) {
		goto done;
	}
	ok = true;

done:
	free(reserve);
	free(pcm);
	free(gfx);
	return ok;
}

static uint64_t probe_max_primary(uint64_t low_bytes, uint64_t high_bytes,
	uint64_t pcm_floor_bytes, uint64_t reserve_bytes,
	uint64_t total_cap, uint64_t single_cap) {
	uint64_t low_blocks = align_up_block(low_bytes) / MEMORY_PLAN_BLOCK_SIZE;
	uint64_t high_blocks = align_down_block(high_bytes) / MEMORY_PLAN_BLOCK_SIZE;
	uint64_t best_blocks = 0;

	while (low_blocks <= high_blocks) {
		uint64_t mid_blocks = low_blocks + (high_blocks - low_blocks) / 2;
		uint64_t candidate = mid_blocks * MEMORY_PLAN_BLOCK_SIZE;
		if (probe_shape_once(candidate, pcm_floor_bytes, reserve_bytes,
			total_cap, single_cap)) {
			best_blocks = mid_blocks;
			low_blocks = mid_blocks + 1;
		} else {
			if (mid_blocks == 0) {
				break;
			}
			high_blocks = mid_blocks - 1;
		}
	}

	return best_blocks * MEMORY_PLAN_BLOCK_SIZE;
}

static uint64_t probe_max_secondary_preference(uint64_t gfx_floor_bytes,
	uint64_t low_bytes, uint64_t high_bytes, uint64_t reserve_bytes,
	uint64_t total_cap, uint64_t single_cap) {
	uint64_t low_blocks = align_up_block(low_bytes) / MEMORY_PLAN_BLOCK_SIZE;
	uint64_t high_blocks = align_down_block(high_bytes) / MEMORY_PLAN_BLOCK_SIZE;
	uint64_t best_blocks = 0;

	while (low_blocks <= high_blocks) {
		uint64_t mid_blocks = low_blocks + (high_blocks - low_blocks) / 2;
		uint64_t candidate = mid_blocks * MEMORY_PLAN_BLOCK_SIZE;
		if (probe_shape_once(gfx_floor_bytes, candidate, reserve_bytes,
			total_cap, single_cap)) {
			best_blocks = mid_blocks;
			low_blocks = mid_blocks + 1;
		} else {
			if (mid_blocks == 0) {
				break;
			}
			high_blocks = mid_blocks - 1;
		}
	}

	return best_blocks * MEMORY_PLAN_BLOCK_SIZE;
}

static bool probe_secondary_with_primary(void *gfx_memory, uint64_t pcm_bytes,
	uint64_t reserve_bytes, uint64_t total_cap, uint64_t single_cap,
	uint64_t gfx_bytes) {
	void *pcm = NULL;
	void *reserve = NULL;
	bool ok = false;
	(void)gfx_memory;

	if (!shape_sizes_fit_constraints(gfx_bytes, pcm_bytes, reserve_bytes,
		total_cap, single_cap)) {
		return false;
	}
	if (pcm_bytes != 0 && (pcm = malloc((size_t)pcm_bytes)) == NULL) {
		goto done;
	}
	if (reserve_bytes != 0 && (reserve = malloc((size_t)reserve_bytes)) == NULL) {
		goto done;
	}
	ok = true;

done:
	free(reserve);
	free(pcm);
	return ok;
}

static uint64_t probe_max_secondary(void *gfx_memory, uint64_t gfx_bytes,
	uint64_t low_bytes, uint64_t high_bytes, uint64_t reserve_bytes,
	uint64_t total_cap, uint64_t single_cap) {
	uint64_t low_blocks = align_up_block(low_bytes) / MEMORY_PLAN_BLOCK_SIZE;
	uint64_t high_blocks = align_down_block(high_bytes) / MEMORY_PLAN_BLOCK_SIZE;
	uint64_t best_blocks = 0;

	while (low_blocks <= high_blocks) {
		uint64_t mid_blocks = low_blocks + (high_blocks - low_blocks) / 2;
		uint64_t candidate = mid_blocks * MEMORY_PLAN_BLOCK_SIZE;
		if (probe_secondary_with_primary(gfx_memory, candidate, reserve_bytes,
			total_cap, single_cap, gfx_bytes)) {
			best_blocks = mid_blocks;
			low_blocks = mid_blocks + 1;
		} else {
			if (mid_blocks == 0) {
				break;
			}
			high_blocks = mid_blocks - 1;
		}
	}

	return best_blocks * MEMORY_PLAN_BLOCK_SIZE;
}

static uint64_t policy_floor_bytes(uint64_t source_bytes,
	const cache_region_policy_t *policy) {
	uint64_t source_ceiling = align_up_block(source_bytes);
	return min_u64(source_ceiling,
		align_up_block((uint64_t)policy->floor_kb * KIB_BYTES));
}

static bool try_allocate_tier_shape(memory_tier_t tier,
	const game_memory_requirements_t *game, uint64_t total_cap, uint64_t single_cap,
	memory_allocation_shape_t *out) {
	const memory_tier_policy_t *policy = &tier_policies[tier];
	const cache_region_policy_t *gfx_policy = game->core == MEMORY_PLAN_CORE_CPS2 ?
		&policy->cps2_gfx : &policy->mvs_crom;
	const cache_region_policy_t *pcm_policy = &policy->mvs_pcm;
	uint64_t reserve_bytes = align_up_block((uint64_t)policy->safety_reserve_kb * KIB_BYTES);
	uint64_t gfx_ceiling = min_u64(align_up_block(game->gfx_or_crom_bytes), single_cap);
	uint64_t pcm_ceiling = game->core == MEMORY_PLAN_CORE_MVS ?
		min_u64(align_up_block(game->pcm_or_vrom_bytes), single_cap) : 0;
	uint64_t gfx_floor = policy_floor_bytes(game->gfx_or_crom_bytes, gfx_policy);
	uint64_t pcm_floor = game->core == MEMORY_PLAN_CORE_MVS ?
		policy_floor_bytes(game->pcm_or_vrom_bytes, pcm_policy) : 0;
	uint64_t pcm_preferred = pcm_floor;
	uint64_t minimum_cache = (uint64_t)policy->min_cacheable_mb * MIB_BYTES;
	uint64_t primary_high;
	uint64_t gfx_bytes;
	uint64_t pcm_high;
	uint64_t pcm_bytes = 0;
	uint64_t committed;
	void *gfx_memory;
	void *pcm_memory = NULL;
	void *reserve_memory = NULL;

	/* A tier whose nominal minimum cannot fit the test/real capacity cannot win;
	 * reject it before issuing any allocator probes. */
	if (minimum_cache > total_cap || reserve_bytes > total_cap - minimum_cache ||
		gfx_floor == 0 || gfx_floor > gfx_ceiling || reserve_bytes > total_cap ||
		pcm_floor > pcm_ceiling || gfx_floor + pcm_floor > total_cap - reserve_bytes) {
		return false;
	}

	if (game->core == MEMORY_PLAN_CORE_MVS && pcm_ceiling != 0 && pcm_policy->cap_kb != 0) {
		uint64_t preferred_ceiling = min_u64(pcm_ceiling,
			align_down_block((uint64_t)pcm_policy->cap_kb * KIB_BYTES));
		pcm_preferred = probe_max_secondary_preference(gfx_floor, pcm_floor,
			preferred_ceiling, reserve_bytes, total_cap, single_cap);
		if (pcm_preferred < pcm_floor) {
			return false;
		}
	}

	primary_high = min_u64(gfx_ceiling, total_cap - reserve_bytes - pcm_preferred);
	gfx_bytes = probe_max_primary(gfx_floor, primary_high, pcm_preferred,
		reserve_bytes, total_cap, single_cap);
	if (gfx_bytes < gfx_floor) {
		return false;
	}

	gfx_memory = malloc((size_t)gfx_bytes);
	if (gfx_memory == NULL) {
		return false;
	}

	if (game->core == MEMORY_PLAN_CORE_MVS && pcm_ceiling != 0) {
		pcm_high = min_u64(pcm_ceiling, total_cap - reserve_bytes - gfx_bytes);
		pcm_bytes = probe_max_secondary(gfx_memory, gfx_bytes, pcm_preferred,
			pcm_high, reserve_bytes, total_cap, single_cap);
		if (pcm_bytes < pcm_preferred) {
			free(gfx_memory);
			return false;
		}
		if (pcm_bytes != 0) {
			pcm_memory = malloc((size_t)pcm_bytes);
			if (pcm_memory == NULL) {
				free(gfx_memory);
				return false;
			}
		}
	}

	if (reserve_bytes != 0) {
		reserve_memory = malloc((size_t)reserve_bytes);
		if (reserve_memory == NULL) {
			free(pcm_memory);
			free(gfx_memory);
			return false;
		}
	}

	committed = gfx_bytes + pcm_bytes;
	if (committed < minimum_cache) {
		free(reserve_memory);
		free(pcm_memory);
		free(gfx_memory);
		return false;
	}

	memset(out, 0, sizeof(*out));
	out->gfx_memory = gfx_memory;
	out->pcm_memory = pcm_memory;
	out->reserve_memory = reserve_memory;
	out->plan.tier = tier;
	out->plan.measured_free_bytes = committed + reserve_bytes;
	out->plan.largest_free_block_bytes = max_u64(gfx_bytes,
		max_u64(pcm_bytes, reserve_bytes));
	out->plan.safety_reserve_bytes = reserve_bytes;
	out->plan.cacheable_budget_bytes = committed;
	out->plan.gfx_cache_bytes = gfx_bytes;
	out->plan.pcm_cache_bytes = pcm_bytes;
	out->plan.unused_cacheable_bytes = 0;
	out->plan.gfx_fully_resident = gfx_bytes >= game->gfx_or_crom_bytes;
	out->plan.pcm_fully_resident = game->core != MEMORY_PLAN_CORE_MVS ||
		game->pcm_or_vrom_bytes == 0 || pcm_bytes >= game->pcm_or_vrom_bytes;
	out->plan.allocation_probed = true;
	return true;
}

bool memory_plan_allocate_shape(const game_memory_requirements_t *game,
	const memory_probe_constraints_t *constraints, memory_allocation_shape_t *out) {
	uint64_t total_cap;
	uint64_t single_cap;
	int tier;

	if (game == NULL || out == NULL || game->mandatory_late_allocations_bytes != 0 ||
		(game->core != MEMORY_PLAN_CORE_CPS2 && game->core != MEMORY_PLAN_CORE_MVS)) {
		return false;
	}

	memset(out, 0, sizeof(*out));
	total_cap = shape_total_cap(constraints);
	single_cap = shape_single_cap(constraints, total_cap);
	if (total_cap < MEMORY_PLAN_BLOCK_SIZE || single_cap < MEMORY_PLAN_BLOCK_SIZE) {
		return false;
	}

	for (tier = MEMORY_TIER_COUNT - 1; tier >= MEMORY_TIER_CRITICAL; --tier) {
		if (try_allocate_tier_shape((memory_tier_t)tier, game, total_cap,
			single_cap, out)) {
			return true;
		}
	}

	return false;
}

void memory_allocation_shape_release_reserve(memory_allocation_shape_t *shape) {
	if (shape == NULL || shape->reserve_memory == NULL) {
		return;
	}
	free(shape->reserve_memory);
	shape->reserve_memory = NULL;
}

void memory_allocation_shape_release(memory_allocation_shape_t *shape) {
	if (shape == NULL) {
		return;
	}
	free(shape->reserve_memory);
	free(shape->pcm_memory);
	free(shape->gfx_memory);
	memset(shape, 0, sizeof(*shape));
}

void memory_plan_log(const memory_plan_t *plan) {
	if (plan == NULL) {
		return;
	}

	printf(plan->allocation_probed ?
		"[memory_plan] probed=%.1fMiB largest_alloc=%.1fMiB reserve=%.1fMiB tier=%s budget=%.1fMiB gfx=%.1fMiB pcm=%.1fMiB unused=%.1fMiB\n" :
		"[memory_plan] free=%.1fMiB largest=%.1fMiB reserve=%.1fMiB tier=%s budget=%.1fMiB gfx=%.1fMiB pcm=%.1fMiB unused=%.1fMiB\n",
		(double)plan->measured_free_bytes / (double)MIB_BYTES,
		(double)plan->largest_free_block_bytes / (double)MIB_BYTES,
		(double)plan->safety_reserve_bytes / (double)MIB_BYTES,
		memory_plan_tier_name(plan->tier),
		(double)plan->cacheable_budget_bytes / (double)MIB_BYTES,
		(double)plan->gfx_cache_bytes / (double)MIB_BYTES,
		(double)plan->pcm_cache_bytes / (double)MIB_BYTES,
		(double)plan->unused_cacheable_bytes / (double)MIB_BYTES);
}
