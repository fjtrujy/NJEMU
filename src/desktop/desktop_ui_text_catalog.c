/******************************************************************************

	desktop_ui_text_catalog.c

	Desktop UI text driver backed by the common external .lng loader.

******************************************************************************/

#include <stdio.h>

#include "emumain.h"
#include "common/ui_text_catalog.h"

static void *desktop_text_init(void)
{
	ui_text_catalog_error_t error;
	ui_text_catalog_t *catalog = ui_text_catalog_load(
		launchDir, UI_TEXT_PACK_LANG_ENGLISH, NULL, &error);

	if (catalog == NULL) {
		printf("Failed to load Desktop translation catalog: %s\n",
			ui_text_catalog_error_string(error));
		return NULL;
	}
	return catalog;
}

static void desktop_text_free(void *data)
{
	ui_text_catalog_free((ui_text_catalog_t *)data);
}

static int32_t desktop_text_get_language(void *data)
{
	return (int32_t)ui_text_catalog_language((const ui_text_catalog_t *)data);
}

static const char *desktop_text_get(void *data, ui_text_id_t id)
{
	return ui_text_catalog_get((const ui_text_catalog_t *)data, id);
}

ui_text_driver_t ui_text_desktop = {
	"desktop",
	desktop_text_init,
	desktop_text_free,
	desktop_text_get_language,
	desktop_text_get,
};
