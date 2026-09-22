#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common/memory_plan.h"

#define KIB(n) ((uint64_t)(n) * 1024u)
#define MIB(n) ((uint64_t)(n) * 1024u * 1024u)

static uint64_t reserve_for_tier(memory_tier_t tier) {
	switch (tier) {
	case MEMORY_TIER_CRITICAL: return MIB(1);
	case MEMORY_TIER_LOW: return MIB(2);
	case MEMORY_TIER_MEDIUM: return MIB(2);
	case MEMORY_TIER_HIGH: return MIB(2);
	case MEMORY_TIER_VERY_HIGH: return MIB(4);
	default: return 0;
	}
}

static platform_memory_info_t memory_for_cacheable(uint64_t cacheable,
	uint64_t mandatory, uint64_t largest) {
	platform_memory_info_t info;
	memory_tier_t tier = memory_plan_tier_for_cacheable_budget(cacheable);
	memset(&info, 0, sizeof(info));
	info.free_bytes = cacheable + mandatory + reserve_for_tier(tier);
	info.largest_free_block_bytes = largest != 0 ? largest : info.free_bytes;
	info.capabilities = PLATFORM_MEMORY_CAP_QUERY_FREE | PLATFORM_MEMORY_CAP_QUERY_LARGEST_BLOCK;
	return info;
}

static game_memory_requirements_t cps2_requirements(uint64_t gfx, uint64_t mandatory) {
	game_memory_requirements_t game;
	memset(&game, 0, sizeof(game));
	game.core = MEMORY_PLAN_CORE_CPS2;
	game.mandatory_late_allocations_bytes = mandatory;
	game.gfx_or_crom_bytes = gfx;
	return game;
}

static game_memory_requirements_t mvs_requirements(uint64_t crom, uint64_t pcm,
	uint64_t mandatory) {
	game_memory_requirements_t game;
	memset(&game, 0, sizeof(game));
	game.core = MEMORY_PLAN_CORE_MVS;
	game.mandatory_late_allocations_bytes = mandatory;
	game.gfx_or_crom_bytes = crom;
	game.pcm_or_vrom_bytes = pcm;
	return game;
}

static void assert_plan_invariants(const memory_plan_t *plan,
	const game_memory_requirements_t *game) {
	assert(plan->gfx_cache_bytes % MEMORY_PLAN_BLOCK_SIZE == 0);
	assert(plan->pcm_cache_bytes % MEMORY_PLAN_BLOCK_SIZE == 0);
	assert(plan->cacheable_budget_bytes % MEMORY_PLAN_BLOCK_SIZE == 0);
	assert(plan->gfx_cache_bytes + plan->pcm_cache_bytes + plan->unused_cacheable_bytes ==
		plan->cacheable_budget_bytes);
	assert(plan->gfx_cache_bytes + plan->pcm_cache_bytes <= plan->cacheable_budget_bytes);
	assert(game->mandatory_late_allocations_bytes + plan->safety_reserve_bytes +
		plan->cacheable_budget_bytes <= plan->measured_free_bytes);
	if (plan->largest_free_block_bytes != 0) {
		assert(plan->gfx_cache_bytes <= plan->largest_free_block_bytes);
		assert(plan->pcm_cache_bytes <= plan->largest_free_block_bytes);
	}
}

static void test_tier_boundaries(void) {
	const uint64_t block = MEMORY_PLAN_BLOCK_SIZE;
	assert(memory_plan_tier_for_cacheable_budget(MIB(6) - block) == MEMORY_TIER_CRITICAL);
	assert(memory_plan_tier_for_cacheable_budget(MIB(6)) == MEMORY_TIER_LOW);
	assert(memory_plan_tier_for_cacheable_budget(MIB(6) + block) == MEMORY_TIER_LOW);
	assert(memory_plan_tier_for_cacheable_budget(MIB(12) - block) == MEMORY_TIER_LOW);
	assert(memory_plan_tier_for_cacheable_budget(MIB(12)) == MEMORY_TIER_MEDIUM);
	assert(memory_plan_tier_for_cacheable_budget(MIB(12) + block) == MEMORY_TIER_MEDIUM);
	assert(memory_plan_tier_for_cacheable_budget(MIB(24) - block) == MEMORY_TIER_MEDIUM);
	assert(memory_plan_tier_for_cacheable_budget(MIB(24)) == MEMORY_TIER_HIGH);
	assert(memory_plan_tier_for_cacheable_budget(MIB(24) + block) == MEMORY_TIER_HIGH);
	assert(memory_plan_tier_for_cacheable_budget(MIB(40) - block) == MEMORY_TIER_HIGH);
	assert(memory_plan_tier_for_cacheable_budget(MIB(40)) == MEMORY_TIER_VERY_HIGH);
	assert(memory_plan_tier_for_cacheable_budget(MIB(40) + block) == MEMORY_TIER_VERY_HIGH);
}

static void test_cps2_budget_matrix(void) {
	const uint32_t budgets_mb[] = { 4, 6, 8, 12, 16, 24, 32, 40, 48, 64 };
	size_t i;
	for (i = 0; i < sizeof(budgets_mb) / sizeof(budgets_mb[0]); ++i) {
		uint64_t budget = MIB(budgets_mb[i]);
		platform_memory_info_t memory = memory_for_cacheable(budget, 0, 0);
		game_memory_requirements_t game = cps2_requirements(MIB(128), 0);
		memory_plan_t plan;
		assert(memory_plan_build(&memory, &game, &plan));
		assert(plan.tier == memory_plan_tier_for_cacheable_budget(budget));
		assert(plan.cacheable_budget_bytes == budget);
		assert(plan.gfx_cache_bytes == budget);
		assert(plan.pcm_cache_bytes == 0);
		assert(plan.unused_cacheable_bytes == 0);
		assert(!plan.gfx_fully_resident);
		assert(plan.pcm_fully_resident);
		assert_plan_invariants(&plan, &game);
	}
}

static void test_mandatory_and_reserve_accounting(void) {
	uint64_t budget = MIB(16);
	uint64_t mandatory = MIB(7);
	platform_memory_info_t memory = memory_for_cacheable(budget, mandatory, 0);
	game_memory_requirements_t game = cps2_requirements(MIB(64), mandatory);
	memory_plan_t plan;
	assert(memory_plan_build(&memory, &game, &plan));
	assert(plan.tier == MEMORY_TIER_MEDIUM);
	assert(plan.safety_reserve_bytes == MIB(2));
	assert(plan.cacheable_budget_bytes == budget);
	assert_plan_invariants(&plan, &game);
}

static void test_cps2_full_resident_and_fragmentation(void) {
	platform_memory_info_t memory = memory_for_cacheable(MIB(12), 0, 0);
	game_memory_requirements_t game = cps2_requirements(MIB(10), 0);
	memory_plan_t plan;
	assert(memory_plan_build(&memory, &game, &plan));
	assert(plan.gfx_cache_bytes == MIB(10));
	assert(plan.unused_cacheable_bytes == MIB(2));
	assert(plan.gfx_fully_resident);
	assert_plan_invariants(&plan, &game);

	memory = memory_for_cacheable(MIB(40), 0, MIB(8));
	game = cps2_requirements(MIB(128), 0);
	assert(memory_plan_build(&memory, &game, &plan));
	assert(plan.tier == MEMORY_TIER_VERY_HIGH);
	assert(plan.gfx_cache_bytes == MIB(8));
	assert(plan.unused_cacheable_bytes == MIB(32));
	assert(!plan.gfx_fully_resident);
	assert_plan_invariants(&plan, &game);

	memory = memory_for_cacheable(MIB(40), 0, MIB(7));
	assert(!memory_plan_build(&memory, &game, &plan));
}

static void test_cps2_region_edges_and_floor(void) {
	const uint64_t budget = MIB(12);
	const uint64_t block = MEMORY_PLAN_BLOCK_SIZE;
	const uint64_t sources[] = { budget - block, budget, budget + block };
	size_t i;
	for (i = 0; i < sizeof(sources) / sizeof(sources[0]); ++i) {
		platform_memory_info_t memory = memory_for_cacheable(budget, 0, 0);
		game_memory_requirements_t game = cps2_requirements(sources[i], 0);
		memory_plan_t plan;
		assert(memory_plan_build(&memory, &game, &plan));
		assert(plan.gfx_cache_bytes == (sources[i] < budget ? sources[i] : budget));
		assert(plan.gfx_fully_resident == (sources[i] <= budget));
		assert_plan_invariants(&plan, &game);
	}

	{
		platform_memory_info_t memory = memory_for_cacheable(MIB(2), 0, 0);
		game_memory_requirements_t game = cps2_requirements(MIB(64), 0);
		memory_plan_t plan;
		assert(memory_plan_build(&memory, &game, &plan));
		assert(plan.tier == MEMORY_TIER_CRITICAL);
		assert(plan.gfx_cache_bytes == MIB(2));
		assert_plan_invariants(&plan, &game);

		memory = memory_for_cacheable(MIB(1), 0, 0);
		assert(!memory_plan_build(&memory, &game, &plan));
	}
}

static void test_mvs_distribution(void) {
	struct test_case {
		uint32_t budget_mb;
		memory_tier_t tier;
		uint32_t crom_kb;
		uint32_t pcm_kb;
	} cases[] = {
		{ 10, MEMORY_TIER_LOW, 7936, 2304 },
		{ 16, MEMORY_TIER_MEDIUM, 13312, 3072 },
		{ 32, MEMORY_TIER_HIGH, 29696, 3072 },
		{ 48, MEMORY_TIER_VERY_HIGH, 40448, 8704 },
	};
	size_t i;
	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
		platform_memory_info_t memory = memory_for_cacheable(MIB(cases[i].budget_mb), 0, 0);
		game_memory_requirements_t game = mvs_requirements(MIB(128), MIB(128), 0);
		memory_plan_t plan;
		assert(memory_plan_build(&memory, &game, &plan));
		assert(plan.tier == cases[i].tier);
		assert(plan.gfx_cache_bytes == KIB(cases[i].crom_kb));
		assert(plan.pcm_cache_bytes == KIB(cases[i].pcm_kb));
		assert(plan.unused_cacheable_bytes == 0);
		assert_plan_invariants(&plan, &game);
	}
}

static void test_mvs_caps_and_spill(void) {
	platform_memory_info_t memory = memory_for_cacheable(MIB(16), 0, 0);
	game_memory_requirements_t game = mvs_requirements(MIB(128), MIB(2), 0);
	memory_plan_t plan;
	assert(memory_plan_build(&memory, &game, &plan));
	assert(plan.gfx_cache_bytes == MIB(14));
	assert(plan.pcm_cache_bytes == MIB(2));
	assert(plan.pcm_fully_resident);
	assert(plan.unused_cacheable_bytes == 0);
	assert_plan_invariants(&plan, &game);

	/* Once C-ROM is fully resident, the final spill may exceed the normal
	 * 3 MiB PCM working-set cap rather than leaving useful RAM idle. */
	memory = memory_for_cacheable(MIB(16), 0, 0);
	game = mvs_requirements(MIB(10), MIB(128), 0);
	assert(memory_plan_build(&memory, &game, &plan));
	assert(plan.gfx_cache_bytes == MIB(10));
	assert(plan.pcm_cache_bytes == MIB(6));
	assert(plan.unused_cacheable_bytes == 0);
	assert_plan_invariants(&plan, &game);

	memory = memory_for_cacheable(MIB(16), 0, 0);
	game = mvs_requirements(MIB(5), MIB(2), 0);
	assert(memory_plan_build(&memory, &game, &plan));
	assert(plan.gfx_fully_resident);
	assert(plan.pcm_fully_resident);
	assert(plan.gfx_cache_bytes == MIB(5));
	assert(plan.pcm_cache_bytes == MIB(2));
	assert(plan.unused_cacheable_bytes == MIB(9));
	assert_plan_invariants(&plan, &game);
}

static void test_mvs_fragmentation_limit(void) {
	platform_memory_info_t memory = memory_for_cacheable(MIB(48), 0, MIB(12));
	game_memory_requirements_t game = mvs_requirements(MIB(128), MIB(128), 0);
	memory_plan_t plan;
	assert(memory_plan_build(&memory, &game, &plan));
	assert(plan.gfx_cache_bytes == MIB(12));
	assert(plan.pcm_cache_bytes == MIB(12));
	assert(plan.unused_cacheable_bytes == MIB(24));
	assert_plan_invariants(&plan, &game);
}

static void test_block_alignment_and_determinism(void) {
	platform_memory_info_t memory = memory_for_cacheable(MIB(12) + KIB(32), 0, 0);
	game_memory_requirements_t game = mvs_requirements(MIB(20) + 1, MIB(4) + 1, 0);
	memory_plan_t a;
	memory_plan_t b;
	assert(memory_plan_build(&memory, &game, &a));
	assert(memory_plan_build(&memory, &game, &b));
	assert(memcmp(&a, &b, sizeof(a)) == 0);
	assert(a.cacheable_budget_bytes == MIB(12));
	assert_plan_invariants(&a, &game);
}

static void test_r9_forced_budget_matrix(void) {
	const uint32_t budgets_mb[] = { 12, 16, 20, 24, 32, 48, 64, 96, 128, 256 };
	size_t i;

	for (i = 0; i < sizeof(budgets_mb) / sizeof(budgets_mb[0]); ++i) {
		platform_memory_info_t memory;
		game_memory_requirements_t cps2 = cps2_requirements(MIB(128), 0);
		game_memory_requirements_t mvs = mvs_requirements(MIB(128), MIB(32), 0);
		memory_plan_t cps2_a, cps2_b, mvs_a, mvs_b;

		memset(&memory, 0, sizeof(memory));
		memory.free_bytes = MIB(budgets_mb[i]);
		memory.largest_free_block_bytes = memory.free_bytes;
		memory.capabilities = PLATFORM_MEMORY_CAP_QUERY_FREE |
			PLATFORM_MEMORY_CAP_QUERY_LARGEST_BLOCK;

		assert(memory_plan_build(&memory, &cps2, &cps2_a));
		assert(memory_plan_build(&memory, &cps2, &cps2_b));
		assert(memcmp(&cps2_a, &cps2_b, sizeof(cps2_a)) == 0);
		assert(cps2_a.measured_free_bytes == MIB(budgets_mb[i]));
		assert_plan_invariants(&cps2_a, &cps2);

		assert(memory_plan_build(&memory, &mvs, &mvs_a));
		assert(memory_plan_build(&memory, &mvs, &mvs_b));
		assert(memcmp(&mvs_a, &mvs_b, sizeof(mvs_a)) == 0);
		assert(mvs_a.measured_free_bytes == MIB(budgets_mb[i]));
		assert_plan_invariants(&mvs_a, &mvs);
	}
}

int main(void) {
	test_tier_boundaries();
	test_cps2_budget_matrix();
	test_mandatory_and_reserve_accounting();
	test_cps2_full_resident_and_fragmentation();
	test_cps2_region_edges_and_floor();
	test_mvs_distribution();
	test_mvs_caps_and_spill();
	test_mvs_fragmentation_limit();
	test_block_alignment_and_determinism();
	test_r9_forced_budget_matrix();
	puts("memory_plan_tests: OK");
	return 0;
}
