/* Per-game widescreen profile lifecycle. The renderer only consumes the
 * resulting view geometry (mvs/wide.c). */
#include "mvs.h"
#include "mvs/wide.h"
#include "mvs/wide_debug.h"
#if defined(DESKTOP)
#include "common/runtime_paths.h"
#include "mvs/wide_profile.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool mvs_wide_supported;
static bool mvs_wide_warning_emitted;
static mvs_wide_profile_t *mvs_wide_profile;
static char mvs_wide_error[192];

static int mvs_true_wide_forced(void)
{
	static int forced = -1;

	if (forced < 0)
	{
		const char *value = getenv("NJEMU_MVS_TRUE_WIDE");
		forced = value != NULL && value[0] != '\0' && value[0] != '0';
	}
	return forced;
}
#endif

void mvs_wide_exit(void)
{
#if defined(DESKTOP)
	free(mvs_wide_profile);
	mvs_wide_profile = NULL;
	mvs_wide_supported = false;
#endif
	mvs_wide_set_active(false);
}

void mvs_wide_init(void)
{
	mvs_wide_exit();
#if defined(DESKTOP)
	char path[PATH_MAX];
	const char *directory = getenv("NJEMU_MVS_PROFILE_DIR");
	const char *experimental = getenv("NJEMU_MVS_ALLOW_EXPERIMENTAL");
	int length, program_mode;
	mvs_wide_warning_emitted = false;
#if !RELEASE
	mvs_wide_debug_init();
#endif
	if (strspn(game_name, "abcdefghijklmnopqrstuvwxyz0123456789_-") != strlen(game_name))
	{
		snprintf(mvs_wide_error, sizeof(mvs_wide_error), "invalid game identifier");
		return;
	}
	length = directory && *directory ? snprintf(path, sizeof(path), "%s/%s.ini", directory, game_name) :
		snprintf(path, sizeof(path), "%swidescreen/%s.ini", launchDir, game_name);
	if (length < 0 || (size_t)length >= sizeof(path))
	{
		snprintf(mvs_wide_error, sizeof(mvs_wide_error), "profile path too long");
		return;
	}
	mvs_wide_profile = mvs_wide_profile_load(path, mvs_wide_error, sizeof(mvs_wide_error));
	if (!mvs_wide_profile) return;
	if (strcmp(mvs_wide_profile->game, game_name) || mvs_wide_profile->ngh != neogeo_ngh)
	{
		snprintf(mvs_wide_error, sizeof(mvs_wide_error), "profile belongs to a different game/NGH");
		return;
	}
	if (mvs_wide_profile->status == MVS_WIDE_PROFILE_DRAFT ||
		(mvs_wide_profile->status == MVS_WIDE_PROFILE_EXPERIMENTAL &&
		(!experimental || strcmp(experimental, "1"))))
	{
		snprintf(mvs_wide_error, sizeof(mvs_wide_error), "draft or experimental profile not authorized");
		return;
	}
	program_mode = mvs_wide_profile_check(mvs_wide_profile,
		(const uint16_t *)memory_region_cpu1, memory_length_cpu1,
		mvs_wide_error, sizeof(mvs_wide_error));
	mvs_wide_supported = program_mode == 0;
	if (program_mode == 1)
		snprintf(mvs_wide_error, sizeof(mvs_wide_error), "program was already patched before profile initialization");
#endif
}

void mvs_wide_update(void)
{
#if defined(DESKTOP)
	bool requested;

	/* The 16:9 stretch preset that used to request this mode is gone;
	 * until a menu option replaces it, only the environment requests it. */
	requested = mvs_true_wide_forced();
	if (requested == (bool)mvs_true_wide_enabled() || mvs_wide_warning_emitted)
		return;
	if (!mvs_wide_supported ||
		!mvs_wide_profile_apply(mvs_wide_profile, (uint16_t *)memory_region_cpu1,
			memory_length_cpu1, requested, mvs_wide_error, sizeof(mvs_wide_error)))
	{
		printf("[MVS_WIDE] %s: %s; mode change not applied.\n", game_name, mvs_wide_error);
		mvs_wide_warning_emitted = true;
		return;
	}
	mvs_wide_set_active(requested);
	printf("[MVS_WIDE] %s: %s; profile %s%s.\n", game_name,
		requested ? "400x225 viewport" : "Native viewport",
		requested ? "enabled" : "restored",
		mvs_wide_profile->viewport_only ? " (viewport only)" : "");
#endif
}
