/******************************************************************************

	ui_text_catalog.c

	Common runtime loader for NJEMU .lng translation packs.

******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_text_catalog.h"

#define UI_TEXT_PACK_HEADER_SIZE 20u

struct ui_text_catalog
{
	uint16_t language;
	uint16_t message_count;
	uint32_t string_blob_size;
	size_t allocation_size;
	uint16_t *offsets;
	char *strings;
};

static const char *const language_tags[UI_TEXT_PACK_LANGUAGE_COUNT] = {
	"en",
	"ja",
	"es",
	"zh-Hans",
	"zh-Hant"
};

static uint16_t read_u16le(const unsigned char *data)
{
	return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_u32le(const unsigned char *data)
{
	return (uint32_t)data[0]
		| ((uint32_t)data[1] << 8)
		| ((uint32_t)data[2] << 16)
		| ((uint32_t)data[3] << 24);
}

static void set_error(ui_text_catalog_error_t *error, ui_text_catalog_error_t value)
{
	if (error != NULL)
		*error = value;
}

static int build_pack_path(char *path, size_t path_size, const char *base_dir,
	uint16_t language)
{
	const char *separator;
	int written;
	size_t base_len;

	if (base_dir == NULL || language >= UI_TEXT_PACK_LANGUAGE_COUNT)
		return 0;

	base_len = strlen(base_dir);
	separator = (base_len > 0 && base_dir[base_len - 1] == '/') ? "" : "/";
	written = snprintf(path, path_size, "%s%slang/%s.lng",
		base_dir, separator, language_tags[language]);
	return written >= 0 && (size_t)written < path_size;
}

static ui_text_catalog_t *load_exact(const char *path, uint16_t expected_language,
	ui_text_catalog_error_t *error)
{
	unsigned char header[UI_TEXT_PACK_HEADER_SIZE];
	ui_text_catalog_t *catalog = NULL;
	FILE *file = NULL;
	long file_size_long;
	size_t file_size;
	size_t expected_size;
	size_t allocation_size;
	uint16_t version;
	uint16_t language;
	uint16_t message_count;
	uint16_t reserved;
	uint32_t blob_size;
	uint32_t schema_hash;
	unsigned char *raw_offsets;
	uint16_t i;

	set_error(error, UI_TEXT_CATALOG_OPEN_FAILED);
	file = fopen(path, "rb");
	if (file == NULL)
		return NULL;

	if (fseek(file, 0, SEEK_END) != 0) {
		set_error(error, UI_TEXT_CATALOG_IO_ERROR);
		goto fail;
	}
	file_size_long = ftell(file);
	if (file_size_long < 0 || fseek(file, 0, SEEK_SET) != 0) {
		set_error(error, UI_TEXT_CATALOG_IO_ERROR);
		goto fail;
	}
	file_size = (size_t)file_size_long;
	if (file_size < UI_TEXT_PACK_HEADER_SIZE) {
		set_error(error, UI_TEXT_CATALOG_BAD_SIZE);
		goto fail;
	}
	if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
		set_error(error, UI_TEXT_CATALOG_IO_ERROR);
		goto fail;
	}

	if (memcmp(header, "NJTL", 4) != 0) {
		set_error(error, UI_TEXT_CATALOG_BAD_MAGIC);
		goto fail;
	}
	version = read_u16le(header + 4);
	language = read_u16le(header + 6);
	message_count = read_u16le(header + 8);
	reserved = read_u16le(header + 10);
	blob_size = read_u32le(header + 12);
	schema_hash = read_u32le(header + 16);

	if (version != UI_TEXT_PACK_VERSION) {
		set_error(error, UI_TEXT_CATALOG_BAD_VERSION);
		goto fail;
	}
	if (language != expected_language || language >= UI_TEXT_PACK_LANGUAGE_COUNT) {
		set_error(error, UI_TEXT_CATALOG_BAD_LANGUAGE);
		goto fail;
	}
	if (message_count != UI_TEXT_MAX) {
		set_error(error, UI_TEXT_CATALOG_BAD_COUNT);
		goto fail;
	}
	if (reserved != 0) {
		set_error(error, UI_TEXT_CATALOG_BAD_RESERVED);
		goto fail;
	}
	if (blob_size > UI_TEXT_PACK_MAX_BLOB_SIZE) {
		set_error(error, UI_TEXT_CATALOG_BAD_SIZE);
		goto fail;
	}
	if (schema_hash != UI_TEXT_SCHEMA_HASH) {
		set_error(error, UI_TEXT_CATALOG_BAD_SCHEMA);
		goto fail;
	}

	expected_size = UI_TEXT_PACK_HEADER_SIZE + (size_t)message_count * 2u + blob_size;
	if (file_size != expected_size) {
		set_error(error, UI_TEXT_CATALOG_BAD_SIZE);
		goto fail;
	}
	allocation_size = sizeof(*catalog) + (size_t)message_count * sizeof(uint16_t) + blob_size;
	catalog = (ui_text_catalog_t *)malloc(allocation_size);
	if (catalog == NULL) {
		set_error(error, UI_TEXT_CATALOG_OUT_OF_MEMORY);
		goto fail;
	}

	catalog->language = language;
	catalog->message_count = message_count;
	catalog->string_blob_size = blob_size;
	catalog->allocation_size = allocation_size;
	catalog->offsets = (uint16_t *)(catalog + 1);
	catalog->strings = (char *)(catalog->offsets + message_count);

	raw_offsets = (unsigned char *)catalog->offsets;
	if (fread(raw_offsets, 1, (size_t)message_count * 2u, file) != (size_t)message_count * 2u) {
		set_error(error, UI_TEXT_CATALOG_IO_ERROR);
		goto fail;
	}
	for (i = 0; i < message_count; ++i)
		catalog->offsets[i] = read_u16le(raw_offsets + (size_t)i * 2u);

	if (blob_size > 0 && fread(catalog->strings, 1, blob_size, file) != blob_size) {
		set_error(error, UI_TEXT_CATALOG_IO_ERROR);
		goto fail;
	}

	for (i = 0; i < message_count; ++i) {
		uint16_t offset = catalog->offsets[i];
		if (offset == UI_TEXT_PACK_NULL_OFFSET)
			continue;
		if (offset >= blob_size) {
			set_error(error, UI_TEXT_CATALOG_BAD_OFFSET);
			goto fail;
		}
		if (memchr(catalog->strings + offset, '\0', blob_size - offset) == NULL) {
			set_error(error, UI_TEXT_CATALOG_MISSING_TERMINATOR);
			goto fail;
		}
	}
	if (catalog->offsets[END_OF_TEXT] != UI_TEXT_PACK_NULL_OFFSET) {
		set_error(error, UI_TEXT_CATALOG_BAD_OFFSET);
		goto fail;
	}

	fclose(file);
	set_error(error, UI_TEXT_CATALOG_OK);
	return catalog;

fail:
	if (file != NULL)
		fclose(file);
	free(catalog);
	return NULL;
}

ui_text_catalog_t *ui_text_catalog_load(const char *base_dir,
	uint16_t requested_language, uint16_t *loaded_language,
	ui_text_catalog_error_t *error)
{
	char path[1024];
	ui_text_catalog_t *catalog;
	ui_text_catalog_error_t load_error;
	uint16_t language = requested_language;

	if (language >= UI_TEXT_PACK_LANGUAGE_COUNT)
		language = UI_TEXT_PACK_LANG_ENGLISH;

	if (build_pack_path(path, sizeof(path), base_dir, language)) {
		catalog = load_exact(path, language, &load_error);
		if (catalog != NULL) {
			if (loaded_language != NULL)
				*loaded_language = language;
			set_error(error, UI_TEXT_CATALOG_OK);
			return catalog;
		}
	} else {
		load_error = UI_TEXT_CATALOG_BAD_SIZE;
	}

	if (language != UI_TEXT_PACK_LANG_ENGLISH) {
		language = UI_TEXT_PACK_LANG_ENGLISH;
		if (build_pack_path(path, sizeof(path), base_dir, language)) {
			catalog = load_exact(path, language, &load_error);
			if (catalog != NULL) {
				if (loaded_language != NULL)
					*loaded_language = language;
				set_error(error, UI_TEXT_CATALOG_OK);
				return catalog;
			}
		} else {
			load_error = UI_TEXT_CATALOG_BAD_SIZE;
		}
	}

	set_error(error, load_error);
	return NULL;
}

void ui_text_catalog_free(ui_text_catalog_t *catalog)
{
	free(catalog);
}

const char *ui_text_catalog_get(const ui_text_catalog_t *catalog, ui_text_id_t id)
{
	uint16_t offset;
	if (catalog == NULL || (unsigned)id >= catalog->message_count)
		return "";
	offset = catalog->offsets[id];
	if (offset == UI_TEXT_PACK_NULL_OFFSET)
		return NULL;
	return catalog->strings + offset;
}

uint16_t ui_text_catalog_language(const ui_text_catalog_t *catalog)
{
	return catalog != NULL ? catalog->language : UI_TEXT_PACK_LANG_ENGLISH;
}

size_t ui_text_catalog_allocation_size(const ui_text_catalog_t *catalog)
{
	return catalog != NULL ? catalog->allocation_size : 0;
}

const char *ui_text_catalog_error_string(ui_text_catalog_error_t error)
{
	switch (error) {
	case UI_TEXT_CATALOG_OK: return "ok";
	case UI_TEXT_CATALOG_OPEN_FAILED: return "open failed";
	case UI_TEXT_CATALOG_IO_ERROR: return "I/O error";
	case UI_TEXT_CATALOG_BAD_MAGIC: return "bad magic";
	case UI_TEXT_CATALOG_BAD_VERSION: return "bad version";
	case UI_TEXT_CATALOG_BAD_LANGUAGE: return "bad language";
	case UI_TEXT_CATALOG_BAD_COUNT: return "bad message count";
	case UI_TEXT_CATALOG_BAD_RESERVED: return "bad reserved field";
	case UI_TEXT_CATALOG_BAD_SIZE: return "bad size";
	case UI_TEXT_CATALOG_BAD_SCHEMA: return "bad schema";
	case UI_TEXT_CATALOG_OUT_OF_MEMORY: return "out of memory";
	case UI_TEXT_CATALOG_BAD_OFFSET: return "bad offset";
	case UI_TEXT_CATALOG_MISSING_TERMINATOR: return "missing NUL terminator";
	default: return "unknown error";
	}
}
