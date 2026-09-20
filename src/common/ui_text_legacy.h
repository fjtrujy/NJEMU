/******************************************************************************

	ui_text_legacy.h

	Temporary adapter for the embedded positional translation catalogs.
	Remove this file when T6 removes the legacy tables.

******************************************************************************/

#ifndef UI_TEXT_LEGACY_H
#define UI_TEXT_LEGACY_H

#include "../emucfg.h"
#include "ui_text_ids.h"

typedef enum legacy_ui_text_id
{
#define LEGACY_UI_TEXT_ID(legacy_name, stable_id) LEGACY_##legacy_name,
#include "../../translations/legacy_layout.def"
#undef LEGACY_UI_TEXT_ID
	LEGACY_UI_TEXT_MAX
} legacy_ui_text_id_t;

void ui_text_copy_legacy_catalog(const char **destination,
					 const char *const *legacy_catalog);

#endif /* UI_TEXT_LEGACY_H */
