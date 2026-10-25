/* Per-game widescreen profile lifecycle. The renderer only consumes the
 * resulting view geometry (mvs/wide.c). */
#include "mvs.h"
#include "mvs/wide.h"
#include "mvs/wide_debug.h"
#if MVS_WIDESCREEN
#include "common/runtime_paths.h"
#include "common/wide_profile.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef GUI
#include "common/ui.h"
#include "common/ui_text_driver.h"
#endif

int option_mvs_widescreen;

static bool mvs_wide_supported;
static bool mvs_wide_warning_emitted;
static wide_profile_t *mvs_profile;
static char mvs_wide_error[192];

/* Test override: NJEMU_MVS_TRUE_WIDE requests the mode without the option. */
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
#if MVS_WIDESCREEN
	free(mvs_profile);
	mvs_profile = NULL;
	mvs_wide_supported = false;
#endif
	mvs_wide_set_active(false);
}

/* PSP only resolves this ~32 KiB profile when the player requests widescreen. */
#if MVS_WIDESCREEN
static void mvs_wide_load_profile(void)
{
	char path[PATH_MAX];
	const char *directory = getenv("NJEMU_MVS_PROFILE_DIR");
	const char *experimental = getenv("NJEMU_MVS_ALLOW_EXPERIMENTAL");
	int length, program_mode;
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
	mvs_profile = wide_profile_load(path, mvs_wide_error, sizeof(mvs_wide_error));
	if (!mvs_profile) return;
	if (strcmp(mvs_profile->game, game_name) || mvs_profile->ngh != neogeo_ngh)
	{
		snprintf(mvs_wide_error, sizeof(mvs_wide_error), "profile belongs to a different game/NGH");
		return;
	}
	if (mvs_profile->status == WIDE_PROFILE_DRAFT ||
		(mvs_profile->status == WIDE_PROFILE_EXPERIMENTAL &&
		(!experimental || strcmp(experimental, "1"))))
	{
		snprintf(mvs_wide_error, sizeof(mvs_wide_error), "draft or experimental profile not authorized");
		return;
	}
	program_mode = wide_profile_check(mvs_profile,
		(const uint16_t *)memory_region_cpu1, memory_length_cpu1,
		mvs_wide_error, sizeof(mvs_wide_error));
	mvs_wide_supported = program_mode == 0;
	if (program_mode == 1)
		snprintf(mvs_wide_error, sizeof(mvs_wide_error), "program was already patched before profile initialization");
}
#endif

void mvs_wide_init(void)
{
	mvs_wide_exit();
#if MVS_WIDESCREEN
	mvs_wide_warning_emitted = false;
#if defined(DESKTOP) && !RELEASE
	mvs_wide_debug_init();
#endif
#ifndef PSP
	mvs_wide_load_profile();
#endif
#endif
}

#if MVS_WIDESCREEN
static void mvs_wide_transition(bool requested)
{
	/* A failed request is not retried until the next game load. */
	if (requested && mvs_wide_warning_emitted)
	{
		option_mvs_widescreen = mvs_true_wide_enabled();
		return;
	}
#ifdef PSP
	if (requested && !mvs_profile)
		mvs_wide_load_profile();
#endif
	if (!mvs_wide_supported ||
		!wide_profile_apply(mvs_profile, (uint16_t *)memory_region_cpu1,
			memory_length_cpu1, requested, mvs_wide_error, sizeof(mvs_wide_error)))
	{
		printf("[MVS_WIDE] %s: %s; mode change not applied.\n", game_name, mvs_wide_error);
		mvs_wide_warning_emitted = true;
		/* The game keeps its current view: show the option as it is. */
		if (option_mvs_widescreen != mvs_true_wide_enabled())
		{
			option_mvs_widescreen = mvs_true_wide_enabled();
#ifdef GUI
			ui_popup(TEXT(WIDESCREEN_NOT_AVAILABLE));
#endif
		}
#ifdef PSP
		if (!mvs_true_wide_enabled())
		{
			free(mvs_profile);
			mvs_profile = NULL;
			mvs_wide_supported = false;
		}
#endif
		return;
	}
	mvs_wide_set_active(requested);
	printf("[MVS_WIDE] %s: %s; profile %s%s.\n", game_name,
		requested ? "400x225 viewport" : "Native viewport",
		requested ? "enabled" : "restored",
		mvs_profile->viewport_only ? " (viewport only)" : "");
#ifdef PSP
	/* The ROM has been restored; keep no profile allocation in native mode. */
	if (!requested)
	{
		free(mvs_profile);
		mvs_profile = NULL;
		mvs_wide_supported = false;
	}
#endif
}
#endif

void mvs_wide_update(void)
{
#if MVS_WIDESCREEN
	bool requested = option_mvs_widescreen || mvs_true_wide_forced();

	if (requested != (bool)mvs_true_wide_enabled())
		mvs_wide_transition(requested);
	/* The frame about to run displays the sprites its vblank uploads, which
	 * the game built from the RAM state present now. */
	if (mvs_true_wide_enabled() && mvs_profile->has_layers)
	{
		const mvs_view_geometry_t *view = mvs_get_view_geometry();
		wide_layers_pan(&mvs_profile->layers, m68000_read_memory_16,
			view->render_left - view->x_bias, view->render_right - view->x_bias,
			mvs_wide_sprite_pan, MVS_WIDE_PAN_SPRITES);
	}
#endif
}
