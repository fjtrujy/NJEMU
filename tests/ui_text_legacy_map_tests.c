#include <assert.h>
#include <stddef.h>

#include "emucfg.h"
#include "common/ui_text_driver.h"
#include "common/ui_text_legacy.h"

/* ui_text_driver.c references the active platform driver registry entry. */
ui_text_driver_t ui_text_desktop;

int main(void)
{
	char tokens[LEGACY_UI_TEXT_MAX];
	const char *legacy[LEGACY_UI_TEXT_MAX];
	const char *stable[UI_TEXT_MAX];
	unsigned char seen[UI_TEXT_MAX] = { 0 };
	int i;

	for (i = 0; i < LEGACY_UI_TEXT_MAX; ++i)
		legacy[i] = &tokens[i];

	ui_text_copy_legacy_catalog(stable, legacy);

#define LEGACY_UI_TEXT_ID(legacy_name, stable_id) \
	do { \
		assert(stable[stable_id] == legacy[LEGACY_##legacy_name]); \
		assert(!seen[stable_id]); \
		seen[stable_id] = 1; \
	} while (0);
#include "../translations/legacy_layout.def"
#undef LEGACY_UI_TEXT_ID

	for (i = 0; i < UI_TEXT_MAX; ++i) {
		if (!seen[i])
			assert(stable[i] == NULL);
	}

	return 0;
}
