/******************************************************************************

	ui_text_catalog.h

	Common runtime loader for NJEMU .lng translation packs.

******************************************************************************/

#ifndef UI_TEXT_CATALOG_H
#define UI_TEXT_CATALOG_H

#include <stddef.h>
#include <stdint.h>

#include "ui_language.h"
#include "ui_text_ids.h"

#define UI_TEXT_PACK_VERSION 1u
#define UI_TEXT_PACK_NULL_OFFSET 0xffffu
#define UI_TEXT_PACK_MAX_BLOB_SIZE 65534u

typedef enum ui_text_catalog_error
{
	UI_TEXT_CATALOG_OK = 0,
	UI_TEXT_CATALOG_OPEN_FAILED,
	UI_TEXT_CATALOG_IO_ERROR,
	UI_TEXT_CATALOG_BAD_MAGIC,
	UI_TEXT_CATALOG_BAD_VERSION,
	UI_TEXT_CATALOG_BAD_LANGUAGE,
	UI_TEXT_CATALOG_BAD_COUNT,
	UI_TEXT_CATALOG_BAD_RESERVED,
	UI_TEXT_CATALOG_BAD_SIZE,
	UI_TEXT_CATALOG_BAD_SCHEMA,
	UI_TEXT_CATALOG_OUT_OF_MEMORY,
	UI_TEXT_CATALOG_BAD_OFFSET,
	UI_TEXT_CATALOG_MISSING_TERMINATOR
} ui_text_catalog_error_t;

typedef struct ui_text_catalog ui_text_catalog_t;

ui_text_catalog_t *ui_text_catalog_load(const char *base_dir,
	ui_language_t requested_language, ui_language_t *loaded_language,
	ui_text_catalog_error_t *error);
void ui_text_catalog_free(ui_text_catalog_t *catalog);
const char *ui_text_catalog_get(const ui_text_catalog_t *catalog, ui_text_id_t id);
ui_language_t ui_text_catalog_language(const ui_text_catalog_t *catalog);
size_t ui_text_catalog_allocation_size(const ui_text_catalog_t *catalog);
const char *ui_text_catalog_error_string(ui_text_catalog_error_t error);

#endif /* UI_TEXT_CATALOG_H */
