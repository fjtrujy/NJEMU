/* Per-game widescreen profile lifecycle. The renderer only consumes the
 * resulting margin and bias (cps1/wide.h). */
#include "cps1.h"
#include "cps1/wide.h"
#include "common/emulator_options.h"

int cps1_wide_margin;
int cps1_wide_bias;

#if CPS_WIDESCREEN
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

int option_cps_widescreen;

static bool cps1_wide_supported;
static bool cps1_wide_warning_emitted;
static wide_profile_t *cps1_profile;
static char cps1_wide_error[192];

/* Test override: NJEMU_CPS1_TRUE_WIDE requests the mode without the option. */
static int cps1_true_wide_forced(void)
{
	static int forced = -1;

	if (forced < 0)
	{
		const char *value = getenv("NJEMU_CPS1_TRUE_WIDE");
		forced = value != NULL && value[0] != '\0' && value[0] != '0';
	}
	return forced;
}
#endif

static void cps1_wide_set_active(bool active)
{
	cps1_wide_margin = active ? CPS1_WIDE_MARGIN : 0;
	cps1_wide_bias = active ? CPS1_WIDE_MARGIN - 32 : 0;
}

void cps1_wide_exit(void)
{
#if CPS_WIDESCREEN
	free(cps1_profile);
	cps1_profile = NULL;
	cps1_wide_supported = false;
#endif
	cps1_wide_set_active(false);
}

#if CPS_WIDESCREEN
static void cps1_wide_load_profile(void)
{
	char path[PATH_MAX];
	const char *directory = getenv("NJEMU_CPS1_PROFILE_DIR");
	const char *experimental = getenv("NJEMU_CPS1_ALLOW_EXPERIMENTAL");
	int length, program_mode;
	cps1_wide_warning_emitted = false;
	if (strspn(game_name, "abcdefghijklmnopqrstuvwxyz0123456789_-") != strlen(game_name))
	{
		snprintf(cps1_wide_error, sizeof(cps1_wide_error), "invalid game identifier");
		return;
	}
	/* The 480-pixel view only exists for horizontal screens. */
	if (machine_screen_type == SCREEN_VERTICAL)
	{
		snprintf(cps1_wide_error, sizeof(cps1_wide_error), "vertical screen");
		return;
	}
	length = directory && *directory ? snprintf(path, sizeof(path), "%s/%s.ini", directory, game_name) :
		snprintf(path, sizeof(path), "%swidescreen/%s.ini", launchDir, game_name);
	if (length < 0 || (size_t)length >= sizeof(path))
	{
		snprintf(cps1_wide_error, sizeof(cps1_wide_error), "profile path too long");
		return;
	}
	cps1_profile = wide_profile_load(path, cps1_wide_error, sizeof(cps1_wide_error));
	if (!cps1_profile) return;
	if (strcmp(cps1_profile->game, game_name) || cps1_profile->has_layers)
	{
		snprintf(cps1_wide_error, sizeof(cps1_wide_error), "profile belongs to a different game or target");
		return;
	}
	if (cps1_profile->status == WIDE_PROFILE_DRAFT ||
		(cps1_profile->status == WIDE_PROFILE_EXPERIMENTAL &&
		(!experimental || strcmp(experimental, "1"))))
	{
		snprintf(cps1_wide_error, sizeof(cps1_wide_error), "draft or experimental profile not authorized");
		return;
	}
	program_mode = wide_profile_check(cps1_profile,
		(const uint16_t *)memory_region_cpu1, memory_length_cpu1,
		cps1_wide_error, sizeof(cps1_wide_error));
	cps1_wide_supported = program_mode == 0;
	if (program_mode == 1)
		snprintf(cps1_wide_error, sizeof(cps1_wide_error), "program was already patched before profile initialization");
	if (!cps1_wide_supported)
		printf("[CPS1_WIDE] %s: %s.\n", game_name, cps1_wide_error);
}
#endif

void cps1_wide_init(void)
{
	cps1_wide_exit();
#if CPS_WIDESCREEN
	cps1_wide_warning_emitted = false;
#ifndef PSP
	cps1_wide_load_profile();
#endif
#endif
}

#if CPS_WIDESCREEN
static void cps1_wide_transition(bool requested)
{
	/* A failed request is not retried until the next game load. */
	if (requested && cps1_wide_warning_emitted)
	{
		option_cps_widescreen = cps1_wide_margin != 0;
		return;
	}
#ifdef PSP
	if (requested && !cps1_profile)
		cps1_wide_load_profile();
#endif
	if (!cps1_wide_supported ||
		!wide_profile_apply(cps1_profile, (uint16_t *)memory_region_cpu1,
			memory_length_cpu1, requested, cps1_wide_error, sizeof(cps1_wide_error)))
	{
		printf("[CPS1_WIDE] %s: %s; mode change not applied.\n", game_name, cps1_wide_error);
		cps1_wide_warning_emitted = true;
		/* The game keeps its current view: show the option as it is. */
		if (option_cps_widescreen != (cps1_wide_margin != 0))
		{
			option_cps_widescreen = cps1_wide_margin != 0;
#ifdef GUI
			ui_popup(TEXT(WIDESCREEN_NOT_AVAILABLE));
#endif
		}
#ifdef PSP
		if (cps1_wide_margin == 0)
		{
			free(cps1_profile);
			cps1_profile = NULL;
			cps1_wide_supported = false;
		}
#endif
		return;
	}
	cps1_wide_set_active(requested);
	printf("[CPS1_WIDE] %s: %s; profile %s.\n", game_name,
		requested ? "480x224 viewport" : "Native viewport",
		requested ? "enabled" : "restored");
#ifdef PSP
	if (!requested)
	{
		free(cps1_profile);
		cps1_profile = NULL;
		cps1_wide_supported = false;
	}
#endif
}
#endif

void cps1_wide_update(void)
{
#if CPS_WIDESCREEN
	bool requested = option_cps_widescreen || cps1_true_wide_forced();

	if (requested != (cps1_wide_margin != 0))
		cps1_wide_transition(requested);
#endif
}
