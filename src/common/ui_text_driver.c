/******************************************************************************

	ui_text_driver.c

******************************************************************************/

#include <stddef.h>
#include <string.h>
#include "ui_text_driver.h"
#include "ui_text_legacy.h"

void *ui_text_data;

static const uint16_t legacy_to_stable_id[LEGACY_UI_TEXT_MAX] = {
#define LEGACY_UI_TEXT_ID(legacy_name, stable_id) stable_id,
#include "../../translations/legacy_layout.def"
#undef LEGACY_UI_TEXT_ID
};

void ui_text_copy_legacy_catalog(const char **destination,
					 const char *const *legacy_catalog)
{
	int i;

	memset(destination, 0, sizeof(*destination) * UI_TEXT_MAX);
	for (i = 0; i < LEGACY_UI_TEXT_MAX; ++i)
		destination[legacy_to_stable_id[i]] = legacy_catalog[i];
}

ui_text_driver_t ui_text_null = {
	"null",
	NULL,
	NULL,
	NULL,
	NULL,
};

ui_text_driver_t *ui_text_drivers[] = {
#ifdef PSP
	&ui_text_psp,
#endif
#ifdef PS2
	&ui_text_ps2,
#endif
#ifdef DESKTOP
	&ui_text_desktop,
#endif
	&ui_text_null,
	NULL,
};
