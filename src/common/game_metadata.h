#ifndef GAME_METADATA_H
#define GAME_METADATA_H

#include <stddef.h>
#include <stdint.h>

#define GAME_METADATA_NAME_BYTES 16
#define GAME_METADATA_TITLE_COUNT 4

#define GAME_METADATA_DISPLAY_NOT_WORK 0x01
#define GAME_METADATA_DISPLAY_BOOTLEG  0x02
#define GAME_METADATA_DISPLAY_HACK     0x04

#define GAME_METADATA_CPS2_PHOENIX               0x01
#define GAME_METADATA_CPS2_CACHE_PARENT_OVERRIDE 0x02
#define GAME_METADATA_CPS2_CACHE_INDEPENDENT     0x04

#define GAME_METADATA_MVS_OWNS_CROM 0x01
#define GAME_METADATA_MVS_OWNS_SROM 0x02
#define GAME_METADATA_MVS_OWNS_VROM 0x04

typedef enum game_metadata_core
{
	GAME_METADATA_CORE_CPS1 = 1,
	GAME_METADATA_CORE_CPS2 = 2,
	GAME_METADATA_CORE_MVS = 3,
	GAME_METADATA_CORE_NCDZ = 4
} game_metadata_core_t;

typedef enum game_metadata_language
{
	GAME_METADATA_LANG_ENGLISH = 0,
	GAME_METADATA_LANG_JAPANESE = 1,
	GAME_METADATA_LANG_CHINESE_SIMPLIFIED = 2,
	GAME_METADATA_LANG_CHINESE_TRADITIONAL = 3
} game_metadata_language_t;

typedef enum game_metadata_error
{
	GAME_METADATA_OK = 0,
	GAME_METADATA_ERROR_ARGUMENT,
	GAME_METADATA_ERROR_OPEN,
	GAME_METADATA_ERROR_READ,
	GAME_METADATA_ERROR_MEMORY,
	GAME_METADATA_ERROR_FORMAT,
	GAME_METADATA_ERROR_VERSION,
	GAME_METADATA_ERROR_CORE,
	GAME_METADATA_ERROR_CHECKSUM
} game_metadata_error_t;

typedef struct game_metadata
{
	uint8_t *data;
	size_t size;
	uint32_t count;
	uint32_t records_offset;
	uint32_t strings_offset;
	uint32_t strings_size;
	uint16_t core;
} game_metadata_t;

typedef struct game_metadata_entry
{
	char name[GAME_METADATA_NAME_BYTES];
	const char *title[GAME_METADATA_TITLE_COUNT];
	const char *aux_name;
	uint32_t data[3];
	uint8_t display_flags;
	uint8_t core_flags;
} game_metadata_entry_t;

const char *game_metadata_filename(void);
game_metadata_core_t game_metadata_current_core(void);

game_metadata_error_t game_metadata_load(game_metadata_t *metadata,
	const char *path, game_metadata_core_t expected_core);
void game_metadata_unload(game_metadata_t *metadata);

uint32_t game_metadata_count(const game_metadata_t *metadata);
int game_metadata_get(const game_metadata_t *metadata, uint32_t index,
	game_metadata_entry_t *entry);
int game_metadata_find(const game_metadata_t *metadata, const char *name,
	game_metadata_entry_t *entry);
int game_metadata_find_ngh(const game_metadata_t *metadata, uint16_t ngh,
	game_metadata_entry_t *entry);
const char *game_metadata_title(const game_metadata_entry_t *entry,
	game_metadata_language_t language);
const char *game_metadata_error_string(game_metadata_error_t error);

#endif /* GAME_METADATA_H */
