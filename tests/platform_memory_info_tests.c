#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "common/platform_memory_info.h"

#define MIB(n) ((uint64_t)(n) * 1024u * 1024u)

static void test_normalize(void) {
	platform_memory_info_t info = {
		.physical_total_bytes = MIB(32),
		.budget_cap_bytes = MIB(64),
		.free_bytes = MIB(20),
		.largest_free_block_bytes = MIB(24),
	};

	platform_memory_info_normalize(&info);
	assert(info.budget_cap_bytes == MIB(32));
	assert(info.largest_free_block_bytes == MIB(20));
}

static void test_effective_budget(void) {
	platform_memory_info_t info = {
		.free_bytes = MIB(40),
		.budget_cap_bytes = MIB(24),
	};

	assert(platform_memory_info_effective_budget(&info) == MIB(24));
	info.budget_cap_bytes = 0;
	assert(platform_memory_info_effective_budget(&info) == MIB(40));
	info.free_bytes = 0;
	info.budget_cap_bytes = MIB(24);
	assert(platform_memory_info_effective_budget(&info) == 0);
}

static void test_overrides(void) {
	platform_memory_info_t info = {
		.physical_total_bytes = MIB(64),
		.free_bytes = MIB(48),
		.largest_free_block_bytes = MIB(40),
	};

	assert(platform_memory_info_apply_override_values(&info, "32", "12"));
	assert(info.budget_cap_bytes == MIB(32));
	assert(info.largest_free_block_bytes == MIB(12));
	assert((info.capabilities & PLATFORM_MEMORY_CAP_QUERY_LARGEST_BLOCK) != 0);

	assert(!platform_memory_info_apply_override_values(&info, "nope", NULL));
	assert(info.budget_cap_bytes == MIB(32));
}

int main(void) {
	test_normalize();
	test_effective_budget();
	test_overrides();
	puts("platform_memory_info_tests: OK");
	return 0;
}
