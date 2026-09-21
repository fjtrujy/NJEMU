/******************************************************************************

	ui_text_driver.c

******************************************************************************/

#include <stdio.h>

#include "platform_driver.h"
#include "ui_text_catalog.h"
#include "ui_text_driver.h"

extern char launchDir[];

void *ui_text_data;

static void *common_text_init(void)
{
	ui_text_catalog_error_t error;
	ui_language_t requested_language = UI_LANG_ENGLISH;
	ui_language_t loaded_language = UI_LANG_ENGLISH;
	ui_text_catalog_t *catalog;

	if (platform_driver->getSystemLanguage != NULL)
		requested_language = platform_driver->getSystemLanguage(platform_data);

	catalog = ui_text_catalog_load(launchDir, requested_language,
		&loaded_language, &error);
	if (catalog == NULL) {
		printf("Failed to load translation catalog: %s\n",
			ui_text_catalog_error_string(error));
		return NULL;
	}

	if (loaded_language != requested_language) {
		printf("Translation catalog %d unavailable; using English fallback\n",
			(int)requested_language);
	}
	return catalog;
}

static void common_text_free(void *data)
{
	ui_text_catalog_free((ui_text_catalog_t *)data);
}

static ui_language_t common_text_get_language(void *data)
{
	return ui_text_catalog_language((const ui_text_catalog_t *)data);
}

static const char *common_text_get(void *data, ui_text_id_t id)
{
	return ui_text_catalog_get((const ui_text_catalog_t *)data, id);
}

ui_text_driver_t ui_text_common = {
	"common",
	common_text_init,
	common_text_free,
	common_text_get_language,
	common_text_get,
};
