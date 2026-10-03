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
	else
	{
		CHECK(first.data[0] != 0);
		CHECK(game_metadata_find_ngh(&metadata, (uint16_t)first.data[0], &found));
		CHECK(strcmp(first.name, found.name) == 0);
	}

	if (core == GAME_METADATA_CORE_CPS1)
	{
		CHECK(game_metadata_find(&metadata, "ghouls", &found));
		CHECK(game_metadata_title(&found, GAME_METADATA_LANG_ENGLISH) != NULL);
		CHECK(game_metadata_find(&metadata, "ghoulsu", &found));
		CHECK(game_metadata_title(&found, GAME_METADATA_LANG_ENGLISH) != NULL);
	}
	else if (core == GAME_METADATA_CORE_CPS2)
	{
		CHECK(game_metadata_find(&metadata, "ssf2", &found));
		CHECK(found.data[0] == 0x23456789u);
		CHECK(found.data[1] == 0xabcdef01u);
		CHECK(found.data[2] == 0x00400000u);

		CHECK(game_metadata_find(&metadata, "ddtodd", &found));
		CHECK((found.core_flags & GAME_METADATA_CPS2_PHOENIX) != 0);
		CHECK(found.data[0] == 0 && found.data[1] == 0 && found.data[2] == 0);

		CHECK(game_metadata_find(&metadata, "ssf2ta", &found));
		CHECK((found.core_flags & GAME_METADATA_CPS2_CACHE_PARENT_OVERRIDE) != 0);
		CHECK(found.aux_name != NULL && strcmp(found.aux_name, "ssf2t") == 0);

		CHECK(game_metadata_find(&metadata, "mpangj", &found));
		CHECK((found.core_flags & GAME_METADATA_CPS2_CACHE_INDEPENDENT) != 0);
	}
	else if (core == GAME_METADATA_CORE_MVS)
	{
		CHECK(game_metadata_find(&metadata, "kof96ae", &found));
		CHECK(found.core_flags == (GAME_METADATA_MVS_OWNS_CROM
			| GAME_METADATA_MVS_OWNS_SROM | GAME_METADATA_MVS_OWNS_VROM));

		CHECK(game_metadata_find(&metadata, "kof97ps", &found));
		CHECK(found.core_flags == GAME_METADATA_MVS_OWNS_CROM);

		CHECK(game_metadata_find(&metadata, "matrimbl", &found));
		CHECK(found.core_flags == GAME_METADATA_MVS_OWNS_VROM);

		CHECK(game_metadata_find(&metadata, "mslug", &found));
		CHECK(found.core_flags == 0);
	}
	else if (core == GAME_METADATA_CORE_NCDZ)
	{
		CHECK(game_metadata_find_ngh(&metadata, 0x0243, &found));
		CHECK(strcmp(found.name, "lastbld2") == 0);
		CHECK(game_metadata_find_ngh(&metadata, 0x069c, &found));
		CHECK(strcmp(found.name, "fatfury3") == 0);
	}
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
