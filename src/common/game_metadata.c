#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emucfg.h"
#include "common/game_metadata.h"

#define GAME_METADATA_MAGIC_SIZE 4
#define GAME_METADATA_HEADER_SIZE 32u
#define GAME_METADATA_RECORD_SIZE 52u
#define GAME_METADATA_MAX_RECORDS 4096u
#define GAME_METADATA_MAX_FILE_SIZE (1024u * 1024u)

static const uint8_t game_metadata_magic[GAME_METADATA_MAGIC_SIZE] = {'N', 'J', 'G', 'M'};

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

static uint32_t metadata_crc32(const uint8_t *data, size_t size)
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

static const char *metadata_string(const game_metadata_t *metadata, uint32_t offset)
{
	const char *value;
	size_t available;

	if (offset == 0)
		return NULL;
	if (offset >= metadata->strings_size)
		return NULL;
	value = (const char *)(metadata->data + metadata->strings_offset + offset);
	available = metadata->strings_size - offset;
	if (memchr(value, '\0', available) == NULL)
		return NULL;
	return value;
}

static int metadata_record_valid(const game_metadata_t *metadata, uint32_t index)
{
	const uint8_t *record;
	uint32_t i;
	const char *name;

	if (index >= metadata->count)
		return 0;
	record = metadata->data + metadata->records_offset + index * GAME_METADATA_RECORD_SIZE;
	name = (const char *)record;
	if (record[0] == '\0' || memchr(name, '\0', GAME_METADATA_NAME_BYTES) == NULL)
		return 0;

	for (i = 0; i < 5; i++)
	{
		uint32_t offset = read_u32_le(record + 16u + i * 4u);
		if (offset != 0 && metadata_string(metadata, offset) == NULL)
			return 0;
	}
	return read_u16_le(record + 50u) == 0;
}

const char *game_metadata_filename(void)
{
#if (EMU_SYSTEM == CPS1)
	return "game_metadata.cps1";
#elif (EMU_SYSTEM == CPS2)
	return "game_metadata.cps2";
#elif (EMU_SYSTEM == MVS)
	return "game_metadata.mvs";
#elif (EMU_SYSTEM == NCDZ)
	return "game_metadata.ncdz";
#else
#error Unsupported EMU_SYSTEM
#endif
}

game_metadata_core_t game_metadata_current_core(void)
{
#if (EMU_SYSTEM == CPS1)
	return GAME_METADATA_CORE_CPS1;
#elif (EMU_SYSTEM == CPS2)
	return GAME_METADATA_CORE_CPS2;
#elif (EMU_SYSTEM == MVS)
	return GAME_METADATA_CORE_MVS;
#elif (EMU_SYSTEM == NCDZ)
	return GAME_METADATA_CORE_NCDZ;
#else
#error Unsupported EMU_SYSTEM
#endif
}

void game_metadata_unload(game_metadata_t *metadata)
{
	if (metadata == NULL)
		return;
	free(metadata->data);
	memset(metadata, 0, sizeof(*metadata));
}

game_metadata_error_t game_metadata_load(game_metadata_t *metadata,
	const char *path, game_metadata_core_t expected_core)
{
	FILE *file;
	long file_size;
	size_t read_size;
	uint8_t *data;
	uint16_t version, core;
	uint32_t count, record_size, records_offset, strings_offset, strings_size;
	uint32_t expected_crc, body_size;
	uint32_t i;
	game_metadata_t loaded;
	char previous_name[GAME_METADATA_NAME_BYTES] = {0};

	if (metadata == NULL || path == NULL)
		return GAME_METADATA_ERROR_ARGUMENT;
	game_metadata_unload(metadata);

	file = fopen(path, "rb");
	if (file == NULL)
		return GAME_METADATA_ERROR_OPEN;
	if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0
		|| fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return GAME_METADATA_ERROR_READ;
	}
	if ((unsigned long)file_size < GAME_METADATA_HEADER_SIZE
		|| (unsigned long)file_size > GAME_METADATA_MAX_FILE_SIZE)
	{
		fclose(file);
		return GAME_METADATA_ERROR_FORMAT;
	}

	data = (uint8_t *)malloc((size_t)file_size);
	if (data == NULL)
	{
		fclose(file);
		return GAME_METADATA_ERROR_MEMORY;
	}
	read_size = fread(data, 1, (size_t)file_size, file);
	if (fclose(file) != 0 || read_size != (size_t)file_size)
	{
		free(data);
		return GAME_METADATA_ERROR_READ;
	}

	if (memcmp(data, game_metadata_magic, GAME_METADATA_MAGIC_SIZE) != 0)
	{
		free(data);
		return GAME_METADATA_ERROR_FORMAT;
	}
	version = read_u16_le(data + 4);
	core = read_u16_le(data + 6);
	count = read_u32_le(data + 8);
	record_size = read_u32_le(data + 12);
	records_offset = read_u32_le(data + 16);
	strings_offset = read_u32_le(data + 20);
	strings_size = read_u32_le(data + 24);
	expected_crc = read_u32_le(data + 28);

	if (version != 1)
	{
		free(data);
		return GAME_METADATA_ERROR_VERSION;
	}
	if (core != (uint16_t)expected_core)
	{
		free(data);
		return GAME_METADATA_ERROR_CORE;
	}
	if (count == 0 || count > GAME_METADATA_MAX_RECORDS
		|| record_size != GAME_METADATA_RECORD_SIZE
		|| records_offset != GAME_METADATA_HEADER_SIZE
		|| strings_size == 0
		|| count > (UINT32_MAX - records_offset) / GAME_METADATA_RECORD_SIZE
		|| strings_offset != records_offset + count * GAME_METADATA_RECORD_SIZE
		|| strings_offset > (uint32_t)file_size
		|| strings_size != (uint32_t)file_size - strings_offset
		|| data[strings_offset] != '\0')
	{
		free(data);
		return GAME_METADATA_ERROR_FORMAT;
	}
	body_size = (uint32_t)file_size - GAME_METADATA_HEADER_SIZE;
	if (metadata_crc32(data + GAME_METADATA_HEADER_SIZE, body_size) != expected_crc)
	{
		free(data);
		return GAME_METADATA_ERROR_CHECKSUM;
	}

	memset(&loaded, 0, sizeof(loaded));
	loaded.data = data;
	loaded.size = (size_t)file_size;
	loaded.count = count;
	loaded.records_offset = records_offset;
	loaded.strings_offset = strings_offset;
	loaded.strings_size = strings_size;
	loaded.core = core;

	for (i = 0; i < count; i++)
	{
		const char *name;
		const uint8_t *record = data + records_offset + i * GAME_METADATA_RECORD_SIZE;

		if (!metadata_record_valid(&loaded, i))
		{
			free(data);
			return GAME_METADATA_ERROR_FORMAT;
		}
		name = (const char *)record;
		if (i != 0 && strcmp(previous_name, name) >= 0)
		{
			free(data);
			return GAME_METADATA_ERROR_FORMAT;
		}
		memset(previous_name, 0, sizeof(previous_name));
		memcpy(previous_name, name, strlen(name) + 1);
	}

	*metadata = loaded;
	return GAME_METADATA_OK;
}

uint32_t game_metadata_count(const game_metadata_t *metadata)
{
	return metadata != NULL ? metadata->count : 0;
}

int game_metadata_get(const game_metadata_t *metadata, uint32_t index,
	game_metadata_entry_t *entry)
{
	const uint8_t *record;
	uint32_t i;

	if (metadata == NULL || metadata->data == NULL || entry == NULL || index >= metadata->count)
		return 0;
	record = metadata->data + metadata->records_offset + index * GAME_METADATA_RECORD_SIZE;
	if (!metadata_record_valid(metadata, index))
		return 0;

	memset(entry, 0, sizeof(*entry));
	memcpy(entry->name, record, GAME_METADATA_NAME_BYTES);
	entry->name[GAME_METADATA_NAME_BYTES - 1] = '\0';
	for (i = 0; i < GAME_METADATA_TITLE_COUNT; i++)
		entry->title[i] = metadata_string(metadata, read_u32_le(record + 16u + i * 4u));
	entry->aux_name = metadata_string(metadata, read_u32_le(record + 32u));
	entry->data[0] = read_u32_le(record + 36u);
	entry->data[1] = read_u32_le(record + 40u);
	entry->data[2] = read_u32_le(record + 44u);
	entry->display_flags = record[48];
	entry->core_flags = record[49];
	return 1;
}

int game_metadata_find(const game_metadata_t *metadata, const char *name,
	game_metadata_entry_t *entry)
{
	uint32_t lo, hi;

	if (metadata == NULL || metadata->data == NULL || name == NULL)
		return 0;
	lo = 0;
	hi = metadata->count;
	while (lo < hi)
	{
		uint32_t mid = lo + (hi - lo) / 2;
		const char *candidate = (const char *)(metadata->data
			+ metadata->records_offset + mid * GAME_METADATA_RECORD_SIZE);
		int comparison = strcmp(name, candidate);

		if (comparison == 0)
			return game_metadata_get(metadata, mid, entry);
		if (comparison < 0)
			hi = mid;
		else
			lo = mid + 1;
	}
	return 0;
}

int game_metadata_find_ngh(const game_metadata_t *metadata, uint16_t ngh,
	game_metadata_entry_t *entry)
{
	uint32_t i;
	game_metadata_entry_t candidate;

	if (metadata == NULL || metadata->core != GAME_METADATA_CORE_NCDZ)
		return 0;
	for (i = 0; i < metadata->count; i++)
	{
		if (!game_metadata_get(metadata, i, &candidate))
			return 0;
		if (candidate.data[0] == ngh)
		{
			if (entry != NULL)
				*entry = candidate;
			return 1;
		}
	}
	return 0;
}

const char *game_metadata_title(const game_metadata_entry_t *entry,
	game_metadata_language_t language)
{
	if (entry == NULL)
		return NULL;
	if ((unsigned int)language < GAME_METADATA_TITLE_COUNT && entry->title[language] != NULL)
		return entry->title[language];
	return entry->title[GAME_METADATA_LANG_ENGLISH];
}

const char *game_metadata_error_string(game_metadata_error_t error)
{
	switch (error)
	{
	case GAME_METADATA_OK: return "ok";
	case GAME_METADATA_ERROR_ARGUMENT: return "invalid argument";
	case GAME_METADATA_ERROR_OPEN: return "file not found";
	case GAME_METADATA_ERROR_READ: return "file read failed";
	case GAME_METADATA_ERROR_MEMORY: return "out of memory";
	case GAME_METADATA_ERROR_FORMAT: return "invalid format";
	case GAME_METADATA_ERROR_VERSION: return "unsupported version";
	case GAME_METADATA_ERROR_CORE: return "wrong emulator core";
	case GAME_METADATA_ERROR_CHECKSUM: return "checksum mismatch";
	default: return "unknown error";
	}
}
