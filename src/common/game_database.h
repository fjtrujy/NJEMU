#ifndef GAME_DATABASE_H
#define GAME_DATABASE_H

#include <stdint.h>
#include <stdio.h>

#define GAME_DATABASE_NAME_BYTES 16
#define GAME_DATABASE_ROM_NAME_BYTES 32
#define GAME_DATABASE_TITLE_BYTES 128
#define GAME_DATABASE_TITLE_COUNT 4

#define GAME_DATABASE_DISPLAY_NOT_WORK 0x01
#define GAME_DATABASE_DISPLAY_BOOTLEG  0x02
#define GAME_DATABASE_DISPLAY_HACK     0x04

#define GAME_DATABASE_CPS2_PHOENIX               0x01
#define GAME_DATABASE_CPS2_CACHE_PARENT_OVERRIDE 0x02
#define GAME_DATABASE_CPS2_CACHE_INDEPENDENT     0x04

#define GAME_DATABASE_ROM_FLAG_ROMX 0x01

typedef enum game_database_core
{
	GAME_DATABASE_CORE_CPS1 = 1,
	GAME_DATABASE_CORE_CPS2 = 2,
	GAME_DATABASE_CORE_MVS = 3
} game_database_core_t;

typedef enum game_database_language
{
	GAME_DATABASE_LANG_ENGLISH = 0,
	GAME_DATABASE_LANG_JAPANESE = 1,
	GAME_DATABASE_LANG_CHINESE_SIMPLIFIED = 2,
	GAME_DATABASE_LANG_CHINESE_TRADITIONAL = 3
} game_database_language_t;

typedef enum game_database_region_type
{
	GAME_DATABASE_REGION_CPU1 = 1,
	GAME_DATABASE_REGION_CPU2 = 2,
	GAME_DATABASE_REGION_GFX1 = 3,
	GAME_DATABASE_REGION_SOUND1 = 4,
	GAME_DATABASE_REGION_USER1 = 5
} game_database_region_type_t;

typedef enum game_database_error
{
	GAME_DATABASE_OK = 0,
	GAME_DATABASE_ERROR_ARGUMENT,
	GAME_DATABASE_ERROR_OPEN,
	GAME_DATABASE_ERROR_READ,
	GAME_DATABASE_ERROR_FORMAT,
	GAME_DATABASE_ERROR_VERSION,
	GAME_DATABASE_ERROR_CORE,
	GAME_DATABASE_ERROR_CHECKSUM,
	GAME_DATABASE_ERROR_NOT_FOUND
} game_database_error_t;

typedef struct game_database
{
	FILE *file;
	uint32_t file_size;
	uint32_t game_count;
	uint32_t region_count;
	uint32_t rom_count;
	uint32_t games_offset;
	uint32_t regions_offset;
	uint32_t roms_offset;
	uint32_t core_offset;
	uint32_t strings_offset;
	uint32_t strings_size;
	uint32_t checksum;
	uint16_t core;
} game_database_t;

typedef struct game_database_game
{
	uint32_t index;
	uint32_t first_region;
	uint32_t data[3];
	uint16_t region_count;
	uint16_t machine;
	uint16_t input;
	uint16_t init;
	uint16_t rotation;
	uint8_t display_flags;
	uint8_t core_flags;
	char name[GAME_DATABASE_NAME_BYTES];
	char parent_name[GAME_DATABASE_NAME_BYTES];
	char title[GAME_DATABASE_TITLE_COUNT][GAME_DATABASE_TITLE_BYTES];
	char aux_name[GAME_DATABASE_NAME_BYTES];
} game_database_game_t;

typedef struct game_database_region
{
	uint32_t index;
	uint32_t size;
	uint32_t first_rom;
	uint16_t flags;
	uint8_t type;
	uint8_t rom_count;
} game_database_region_t;

typedef struct game_database_rom
{
	uint32_t type;
	uint32_t offset;
	uint32_t length;
	uint32_t crc;
	uint8_t group;
	uint8_t skip;
	uint8_t flags;
	char name[GAME_DATABASE_ROM_NAME_BYTES];
} game_database_rom_t;

const char *game_database_filename(void);
game_database_core_t game_database_current_core(void);

game_database_error_t game_database_open(game_database_t *database,
	const char *path, game_database_core_t expected_core);
game_database_error_t game_database_validate(game_database_t *database);
void game_database_close(game_database_t *database);

uint32_t game_database_count(const game_database_t *database);
game_database_error_t game_database_get_game(game_database_t *database,
	uint32_t index, game_database_game_t *game);
game_database_error_t game_database_find_game(game_database_t *database,
	const char *name, game_database_game_t *game);
game_database_error_t game_database_get_region(game_database_t *database,
	const game_database_game_t *game, uint32_t relative_index,
	game_database_region_t *region);
game_database_error_t game_database_get_rom(game_database_t *database,
	const game_database_region_t *region, uint32_t relative_index,
	game_database_rom_t *rom);
const char *game_database_title(const game_database_game_t *game,
	game_database_language_t language);
const char *game_database_error_string(game_database_error_t error);

#endif /* GAME_DATABASE_H */
