/******************************************************************************

	platform_memory_info.c

******************************************************************************/

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "platform_memory_info.h"

#define MIB_BYTES (1024ull * 1024ull)

static bool parse_mb(const char *value, uint64_t *out_bytes) {
	char *end = NULL;
	unsigned long long mb;

	if (value == NULL || *value == '\0' || out_bytes == NULL) {
		return false;
	}

	errno = 0;
	mb = strtoull(value, &end, 10);
	if (errno != 0 || end == value || *end != '\0' ||
		mb > (unsigned long long)(UINT64_MAX / MIB_BYTES)) {
		return false;
	}

	*out_bytes = (uint64_t)mb * MIB_BYTES;
	return true;
}

void platform_memory_info_normalize(platform_memory_info_t *info) {
	if (info == NULL) {
		return;
	}

	if (info->physical_total_bytes != 0 && info->budget_cap_bytes > info->physical_total_bytes) {
		info->budget_cap_bytes = info->physical_total_bytes;
	}

	if (info->free_bytes != 0 && info->largest_free_block_bytes > info->free_bytes) {
		info->largest_free_block_bytes = info->free_bytes;
	}
}

bool platform_memory_info_apply_override_values(platform_memory_info_t *info,
	const char *budget_mb, const char *largest_block_mb) {
	uint64_t bytes;
	bool valid = true;

	if (info == NULL) {
		return false;
	}

	if (budget_mb != NULL) {
		if (parse_mb(budget_mb, &bytes)) {
			info->budget_cap_bytes = bytes;
		} else {
			valid = false;
		}
	}

	if (largest_block_mb != NULL) {
		if (parse_mb(largest_block_mb, &bytes)) {
			info->largest_free_block_bytes = bytes;
			info->capabilities |= PLATFORM_MEMORY_CAP_QUERY_LARGEST_BLOCK;
		} else {
			valid = false;
		}
	}

	platform_memory_info_normalize(info);
	return valid;
}

void platform_memory_info_apply_env_overrides(platform_memory_info_t *info) {
	const char *budget = getenv("NJEMU_MEMORY_BUDGET_MB");
	const char *largest = getenv("NJEMU_MEMORY_LARGEST_BLOCK_MB");

	if (!platform_memory_info_apply_override_values(info, budget, largest)) {
		printf("[memory] warning: invalid NJEMU memory override; expected integer MiB values\n");
	}
}

uint64_t platform_memory_info_effective_budget(const platform_memory_info_t *info) {
	uint64_t result;

	if (info == NULL) {
		return 0;
	}

	result = info->free_bytes;
	if (result != 0 && info->budget_cap_bytes != 0 && info->budget_cap_bytes < result) {
		result = info->budget_cap_bytes;
	}
	return result;
}

void platform_memory_info_log(const platform_memory_info_t *info) {
	if (info == NULL) {
		return;
	}

	printf("[memory] physical=%.1fMiB cap=%.1fMiB free=%.1fMiB largest=%.1fMiB caps=0x%08x reliability=0x%08x\n",
		(double)info->physical_total_bytes / (double)MIB_BYTES,
		(double)info->budget_cap_bytes / (double)MIB_BYTES,
		(double)info->free_bytes / (double)MIB_BYTES,
		(double)info->largest_free_block_bytes / (double)MIB_BYTES,
		(unsigned)info->capabilities,
		(unsigned)info->reliability_flags);
}
