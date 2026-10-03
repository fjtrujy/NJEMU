#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/game_database.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		return 1; \
	} \
} while (0)

static uint32_t read_u32_le(const unsigned char *p)
{
	return (uint32_t)p[0]
		| ((uint32_t)p[1] << 8)
		| ((uint32_t)p[2] << 16)
		| ((uint32_t)p[3] << 24);
}

static void write_u32_le(unsigned char *p, uint32_t value)
{
	p[0] = (unsigned char)value;
	p[1] = (unsigned char)(value >> 8);
	p[2] = (unsigned char)(value >> 16);
	p[3] = (unsigned char)(value >> 24);
}

static uint32_t crc32(const unsigned char *data, size_t size)
{
	uint32_t crc = 0xffffffffu;
	size_t i;
	int bit;
	for (i = 0; i < size; i++)
	{
		crc ^= data[i];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
	}
	return crc ^ 0xffffffffu;
}

static int read_file(const char *path, unsigned char **data, size_t *size)
{
	FILE *file;
	long length;
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
	if (fclose(file) != 0 || read_size != *size)
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
	size_t written;
	int close_result;
	if (file == NULL)
		return 0;
	written = fwrite(data, 1, size, file);
	close_result = fclose(file);
	if (written != size || close_result != 0)
	{
		remove(path);
		return 0;
	}
	return 1;
}

static void update_body_crc(unsigned char *data, size_t size)
{
	write_u32_le(data + 56, crc32(data + 64, size - 64));
}

int main(int argc, char **argv)
{
	game_database_t database = {0};
	game_database_game_t game;
	game_database_region_t region;
	game_database_rom_t rom;
	game_database_error_t error;
	uint32_t i, region_total = 0, rom_total = 0;
	unsigned char *bytes = NULL;
	size_t size = 0;
	char temp_path[1024];

	CHECK(argc == 2);
	CHECK(strcmp(game_database_filename(), strrchr(argv[1], '/') != NULL
		? strrchr(argv[1], '/') + 1 : argv[1]) == 0);
	CHECK(game_database_current_core() == GAME_DATABASE_CORE_CPS2);

	error = game_database_open(&database, argv[1], GAME_DATABASE_CORE_CPS2);
	CHECK(error == GAME_DATABASE_OK);
	CHECK(game_database_validate(&database) == GAME_DATABASE_OK);
	CHECK(game_database_count(&database) == 286);
	for (i = 0; i < game_database_count(&database); i++)
	{
		uint32_t j;
		CHECK(game_database_get_game(&database, i, &game) == GAME_DATABASE_OK);
		CHECK(game.name[0] != '\0');
		CHECK(game_database_title(&game, GAME_DATABASE_LANG_ENGLISH) != NULL);
		region_total += game.region_count;
		for (j = 0; j < game.region_count; j++)
		{
			uint32_t k;
			CHECK(game_database_get_region(&database, &game, j, &region) == GAME_DATABASE_OK);
			rom_total += region.rom_count;
			for (k = 0; k < region.rom_count; k++)
				CHECK(game_database_get_rom(&database, &region, k, &rom) == GAME_DATABASE_OK);
		}
	}
	CHECK(region_total == 1387);
	CHECK(rom_total == 5382);
	CHECK(game_database_find_game(&database, "not_a_game", &game)
		== GAME_DATABASE_ERROR_NOT_FOUND);

	CHECK(game_database_find_game(&database, "ssf2", &game) == GAME_DATABASE_OK);
	CHECK(game.data[0] == 0x23456789u);
	CHECK(game.data[1] == 0xabcdef01u);
	CHECK(game.data[2] == 0x00400000u);
	CHECK(game.parent_name[0] == '\0');

	CHECK(game_database_find_game(&database, "ddtodd", &game) == GAME_DATABASE_OK);
	CHECK((game.core_flags & GAME_DATABASE_CPS2_PHOENIX) != 0);
	CHECK(game.data[0] == 0 && game.data[1] == 0 && game.data[2] == 0);

	CHECK(game_database_find_game(&database, "ssf2ta", &game) == GAME_DATABASE_OK);
	CHECK((game.core_flags & GAME_DATABASE_CPS2_CACHE_PARENT_OVERRIDE) != 0);
	CHECK(strcmp(game.aux_name, "ssf2t") == 0);
	CHECK(strcmp(game.parent_name, "ssf2t") == 0);

	CHECK(game_database_find_game(&database, "mpangj", &game) == GAME_DATABASE_OK);
	CHECK((game.core_flags & GAME_DATABASE_CPS2_CACHE_INDEPENDENT) != 0);

	CHECK(game_database_find_game(&database, "1944", &game) == GAME_DATABASE_OK);
	for (i = 0; i < game.region_count; i++)
	{
		CHECK(game_database_get_region(&database, &game, i, &region) == GAME_DATABASE_OK);
		if (region.type == GAME_DATABASE_REGION_CPU2)
		{
			CHECK(region.rom_count == 2);
			CHECK(game_database_get_rom(&database, &region, 1, &rom) == GAME_DATABASE_OK);
			CHECK(rom.type == 1 && rom.name[0] == '\0');
		}
		if (region.type == GAME_DATABASE_REGION_GFX1)
		{
			CHECK(game_database_get_rom(&database, &region, 0, &rom) == GAME_DATABASE_OK);
			CHECK((rom.flags & GAME_DATABASE_ROM_FLAG_ROMX) != 0);
			CHECK(rom.group == 2 && rom.skip == 6);
			CHECK(strcmp(rom.name, "nff.13m") == 0);
		}
	}
	game_database_close(&database);
	CHECK(database.file == NULL);

	error = game_database_open(&database, argv[1], GAME_DATABASE_CORE_CPS1);
	CHECK(error == GAME_DATABASE_ERROR_CORE);
	CHECK(database.file == NULL);

	CHECK(read_file(argv[1], &bytes, &size));
	CHECK(size > 128);
	CHECK(snprintf(temp_path, sizeof(temp_path), "%s.reader-test", argv[1]) > 0);

	bytes[80] ^= 0x01;
	CHECK(write_file(temp_path, bytes, size));
	error = game_database_open(&database, temp_path, GAME_DATABASE_CORE_CPS2);
	CHECK(error == GAME_DATABASE_OK);
	CHECK(game_database_validate(&database) == GAME_DATABASE_ERROR_CHECKSUM);
	game_database_close(&database);
	bytes[80] ^= 0x01;

	write_u32_le(bytes + 36, read_u32_le(bytes + 36) + 1);
	CHECK(write_file(temp_path, bytes, size));
	error = game_database_open(&database, temp_path, GAME_DATABASE_CORE_CPS2);
	CHECK(error == GAME_DATABASE_ERROR_FORMAT);
	write_u32_le(bytes + 36, read_u32_le(bytes + 36) - 1);

	/* first_region lives 16 bytes into the first 40-byte game record */
	write_u32_le(bytes + 64 + 16, read_u32_le(bytes + 24) + 1);
	update_body_crc(bytes, size);
	CHECK(write_file(temp_path, bytes, size));
	error = game_database_open(&database, temp_path, GAME_DATABASE_CORE_CPS2);
	CHECK(error == GAME_DATABASE_OK);
	CHECK(game_database_get_game(&database, 0, &game) == GAME_DATABASE_ERROR_FORMAT);
	CHECK(game_database_validate(&database) == GAME_DATABASE_ERROR_FORMAT);
	game_database_close(&database);

	CHECK(write_file(temp_path, bytes, 32));
	error = game_database_open(&database, temp_path, GAME_DATABASE_CORE_CPS2);
	CHECK(error == GAME_DATABASE_ERROR_FORMAT);
	CHECK(remove(temp_path) == 0);

	free(bytes);
	game_database_close(&database);
	return 0;
}
