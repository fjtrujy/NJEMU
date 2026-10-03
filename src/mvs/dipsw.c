/******************************************************************************

	dipsw.c

	MVS DIP Switch Settings

******************************************************************************/

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "mvs.h"
#include "common/dip_metadata.h"
#include "common/emulator_options.h"
#include "common/path_utils.h"
#include "common/runtime_paths.h"
#include "common/ui_text_driver.h"


/******************************************************************************
	Global Variables
******************************************************************************/

int neogeo_hard_dipsw;


/******************************************************************************
	Local State
******************************************************************************/

static dip_metadata_t dip_metadata;
static dipswitch_t *active_dipswitch;
static int active_has_hard_dip;

static const char *dip_profile_name(void)
{
	if (machine_init_type == INIT_ms5pcb || machine_init_type == INIT_svcpcb)
		return "pcb";
#if !RELEASE
	if (machine_init_type == INIT_kog)
		return "kog";
#endif

	switch (neogeo_ngh)
	{
	case NGH_mahretsu:
	case NGH_janshin:
	case NGH_minasan:
	case NGH_bakatono:
	case NGH_fr2ch:
		return "mjneogeo";
	default:
		return "default";
	}
}

static int profile_has_hard_dip(const char *profile)
{
	return !strcmp(profile, "pcb")
#if !RELEASE
		|| !strcmp(profile, "kog")
#endif
		;
}

static dip_metadata_language_t dip_metadata_language(void)
{
	switch (ui_text_driver->getLanguage(ui_text_data))
	{
	case UI_LANG_JAPANESE: return DIP_METADATA_LANG_JAPANESE;
	case UI_LANG_CHINESE_SIMPLIFIED: return DIP_METADATA_LANG_CHINESE_SIMPLIFIED;
	case UI_LANG_CHINESE_TRADITIONAL: return DIP_METADATA_LANG_CHINESE_TRADITIONAL;
	default: return DIP_METADATA_LANG_ENGLISH;
	}
}


/******************************************************************************
	DIP Switch Interface
******************************************************************************/

dipswitch_t *load_dipswitch(void)
{
	uint8_t value = (uint8_t)~neogeo_dipswitch;
	const char *profile = dip_profile_name();
	char path[PATH_MAX];
	dip_metadata_error_t metadata_error;

	dip_metadata_unload(&dip_metadata);
	active_dipswitch = NULL;
	active_has_hard_dip = 0;

	if (!path_format(path, sizeof(path), "%s%s", launchDir, "dip_metadata.mvs"))
		return NULL;
	metadata_error = dip_metadata_load_profile(
		&dip_metadata, path, profile, dip_metadata_language());
	if (metadata_error != DIP_METADATA_OK)
	{
		printf("MVS DIP metadata: cannot load %s/%s: %s\n", path, profile,
			dip_metadata_error_string(metadata_error));
		return NULL;
	}
	active_dipswitch = dip_metadata_active(&dip_metadata);
	active_has_hard_dip = profile_has_hard_dip(profile);
	if (active_has_hard_dip)
		active_dipswitch[6].value = (uint8_t)neogeo_hard_dipsw;

	active_dipswitch[0].value = (value & 0x01) != 0;
	active_dipswitch[1].value = (value & 0x02) != 0;
	active_dipswitch[2].value = (value & 0x04) != 0;
	active_dipswitch[4].value = (value & 0x40) != 0;
	active_dipswitch[5].value = (value & 0x80) != 0;

	switch (neogeo_dipswitch & 0x38)
	{
	case 0x00: active_dipswitch[3].value = 4; break;
	case 0x10: active_dipswitch[3].value = 3; break;
	case 0x20: active_dipswitch[3].value = 2; break;
	case 0x30: active_dipswitch[3].value = 1; break;
	case 0x38: active_dipswitch[3].value = 0; break;
	default: active_dipswitch[3].value = 0; break;
	}

	return active_dipswitch;
}


void save_dipswitch(void)
{
	uint8_t value;

	if (active_dipswitch == NULL)
		return;
	if (active_has_hard_dip)
		neogeo_hard_dipsw = active_dipswitch[6].value;

	value = 0;
	value |= (active_dipswitch[0].value != 0) ? 0x00 : 0x01;
	value |= (active_dipswitch[1].value != 0) ? 0x00 : 0x02;
	value |= (active_dipswitch[2].value != 0) ? 0x00 : 0x04;
	value |= (active_dipswitch[4].value != 0) ? 0x00 : 0x40;
	value |= (active_dipswitch[5].value != 0) ? 0x00 : 0x80;
	switch (active_dipswitch[3].value)
	{
	case 0: value |= 0x38; break;
	case 1: value |= 0x30; break;
	case 2: value |= 0x20; break;
	case 3: value |= 0x10; break;
	case 4: value |= 0x00; break;
	default: value |= 0x38; break;
	}

	neogeo_dipswitch = value;
	dip_metadata_unload(&dip_metadata);
	active_dipswitch = NULL;
	active_has_hard_dip = 0;
}
