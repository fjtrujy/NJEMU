/******************************************************************************

	memory_profile.h

	Runtime memory tier selection.

	Picks one of several "memory profiles" at startup based on the amount of
	RAM the platform reports. The profile drives feature toggles (which
	preloads to enable) and sizing parameters (safety threshold, cache floor)
	used later by cache_start() and per-target preload paths.

	Sizing principle: preloads first, cache absorbs the remainder.
	    cache_size = available_ram
	               - emulator_baseline
	               - game_required_ram
	               - sum(enabled preload costs)
	               - safety_threshold

	The profile is selected once at platform init. The actual cache budget is
	computed later, when the loaded game's mandatory ROM sizes are known.

******************************************************************************/

#ifndef MEMORY_PROFILE_H
#define MEMORY_PROFILE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	MEMORY_TIER_TINY = 0,
	MEMORY_TIER_SMALL,
	MEMORY_TIER_MEDIUM,
	MEMORY_TIER_LARGE,
	MEMORY_TIER_COUNT
} memory_tier_t;

typedef struct {
	const char *name;              /* "tiny", "small", "medium", "large" */
	uint32_t   min_ram_mb;         /* selection threshold (lower bound) */

	/* Feature toggles -- which preloads are eligible at this tier. The
	 * target may still skip a preload if it doesn't fit for the loaded game.
	 */
	bool       preload_sound;      /* MVS ADPCM, CPS2 QSound */
	bool       preload_crypto;     /* MVS neocrypt buffers */
	bool       preload_gfx;        /* CPS2 full GFX preload */
	bool       use_psp2k_region;   /* PSP Slim 32 MB kernel region */

	/* Cache-size bounds in MB. These are interim Phase 2a knobs that
	 * preserve the old compile-time MIN_CACHE_SIZE / MAX_CACHE_SIZE
	 * behaviour. Phase 2b will replace them with a budget formula
	 * (available_ram - baseline - game_required - preload_costs - safety).
	 */
	uint32_t   cache_min_mb;       /* lower bound for the malloc-probe */
	uint32_t   cache_max_mb;       /* upper bound for the malloc-probe */

	/* Sizing knobs -- reserved for Phase 2b. */
	uint32_t   safety_threshold_mb;/* hold-back for OS / fragmentation */
	uint32_t   cache_floor_mb;     /* minimum cache; abort if unreachable */
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
