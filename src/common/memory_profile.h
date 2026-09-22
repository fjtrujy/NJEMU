/******************************************************************************

	memory_profile.h

	Runtime memory tier selection.

		Legacy startup profile retained temporarily for preload/PSP2K feature
		toggles. Cache sizing is no longer owned here: memory_plan_t computes the
		game-specific runtime budget after mandatory allocations are known.

******************************************************************************/

#ifndef MEMORY_PROFILE_H
#define MEMORY_PROFILE_H

#include <stdint.h>
#include <stdbool.h>

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

	/* Feature toggles -- which preloads are eligible at this tier. The
	 * target may still skip a preload if it doesn't fit for the loaded game.
	 */
	bool       preload_sound;      /* MVS ADPCM, CPS2 QSound */
	bool       preload_crypto;     /* MVS neocrypt buffers */
	bool       use_psp2k_region;   /* PSP Slim 32 MB kernel region */

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
