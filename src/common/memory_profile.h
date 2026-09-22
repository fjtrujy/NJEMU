/******************************************************************************

	memory_profile.h

	Runtime memory tier selection.

		Legacy startup tier label retained temporarily for compatibility and debug
		overrides. Runtime allocation policy is owned by memory_plan_t; this profile
		no longer controls cache sizing, preloads or PSP2K ownership.

******************************************************************************/

#ifndef MEMORY_PROFILE_H
#define MEMORY_PROFILE_H

#include <stdint.h>

typedef enum {
	MEMORY_PROFILE_TIER_TINY = 0,
	MEMORY_PROFILE_TIER_SMALL,
	MEMORY_PROFILE_TIER_MEDIUM,
	MEMORY_PROFILE_TIER_LARGE,
	MEMORY_PROFILE_TIER_COUNT
} memory_profile_tier_t;

typedef struct {
	const char *name;              /* "tiny", "small", "medium", "large" */
	uint32_t   min_ram_mb;         /* selection threshold (lower bound) */

} memory_profile_t;

/* Select a memory profile from the given available-RAM size (in bytes).
 * Honors the NJEMU_MEM_TIER environment variable as an override
 * ("tiny" | "small" | "medium" | "large"). Logs the selection.
 * Stores the selected profile internally; subsequent calls return the same
 * pointer.
 */
const memory_profile_t *memory_profile_select(uint32_t available_ram_bytes);

/* Returns the profile selected by the most recent memory_profile_select()
 * call, or NULL if none has been made yet.
 */
const memory_profile_t *memory_profile_current(void);

#endif /* MEMORY_PROFILE_H */
