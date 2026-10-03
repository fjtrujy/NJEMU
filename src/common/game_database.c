#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "emucfg.h"
#include "common/game_database.h"

#define GAME_DATABASE_MAGIC_SIZE 4u
#define GAME_DATABASE_HEADER_SIZE 64u
#define GAME_DATABASE_GAME_RECORD_SIZE 40u
#define GAME_DATABASE_REGION_RECORD_SIZE 16u
#define GAME_DATABASE_ROM_RECORD_SIZE 20u
#define GAME_DATABASE_CPS2_RECORD_SIZE 16u
#define GAME_DATABASE_PARENT_NONE 0xffffu
#define GAME_DATABASE_CRC_CHUNK 4096u

static const uint8_t game_database_magic[GAME_DATABASE_MAGIC_SIZE] = {'N', 'J', 'G', 'D'};

static uint16_t read_u16_le(const uint8_t *p)
{
	return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *p)
{
	return (uint32_t)p[0]
		| ((uint32_t)p[1] << 8)
		| ((uint32_t)p[2] << 16)
		| ((uint32_t)p[3] << 24);
}

static int section_end(uint32_t offset, uint32_t count, uint32_t record_size,
	uint32_t *end)
{
	uint64_t value = (uint64_t)offset + (uint64_t)count * record_size;
	if (value > UINT32_MAX)
		return 0;
	*end = (uint32_t)value;
	return 1;
}

static game_database_error_t read_at(game_database_t *database, uint32_t offset,
	void *buffer, size_t size)
{
	if (database == NULL || database->file == NULL || buffer == NULL)
		return GAME_DATABASE_ERROR_ARGUMENT;
	if (offset > database->file_size || size > database->file_size - offset)
		return GAME_DATABASE_ERROR_FORMAT;
	if (offset > (uint32_t)LONG_MAX || fseek(database->file, (long)offset, SEEK_SET) != 0)
		return GAME_DATABASE_ERROR_READ;
	if (fread(buffer, 1, size, database->file) != size)
		return GAME_DATABASE_ERROR_READ;
	return GAME_DATABASE_OK;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
	size_t i;
	int bit;
	for (i = 0; i < size; i++)
	{
		crc ^= data[i];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
	}
	return crc;
}

static game_database_error_t validate_checksum(game_database_t *database,
	uint32_t expected_crc)
{
	uint8_t buffer[GAME_DATABASE_CRC_CHUNK];
	uint32_t remaining = database->file_size - GAME_DATABASE_HEADER_SIZE;
	uint32_t crc = 0xffffffffu;

	if (GAME_DATABASE_HEADER_SIZE > (uint32_t)LONG_MAX
		|| fseek(database->file, GAME_DATABASE_HEADER_SIZE, SEEK_SET) != 0)
		return GAME_DATABASE_ERROR_READ;
	while (remaining != 0)
	{
		size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
		if (fread(buffer, 1, chunk, database->file) != chunk)
			return GAME_DATABASE_ERROR_READ;
		crc = crc32_update(crc, buffer, chunk);
		remaining -= (uint32_t)chunk;
	}
	crc ^= 0xffffffffu;
	return crc == expected_crc ? GAME_DATABASE_OK : GAME_DATABASE_ERROR_CHECKSUM;
}

static game_database_error_t read_string(game_database_t *database,
	uint32_t offset, char *buffer, size_t capacity)
{
	uint32_t available;
	size_t read_size;
	void *terminator;
	game_database_error_t error;

	if (buffer == NULL || capacity == 0)
		return GAME_DATABASE_ERROR_ARGUMENT;
	buffer[0] = '\0';
	if (offset == 0)
		return GAME_DATABASE_OK;
	if (offset >= database->strings_size)
		return GAME_DATABASE_ERROR_FORMAT;
	available = database->strings_size - offset;
	read_size = available < capacity ? available : capacity;
	error = read_at(database, database->strings_offset + offset, buffer, read_size);
	if (error != GAME_DATABASE_OK)
		return error;
	terminator = memchr(buffer, '\0', read_size);
	if (terminator == NULL)
	{
		buffer[0] = '\0';
		return GAME_DATABASE_ERROR_FORMAT;
	}
	return GAME_DATABASE_OK;
}

static game_database_error_t read_game_record(game_database_t *database,
	uint32_t index, uint8_t record[GAME_DATABASE_GAME_RECORD_SIZE])
{
	uint32_t offset;
	if (index >= database->game_count)
		return GAME_DATABASE_ERROR_NOT_FOUND;
	offset = database->games_offset + index * GAME_DATABASE_GAME_RECORD_SIZE;
	return read_at(database, offset, record, GAME_DATABASE_GAME_RECORD_SIZE);
}

static game_database_error_t read_game_name(game_database_t *database,
	uint32_t index, char name[GAME_DATABASE_NAME_BYTES])
{
	uint8_t record[GAME_DATABASE_GAME_RECORD_SIZE];
	game_database_error_t error = read_game_record(database, index, record);
	if (error != GAME_DATABASE_OK)
		return error;
	error = read_string(database, read_u32_le(record), name, GAME_DATABASE_NAME_BYTES);
	if (error != GAME_DATABASE_OK)
		return error;
	return name[0] != '\0' ? GAME_DATABASE_OK : GAME_DATABASE_ERROR_FORMAT;
}

static game_database_error_t validate_game_index(game_database_t *database)
{
	char previous[GAME_DATABASE_NAME_BYTES] = {0};
	char current[GAME_DATABASE_NAME_BYTES];
	uint32_t i;

	for (i = 0; i < database->game_count; i++)
	{
		uint8_t record[GAME_DATABASE_GAME_RECORD_SIZE];
		uint16_t parent_index, core_index;
		uint32_t first_region;
		uint8_t region_count;
		game_database_error_t error = read_game_record(database, i, record);
		if (error != GAME_DATABASE_OK)
			return error;
		if (record[37] != 0)
			return GAME_DATABASE_ERROR_FORMAT;
		parent_index = read_u16_le(record + 4);
		region_count = record[6];
		first_region = read_u32_le(record + 16);
		core_index = read_u16_le(record + 38);
		if ((parent_index != GAME_DATABASE_PARENT_NONE && parent_index >= database->game_count)
			|| parent_index == i || core_index != i
			|| first_region > database->region_count
			|| region_count > database->region_count - first_region)
			return GAME_DATABASE_ERROR_FORMAT;
		error = read_string(database, read_u32_le(record), current, sizeof(current));
		if (error != GAME_DATABASE_OK || current[0] == '\0')
			return GAME_DATABASE_ERROR_FORMAT;
		if (i != 0 && strcmp(previous, current) >= 0)
			return GAME_DATABASE_ERROR_FORMAT;
		memcpy(previous, current, sizeof(previous));
	}
	return GAME_DATABASE_OK;
}

const char *game_database_filename(void)
{
#if (EMU_SYSTEM == CPS1)
	return "game_database.cps1";
#elif (EMU_SYSTEM == CPS2)
	return "game_database.cps2";
#elif (EMU_SYSTEM == MVS)
	return "game_database.mvs";
#else
#error Unsupported EMU_SYSTEM for cartridge game database
#endif
}

game_database_core_t game_database_current_core(void)
{
#if (EMU_SYSTEM == CPS1)
	return GAME_DATABASE_CORE_CPS1;
#elif (EMU_SYSTEM == CPS2)
	return GAME_DATABASE_CORE_CPS2;
#elif (EMU_SYSTEM == MVS)
	return GAME_DATABASE_CORE_MVS;
#else
#error Unsupported EMU_SYSTEM for cartridge game database
#endif
}

void game_database_close(game_database_t *database)
{
	if (database == NULL)
		return;
	if (database->file != NULL)
		fclose(database->file);
	memset(database, 0, sizeof(*database));
}

game_database_error_t game_database_open(game_database_t *database,
	const char *path, game_database_core_t expected_core)
{
	FILE *file;
	long file_size;
	uint8_t header[GAME_DATABASE_HEADER_SIZE];
	uint16_t version, core, header_size, game_record_size, region_record_size;
	uint16_t rom_record_size, core_record_size, reserved;
	uint32_t game_count, region_count, rom_count;
	uint32_t games_offset, regions_offset, roms_offset, core_offset;
	uint32_t strings_offset, strings_size, expected_crc, encoded_file_size;
	uint32_t expected_offset;
	uint8_t first_string;
	game_database_error_t error;

	if (database == NULL || path == NULL)
		return GAME_DATABASE_ERROR_ARGUMENT;
	memset(database, 0, sizeof(*database));
	file = fopen(path, "rb");
	if (file == NULL)
		return GAME_DATABASE_ERROR_OPEN;
	if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0
		|| fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return GAME_DATABASE_ERROR_READ;
	}
	if ((unsigned long)file_size < GAME_DATABASE_HEADER_SIZE
		|| (unsigned long)file_size > UINT32_MAX)
	{
		fclose(file);
		return GAME_DATABASE_ERROR_FORMAT;
	}
	if (fread(header, 1, sizeof(header), file) != sizeof(header))
	{
		fclose(file);
		return GAME_DATABASE_ERROR_READ;
	}
	if (memcmp(header, game_database_magic, GAME_DATABASE_MAGIC_SIZE) != 0)
	{
		fclose(file);
		return GAME_DATABASE_ERROR_FORMAT;
	}

	version = read_u16_le(header + 4);
	core = read_u16_le(header + 6);
	header_size = read_u16_le(header + 8);
	game_record_size = read_u16_le(header + 10);
	region_record_size = read_u16_le(header + 12);
	rom_record_size = read_u16_le(header + 14);
	core_record_size = read_u16_le(header + 16);
	reserved = read_u16_le(header + 18);
	game_count = read_u32_le(header + 20);
	region_count = read_u32_le(header + 24);
	rom_count = read_u32_le(header + 28);
	games_offset = read_u32_le(header + 32);
	regions_offset = read_u32_le(header + 36);
	roms_offset = read_u32_le(header + 40);
	core_offset = read_u32_le(header + 44);
	strings_offset = read_u32_le(header + 48);
	strings_size = read_u32_le(header + 52);
	expected_crc = read_u32_le(header + 56);
	encoded_file_size = read_u32_le(header + 60);

	if (version != 1)
	{
		fclose(file);
		return GAME_DATABASE_ERROR_VERSION;
	}
	if (core != (uint16_t)expected_core || core != GAME_DATABASE_CORE_CPS2)
	{
		fclose(file);
		return GAME_DATABASE_ERROR_CORE;
	}
	if (header_size != GAME_DATABASE_HEADER_SIZE
		|| game_record_size != GAME_DATABASE_GAME_RECORD_SIZE
		|| region_record_size != GAME_DATABASE_REGION_RECORD_SIZE
		|| rom_record_size != GAME_DATABASE_ROM_RECORD_SIZE
		|| core_record_size != GAME_DATABASE_CPS2_RECORD_SIZE
		|| reserved != 0 || game_count == 0 || game_count >= GAME_DATABASE_PARENT_NONE
		|| encoded_file_size != (uint32_t)file_size || strings_size == 0
		|| games_offset != GAME_DATABASE_HEADER_SIZE)
	{
		fclose(file);
		return GAME_DATABASE_ERROR_FORMAT;
	}
	if (!section_end(games_offset, game_count, GAME_DATABASE_GAME_RECORD_SIZE, &expected_offset)
		|| regions_offset != expected_offset
		|| !section_end(regions_offset, region_count, GAME_DATABASE_REGION_RECORD_SIZE, &expected_offset)
		|| roms_offset != expected_offset
		|| !section_end(roms_offset, rom_count, GAME_DATABASE_ROM_RECORD_SIZE, &expected_offset)
		|| core_offset != expected_offset
		|| !section_end(core_offset, game_count, GAME_DATABASE_CPS2_RECORD_SIZE, &expected_offset)
		|| strings_offset != expected_offset
		|| strings_offset > encoded_file_size
		|| strings_size != encoded_file_size - strings_offset)
	{
		fclose(file);
		return GAME_DATABASE_ERROR_FORMAT;
	}

	database->file = file;
	database->file_size = encoded_file_size;
	database->game_count = game_count;
	database->region_count = region_count;
	database->rom_count = rom_count;
	database->games_offset = games_offset;
	database->regions_offset = regions_offset;
	database->roms_offset = roms_offset;
	database->core_offset = core_offset;
	database->strings_offset = strings_offset;
	database->strings_size = strings_size;
	database->checksum = expected_crc;
	database->core = core;

	error = read_at(database, strings_offset, &first_string, 1);
	if (error != GAME_DATABASE_OK || first_string != '\0')
	{
		game_database_close(database);
		return error != GAME_DATABASE_OK ? error : GAME_DATABASE_ERROR_FORMAT;
	}
	return GAME_DATABASE_OK;
}

game_database_error_t game_database_validate(game_database_t *database)
{
	game_database_error_t error;

	if (database == NULL || database->file == NULL)
		return GAME_DATABASE_ERROR_ARGUMENT;
	error = validate_checksum(database, database->checksum);
	if (error != GAME_DATABASE_OK)
		return error;
	return validate_game_index(database);
}

uint32_t game_database_count(const game_database_t *database)
{
	return database != NULL && database->file != NULL ? database->game_count : 0;
}

game_database_error_t game_database_get_game(game_database_t *database,
	uint32_t index, game_database_game_t *game)
{
	uint8_t record[GAME_DATABASE_GAME_RECORD_SIZE];
	uint8_t core_record[GAME_DATABASE_CPS2_RECORD_SIZE];
	uint16_t parent_index, core_index;
	uint32_t i;
	game_database_error_t error;

	if (database == NULL || database->file == NULL || game == NULL)
		return GAME_DATABASE_ERROR_ARGUMENT;
	error = read_game_record(database, index, record);
	if (error != GAME_DATABASE_OK)
		return error;
	memset(game, 0, sizeof(*game));
	game->index = index;
	parent_index = read_u16_le(record + 4);
	game->region_count = record[6];
	game->display_flags = record[7];
	game->machine = read_u16_le(record + 8);
	game->input = read_u16_le(record + 10);
	game->init = read_u16_le(record + 12);
	game->rotation = read_u16_le(record + 14);
	game->first_region = read_u32_le(record + 16);
	game->core_flags = record[36];
	core_index = read_u16_le(record + 38);
	if (record[37] != 0 || core_index != index
		|| game->first_region > database->region_count
		|| game->region_count > database->region_count - game->first_region)
		return GAME_DATABASE_ERROR_FORMAT;

	error = read_string(database, read_u32_le(record), game->name, sizeof(game->name));
	if (error != GAME_DATABASE_OK || game->name[0] == '\0')
		return error != GAME_DATABASE_OK ? error : GAME_DATABASE_ERROR_FORMAT;
	if (parent_index != GAME_DATABASE_PARENT_NONE)
	{
		if (parent_index >= database->game_count || parent_index == index)
			return GAME_DATABASE_ERROR_FORMAT;
		error = read_game_name(database, parent_index, game->parent_name);
		if (error != GAME_DATABASE_OK)
			return error;
	}
	for (i = 0; i < GAME_DATABASE_TITLE_COUNT; i++)
	{
		error = read_string(database, read_u32_le(record + 20u + i * 4u),
			game->title[i], sizeof(game->title[i]));
		if (error != GAME_DATABASE_OK)
			return error;
	}

	error = read_at(database, database->core_offset + core_index * GAME_DATABASE_CPS2_RECORD_SIZE,
		core_record, sizeof(core_record));
	if (error != GAME_DATABASE_OK)
		return error;
	game->data[0] = read_u32_le(core_record);
	game->data[1] = read_u32_le(core_record + 4);
	game->data[2] = read_u32_le(core_record + 8);
	return read_string(database, read_u32_le(core_record + 12),
		game->aux_name, sizeof(game->aux_name));
}

game_database_error_t game_database_find_game(game_database_t *database,
	const char *name, game_database_game_t *game)
{
	uint32_t lo, hi;
	if (database == NULL || database->file == NULL || name == NULL || game == NULL)
		return GAME_DATABASE_ERROR_ARGUMENT;
	lo = 0;
	hi = database->game_count;
	while (lo < hi)
	{
		uint32_t mid = lo + (hi - lo) / 2;
		char candidate[GAME_DATABASE_NAME_BYTES];
		int comparison;
		game_database_error_t error = read_game_name(database, mid, candidate);
		if (error != GAME_DATABASE_OK)
			return error;
		comparison = strcmp(name, candidate);
		if (comparison == 0)
			return game_database_get_game(database, mid, game);
		if (comparison < 0)
			hi = mid;
		else
			lo = mid + 1;
	}
	return GAME_DATABASE_ERROR_NOT_FOUND;
}

game_database_error_t game_database_get_region(game_database_t *database,
	const game_database_game_t *game, uint32_t relative_index,
	game_database_region_t *region)
{
	uint8_t record[GAME_DATABASE_REGION_RECORD_SIZE];
	uint32_t index;
	uint16_t game_index;
	game_database_error_t error;

	if (database == NULL || database->file == NULL || game == NULL || region == NULL)
		return GAME_DATABASE_ERROR_ARGUMENT;
	if (relative_index >= game->region_count)
		return GAME_DATABASE_ERROR_NOT_FOUND;
	if (game->first_region > database->region_count
		|| game->region_count > database->region_count - game->first_region)
		return GAME_DATABASE_ERROR_FORMAT;
	index = game->first_region + relative_index;
	error = read_at(database, database->regions_offset + index * GAME_DATABASE_REGION_RECORD_SIZE,
		record, sizeof(record));
	if (error != GAME_DATABASE_OK)
		return error;
	game_index = read_u16_le(record);
	memset(region, 0, sizeof(*region));
	region->index = index;
	region->type = record[2];
	region->rom_count = record[3];
	region->size = read_u32_le(record + 4);
	region->first_rom = read_u32_le(record + 8);
	region->flags = read_u16_le(record + 12);
	if (game_index != game->index || record[14] != 0 || record[15] != 0
		|| region->type < GAME_DATABASE_REGION_CPU1 || region->type > GAME_DATABASE_REGION_USER1
		|| region->first_rom > database->rom_count
		|| region->rom_count > database->rom_count - region->first_rom)
		return GAME_DATABASE_ERROR_FORMAT;
	return GAME_DATABASE_OK;
}

game_database_error_t game_database_get_rom(game_database_t *database,
	const game_database_region_t *region, uint32_t relative_index,
	game_database_rom_t *rom)
{
	uint8_t record[GAME_DATABASE_ROM_RECORD_SIZE];
	uint32_t index, name_offset;
	game_database_error_t error;

	if (database == NULL || database->file == NULL || region == NULL || rom == NULL)
		return GAME_DATABASE_ERROR_ARGUMENT;
	if (relative_index >= region->rom_count)
		return GAME_DATABASE_ERROR_NOT_FOUND;
	if (region->first_rom > database->rom_count
		|| region->rom_count > database->rom_count - region->first_rom)
		return GAME_DATABASE_ERROR_FORMAT;
	index = region->first_rom + relative_index;
	error = read_at(database, database->roms_offset + index * GAME_DATABASE_ROM_RECORD_SIZE,
		record, sizeof(record));
	if (error != GAME_DATABASE_OK)
		return error;
	memset(rom, 0, sizeof(*rom));
	name_offset = read_u32_le(record);
	rom->offset = read_u32_le(record + 4);
	rom->length = read_u32_le(record + 8);
	rom->crc = read_u32_le(record + 12);
	rom->type = record[16];
	rom->group = record[17];
	rom->skip = record[18];
	rom->flags = record[19];
	if (rom->flags & ~GAME_DATABASE_ROM_FLAG_ROMX)
		return GAME_DATABASE_ERROR_FORMAT;
	if ((rom->type == 1 && name_offset != 0) || (rom->type != 1 && name_offset == 0))
		return GAME_DATABASE_ERROR_FORMAT;
	return read_string(database, name_offset, rom->name, sizeof(rom->name));
}

const char *game_database_title(const game_database_game_t *game,
	game_database_language_t language)
{
	if (game == NULL)
		return NULL;
	if ((unsigned int)language < GAME_DATABASE_TITLE_COUNT
		&& game->title[language][0] != '\0')
		return game->title[language];
	return game->title[GAME_DATABASE_LANG_ENGLISH][0] != '\0'
		? game->title[GAME_DATABASE_LANG_ENGLISH] : NULL;
}

const char *game_database_error_string(game_database_error_t error)
{
	switch (error)
	{
	case GAME_DATABASE_OK: return "ok";
	case GAME_DATABASE_ERROR_ARGUMENT: return "invalid argument";
	case GAME_DATABASE_ERROR_OPEN: return "file not found";
	case GAME_DATABASE_ERROR_READ: return "file read failed";
	case GAME_DATABASE_ERROR_FORMAT: return "invalid format";
	case GAME_DATABASE_ERROR_VERSION: return "unsupported version";
	case GAME_DATABASE_ERROR_CORE: return "wrong emulator core";
	case GAME_DATABASE_ERROR_CHECKSUM: return "checksum mismatch";
	case GAME_DATABASE_ERROR_NOT_FOUND: return "game or record not found";
	default: return "unknown error";
	}
}
