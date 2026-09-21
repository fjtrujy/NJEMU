/******************************************************************************

	memory_profile.c

******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory_profile.h"

static const memory_profile_t profile_table[MEMORY_PROFILE_TIER_COUNT] = {
	[MEMORY_PROFILE_TIER_TINY] = {
		.name                 = "tiny",
		.min_ram_mb           = 0,
		.preload_sound        = false,
		.preload_crypto       = false,
		.preload_gfx          = false,
		.use_psp2k_region     = false,
		.cache_min_mb         = 2,
		.cache_max_mb         = 8,
		.safety_threshold_mb  = 1,
		.cache_floor_mb       = 2,
	},
	[MEMORY_PROFILE_TIER_SMALL] = {
		.name                 = "small",
		.min_ram_mb           = 16,  /* PSP base ~20 MB free lands here */
		.preload_sound        = false,
		.preload_crypto       = false,
		.preload_gfx          = false,
		.use_psp2k_region     = false,
		.cache_min_mb         = 2,   /* matches old !LARGE_MEMORY MIN_CACHE_SIZE (0x20) */
		.cache_max_mb         = 20,  /* matches old !LARGE_MEMORY MAX_CACHE_SIZE (0x140) */
		.safety_threshold_mb  = 2,
		.cache_floor_mb       = 4,
	},
	[MEMORY_PROFILE_TIER_MEDIUM] = {
		.name                 = "medium",
		.min_ram_mb           = 48,
		.preload_sound        = true,
		.preload_crypto       = false,
		.preload_gfx          = false,
		.use_psp2k_region     = true,
		.cache_min_mb         = 4,
		.cache_max_mb         = 24,
		.safety_threshold_mb  = 2,
		.cache_floor_mb       = 8,
	},
	[MEMORY_PROFILE_TIER_LARGE] = {
		.name                 = "large",
		.min_ram_mb           = 96,
		.preload_sound        = true,
		.preload_crypto       = true,
		.preload_gfx          = true,
		.use_psp2k_region     = true,
		.cache_min_mb         = 4,   /* matches old LARGE_MEMORY MIN_CACHE_SIZE (0x40) */
		.cache_max_mb         = 32,  /* matches old LARGE_MEMORY MAX_CACHE_SIZE (0x200) */
		.safety_threshold_mb  = 4,
		.cache_floor_mb       = 8,
	},
};

static const memory_profile_t *current_profile = NULL;

static const memory_profile_t *parse_override(const char *value) {
	if (value == NULL || *value == '\0') {
		return NULL;
	}
	for (int i = 0; i < MEMORY_PROFILE_TIER_COUNT; i++) {
		if (strcmp(value, profile_table[i].name) == 0) {
			return &profile_table[i];
		}
	}
	return NULL;
}

static const memory_profile_t *select_for_ram(uint32_t available_mb) {
	const memory_profile_t *chosen = &profile_table[MEMORY_PROFILE_TIER_TINY];
	for (int i = 0; i < MEMORY_PROFILE_TIER_COUNT; i++) {
		if (available_mb >= profile_table[i].min_ram_mb) {
			chosen = &profile_table[i];
		}
	}
	return chosen;
}

const memory_profile_t *memory_profile_select(uint32_t available_ram_bytes) {
	uint32_t available_mb = available_ram_bytes / (1024u * 1024u);
	const memory_profile_t *chosen = NULL;
	const char *source = "auto";

	const char *override_env = getenv("NJEMU_MEM_TIER");
	if (override_env != NULL) {
		chosen = parse_override(override_env);
		if (chosen != NULL) {
			source = "env";
		} else {
			printf("[memory_profile] warning: NJEMU_MEM_TIER='%s' is not a valid tier; using auto-detect\n",
			       override_env);
		}
	}

#ifdef LARGE_MEMORY
	/* Legacy compile-time LARGE_MEMORY (Makefile builds only -- the CMake
	 * option was never propagated). Force the large tier so Makefile builds
	 * keep their PSP Slim preload + 32 MB cache behaviour. */
	if (chosen == NULL) {
		chosen = &profile_table[MEMORY_PROFILE_TIER_LARGE];
		source = "LARGE_MEMORY";
	}
#endif

	if (chosen == NULL) {
		chosen = select_for_ram(available_mb);
	}

	current_profile = chosen;

	printf("[memory_profile] available RAM: %u MB (%u bytes), tier: %s (%s)\n",
	       available_mb, available_ram_bytes, chosen->name, source);

	return chosen;
}

const memory_profile_t *memory_profile_current(void) {
	return current_profile;
}
