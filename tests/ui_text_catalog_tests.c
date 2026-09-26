#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/ui_text_catalog.h"

#ifndef TEST_TRANSLATION_ROOT
#error TEST_TRANSLATION_ROOT must point at a build directory containing lang/en.lng
#endif

#define PACK_HEADER_SIZE 20u

static unsigned char *read_file(const char *path, size_t *size)
{
	FILE *file;
	long end;
	unsigned char *data;

	file = fopen(path, "rb");
	assert(file != NULL);
	assert(fseek(file, 0, SEEK_END) == 0);
	end = ftell(file);
	assert(end >= 0);
	assert(fseek(file, 0, SEEK_SET) == 0);
	data = (unsigned char *)malloc((size_t)end);
	assert(data != NULL);
	assert(fread(data, 1, (size_t)end, file) == (size_t)end);
	assert(fclose(file) == 0);
	*size = (size_t)end;
	return data;
}

static void write_file(const char *path, const unsigned char *data, size_t size)
{
	FILE *file = fopen(path, "wb");
	assert(file != NULL);
	assert(fwrite(data, 1, size, file) == size);
	assert(fclose(file) == 0);
}

static void copy_file(const char *source, const char *destination)
{
	size_t size;
	unsigned char *data = read_file(source, &size);
	write_file(destination, data, size);
	free(data);
}

static uint32_t read_u32le(const unsigned char *data)
{
	return (uint32_t)data[0]
		| ((uint32_t)data[1] << 8)
		| ((uint32_t)data[2] << 16)
		| ((uint32_t)data[3] << 24);
}

static uint16_t read_u16le(const unsigned char *data)
{
	return (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
}

static void write_u16le(unsigned char *data, uint16_t value)
{
	data[0] = (unsigned char)(value & 0xffu);
	data[1] = (unsigned char)(value >> 8);
}

static void write_u32le(unsigned char *data, uint32_t value)
{
	data[0] = (unsigned char)(value & 0xffu);
	data[1] = (unsigned char)((value >> 8) & 0xffu);
	data[2] = (unsigned char)((value >> 16) & 0xffu);
	data[3] = (unsigned char)((value >> 24) & 0xffu);
}

static void make_temp_root(char *root, size_t root_size)
{
	char template_path[] = "/tmp/njemu-ui-text-XXXXXX";
	char *created = mkdtemp(template_path);
	char lang_path[1024];

	assert(created != NULL);
	assert(strlen(created) + 1 <= root_size);
	strcpy(root, created);
	assert(snprintf(lang_path, sizeof(lang_path), "%s/lang", root) > 0);
	assert(mkdir(lang_path, 0700) == 0);
}

static void temp_path(char *path, size_t path_size, const char *root, const char *file)
{
	int written = snprintf(path, path_size, "%s/lang/%s", root, file);
	assert(written >= 0 && (size_t)written < path_size);
}

static void remove_temp_root(const char *root)
{
	char path[1024];

	temp_path(path, sizeof(path), root, "en.lng");
	unlink(path);
	temp_path(path, sizeof(path), root, "ja.lng");
	unlink(path);
	assert(snprintf(path, sizeof(path), "%s/lang", root) > 0);
	assert(rmdir(path) == 0);
	assert(rmdir(root) == 0);
}

static void source_pack_path(char *path, size_t path_size)
{
	int written = snprintf(path, path_size, "%s/lang/en.lng", TEST_TRANSLATION_ROOT);
	assert(written >= 0 && (size_t)written < path_size);
}

static void test_valid_catalog(void)
{
	ui_text_catalog_error_t error = UI_TEXT_CATALOG_IO_ERROR;
	ui_language_t language = UI_LANG_COUNT;
	ui_text_catalog_t *catalog = ui_text_catalog_load(
		TEST_TRANSLATION_ROOT, UI_LANG_ENGLISH, &language, &error);

	assert(catalog != NULL);
	assert(error == UI_TEXT_CATALOG_OK);
	assert(language == UI_LANG_ENGLISH);
	assert(ui_text_catalog_language(catalog) == UI_LANG_ENGLISH);
	assert(ui_text_catalog_allocation_size(catalog) > 7000u);
	assert(strcmp(ui_text_catalog_get(catalog, PLEASE_WAIT), "Please wait...") == 0);
	assert(strcmp(ui_text_catalog_get(catalog, EOM), "") == 0);
	assert(ui_text_catalog_get(catalog, END_OF_TEXT) == NULL);
	assert(strcmp(ui_text_catalog_get(catalog, (ui_text_id_t)UI_TEXT_MAX), "") == 0);
	ui_text_catalog_free(catalog);
}

static void test_missing_requested_language_falls_back(void)
{
	char root[1024];
	char source[1024];
	char destination[1024];
	ui_text_catalog_error_t error;
	ui_language_t language = UI_LANG_COUNT;
	ui_text_catalog_t *catalog;

	make_temp_root(root, sizeof(root));
	source_pack_path(source, sizeof(source));
	temp_path(destination, sizeof(destination), root, "en.lng");
	copy_file(source, destination);
	catalog = ui_text_catalog_load(root, UI_LANG_JAPANESE, &language, &error);
	assert(catalog != NULL);
	assert(error == UI_TEXT_CATALOG_OK);
	assert(language == UI_LANG_ENGLISH);
	assert(strcmp(ui_text_catalog_get(catalog, PLEASE_WAIT), "Please wait...") == 0);
	ui_text_catalog_free(catalog);
	remove_temp_root(root);
}

static void test_corrupt_requested_language_falls_back(void)
{
	char root[1024];
	char source[1024];
	char destination[1024];
	ui_text_catalog_error_t error;
	ui_language_t language = UI_LANG_COUNT;
	ui_text_catalog_t *catalog;

	make_temp_root(root, sizeof(root));
	source_pack_path(source, sizeof(source));
	temp_path(destination, sizeof(destination), root, "en.lng");
	copy_file(source, destination);
	temp_path(destination, sizeof(destination), root, "ja.lng");
	copy_file(source, destination); /* Header still says English: invalid as ja.lng. */
	catalog = ui_text_catalog_load(root, UI_LANG_JAPANESE, &language, &error);
	assert(catalog != NULL);
	assert(error == UI_TEXT_CATALOG_OK);
	assert(language == UI_LANG_ENGLISH);
	ui_text_catalog_free(catalog);
	remove_temp_root(root);
}

static void assert_corrupt_english_rejected(size_t offset, const unsigned char *replacement,
	size_t replacement_size, ui_text_catalog_error_t expected_error, int truncate_last_byte)
{
	char root[1024];
	char source[1024];
	char destination[1024];
	size_t size;
	unsigned char *data;
	ui_text_catalog_error_t error = UI_TEXT_CATALOG_OK;
	ui_text_catalog_t *catalog;

	make_temp_root(root, sizeof(root));
	source_pack_path(source, sizeof(source));
	data = read_file(source, &size);
	assert(offset + replacement_size <= size);
	memcpy(data + offset, replacement, replacement_size);
	if (truncate_last_byte)
		--size;
	temp_path(destination, sizeof(destination), root, "en.lng");
	write_file(destination, data, size);
	free(data);

	catalog = ui_text_catalog_load(root, UI_LANG_ENGLISH, NULL, &error);
	assert(catalog == NULL);
	assert(error == expected_error);
	remove_temp_root(root);
}

static void test_corrupt_catalogs(void)
{
	unsigned char magic[4] = { 'B', 'A', 'D', '!' };
	unsigned char schema[4];
	unsigned char offset[2];
	unsigned char noop = 0;
	unsigned char invalid_utf8 = 0xff;
	char source[1024];
	size_t size;
	unsigned char *data;
	uint32_t blob_size;
	uint16_t please_wait_offset;

	assert_corrupt_english_rejected(0, magic, sizeof(magic), UI_TEXT_CATALOG_BAD_MAGIC, 0);
	write_u32le(schema, UI_TEXT_SCHEMA_HASH ^ 1u);
	assert_corrupt_english_rejected(16, schema, sizeof(schema), UI_TEXT_CATALOG_BAD_SCHEMA, 0);

	source_pack_path(source, sizeof(source));
	data = read_file(source, &size);
	blob_size = read_u32le(data + 12);
	please_wait_offset = read_u16le(data + PACK_HEADER_SIZE + PLEASE_WAIT * 2u);
	free(data);
	write_u16le(offset, (uint16_t)blob_size);
	assert_corrupt_english_rejected(PACK_HEADER_SIZE, offset, sizeof(offset),
		UI_TEXT_CATALOG_BAD_OFFSET, 0);
	assert_corrupt_english_rejected(0, &noop, 0, UI_TEXT_CATALOG_BAD_SIZE, 1);
	assert_corrupt_english_rejected(
		PACK_HEADER_SIZE + UI_TEXT_MAX * 2u + please_wait_offset,
		&invalid_utf8, sizeof(invalid_utf8), UI_TEXT_CATALOG_BAD_UTF8, 0);
}

static void test_missing_english_fails(void)
{
	char root[1024];
	ui_text_catalog_error_t error = UI_TEXT_CATALOG_OK;
	ui_text_catalog_t *catalog;

	make_temp_root(root, sizeof(root));
	catalog = ui_text_catalog_load(root, UI_LANG_ENGLISH, NULL, &error);
	assert(catalog == NULL);
	assert(error == UI_TEXT_CATALOG_OPEN_FAILED);
	remove_temp_root(root);
}

int main(void)
{
	test_valid_catalog();
	test_missing_requested_language_falls_back();
	test_corrupt_requested_language_falls_back();
	test_corrupt_catalogs();
	test_missing_english_fails();
	return 0;
}
