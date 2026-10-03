#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/game_metadata.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		return 1; \
	} \
} while (0)

static int read_file(const char *path, unsigned char **data, size_t *size)
{
	FILE *file;
	long length;
	int close_result;
	size_t read_size;

	file = fopen(path, "rb");
	if (file == NULL)
		return 0;
	if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0
		|| fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return 0;
	}
	*data = (unsigned char *)malloc((size_t)length);
	if (*data == NULL)
	{
		fclose(file);
		return 0;
	}
	*size = (size_t)length;
	read_size = fread(*data, 1, *size, file);
	close_result = fclose(file);
	if (read_size != *size || close_result != 0)
	{
		free(*data);
		*data = NULL;
		return 0;
	}
	return 1;
}

static int write_file(const char *path, const unsigned char *data, size_t size)
{
	FILE *file = fopen(path, "wb");
	int close_result;
	size_t write_size;

	if (file == NULL)
		return 0;
	write_size = fwrite(data, 1, size, file);
	close_result = fclose(file);
	if (write_size != size || close_result != 0)
	{
		remove(path);
		return 0;
	}
	return 1;
}

int main(int argc, char **argv)
{
	game_metadata_t metadata = {0};
	game_metadata_entry_t first, found;
	game_metadata_core_t core = game_metadata_current_core();
	game_metadata_core_t wrong_core = core == GAME_METADATA_CORE_CPS1
		? GAME_METADATA_CORE_CPS2 : GAME_METADATA_CORE_CPS1;
	game_metadata_error_t error;
	unsigned char *bytes = NULL;
	size_t size = 0;
	char corrupt_path[1024];
	char truncated_path[1024];

	CHECK(argc == 2);
	CHECK(strcmp(game_metadata_filename(), strrchr(argv[1], '/') != NULL
		? strrchr(argv[1], '/') + 1 : argv[1]) == 0);

	error = game_metadata_load(&metadata, argv[1], core);
	CHECK(error == GAME_METADATA_OK);
	CHECK(game_metadata_count(&metadata) > 0);
	CHECK(game_metadata_get(&metadata, 0, &first));
	CHECK(first.name[0] != '\0');
	CHECK(game_metadata_find(&metadata, first.name, &found));
	CHECK(strcmp(first.name, found.name) == 0);
	if (core != GAME_METADATA_CORE_NCDZ)
		CHECK(game_metadata_title(&first, GAME_METADATA_LANG_ENGLISH) != NULL);
	game_metadata_unload(&metadata);

	error = game_metadata_load(&metadata, argv[1], wrong_core);
	CHECK(error == GAME_METADATA_ERROR_CORE);
	CHECK(metadata.data == NULL);

	CHECK(read_file(argv[1], &bytes, &size));
	CHECK(size > 69);
	CHECK(snprintf(corrupt_path, sizeof(corrupt_path), "%s.corrupt-test", argv[1])
		> 0);
	/* Header is 32 bytes and data0 starts 36 bytes into the first record.
	 * Mutating payload rather than the string-pool terminator ensures every
	 * core reaches the checksum validation path. */
	bytes[68] ^= 0x01;
	CHECK(write_file(corrupt_path, bytes, size));
	error = game_metadata_load(&metadata, corrupt_path, core);
	CHECK(error == GAME_METADATA_ERROR_CHECKSUM);
	CHECK(remove(corrupt_path) == 0);

	CHECK(snprintf(truncated_path, sizeof(truncated_path), "%s.truncated-test", argv[1])
		> 0);
	CHECK(write_file(truncated_path, bytes, 16));
	error = game_metadata_load(&metadata, truncated_path, core);
	CHECK(error == GAME_METADATA_ERROR_FORMAT);
	CHECK(remove(truncated_path) == 0);

	free(bytes);
	game_metadata_unload(&metadata);
	return 0;
}
