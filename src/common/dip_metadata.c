#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common/dip_metadata.h"

#define DIP_METADATA_MAGIC_SIZE 4u
#define DIP_METADATA_HEADER_SIZE 40u
#define DIP_METADATA_DIRECTORY_SIZE 28u
#define DIP_METADATA_ROW_SIZE 16u
#define DIP_METADATA_MAX_FILE_SIZE (512u * 1024u)
#define DIP_METADATA_PROFILE_NAME_SIZE 16u
#define DIP_METADATA_VERSION 1u

static const uint8_t dip_metadata_magic[DIP_METADATA_MAGIC_SIZE] = {'N', 'J', 'D', 'P'};

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

static uint32_t dip_metadata_crc32(const uint8_t *data, size_t size)
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

static const char *dip_metadata_string(const dip_metadata_t *metadata, uint32_t offset)
{
	const char *value;
	size_t available;

	if (metadata == NULL || metadata->data == NULL || offset >= metadata->strings_size)
		return NULL;
	value = (const char *)(metadata->data + metadata->strings_offset + offset);
	available = metadata->strings_size - offset;
	if (memchr(value, '\0', available) == NULL)
		return NULL;
	return value;
}

static int range_fits(uint32_t offset, uint32_t count, uint32_t item_size, uint32_t limit)
{
	if (offset > limit || item_size == 0)
		return 0;
	if (count > (UINT32_MAX - offset) / item_size)
		return 0;
	return offset + count * item_size <= limit;
}

void dip_metadata_unload(dip_metadata_t *metadata)
{
	if (metadata == NULL)
		return;
	free(metadata->active);
	free(metadata->data);
	memset(metadata, 0, sizeof(*metadata));
}

static dip_metadata_error_t load_file(dip_metadata_t *metadata, const char *path)
{
	FILE *file;
	long file_size;
	size_t read_size;
	uint8_t *data;
	uint16_t version, language_count;
	uint32_t profile_count, directory_count;
	uint32_t directory_offset, rows_offset, choices_offset, strings_offset, strings_size;
	uint32_t expected_crc;
	uint32_t row_count, choice_count;
	uint32_t i;

	file = fopen(path, "rb");
	if (file == NULL)
		return DIP_METADATA_ERROR_OPEN;
	if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0
		|| fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return DIP_METADATA_ERROR_READ;
	}
	if ((unsigned long)file_size < DIP_METADATA_HEADER_SIZE
		|| (unsigned long)file_size > DIP_METADATA_MAX_FILE_SIZE)
	{
		fclose(file);
		return DIP_METADATA_ERROR_FORMAT;
	}
	data = (uint8_t *)malloc((size_t)file_size);
	if (data == NULL)
	{
		fclose(file);
		return DIP_METADATA_ERROR_MEMORY;
	}
	read_size = fread(data, 1, (size_t)file_size, file);
	if (fclose(file) != 0 || read_size != (size_t)file_size)
	{
		free(data);
		return DIP_METADATA_ERROR_READ;
	}

	if (memcmp(data, dip_metadata_magic, DIP_METADATA_MAGIC_SIZE) != 0)
	{
		free(data);
		return DIP_METADATA_ERROR_FORMAT;
	}
	version = read_u16_le(data + 4);
	language_count = read_u16_le(data + 6);
	profile_count = read_u32_le(data + 8);
	directory_count = read_u32_le(data + 12);
	directory_offset = read_u32_le(data + 16);
	rows_offset = read_u32_le(data + 20);
	choices_offset = read_u32_le(data + 24);
	strings_offset = read_u32_le(data + 28);
	strings_size = read_u32_le(data + 32);
	expected_crc = read_u32_le(data + 36);

	if (version != DIP_METADATA_VERSION)
	{
		free(data);
		return DIP_METADATA_ERROR_VERSION;
	}
	if (language_count != DIP_METADATA_LANG_COUNT || profile_count == 0
		|| directory_count != profile_count * DIP_METADATA_LANG_COUNT
		|| directory_offset != DIP_METADATA_HEADER_SIZE
		|| directory_count > 1024u
		|| !range_fits(directory_offset, directory_count, DIP_METADATA_DIRECTORY_SIZE,
			(uint32_t)file_size)
		|| rows_offset != directory_offset + directory_count * DIP_METADATA_DIRECTORY_SIZE
		|| rows_offset > choices_offset || choices_offset > strings_offset
		|| strings_offset > (uint32_t)file_size
		|| strings_size != (uint32_t)file_size - strings_offset
		|| strings_size == 0 || data[strings_offset] != '\0')
	{
		free(data);
		return DIP_METADATA_ERROR_FORMAT;
	}
	if ((choices_offset - rows_offset) % DIP_METADATA_ROW_SIZE != 0
		|| (strings_offset - choices_offset) % sizeof(uint32_t) != 0)
	{
		free(data);
		return DIP_METADATA_ERROR_FORMAT;
	}
	row_count = (choices_offset - rows_offset) / DIP_METADATA_ROW_SIZE;
	choice_count = (strings_offset - choices_offset) / sizeof(uint32_t);
	if (dip_metadata_crc32(data + DIP_METADATA_HEADER_SIZE,
		(size_t)file_size - DIP_METADATA_HEADER_SIZE) != expected_crc)
	{
		free(data);
		return DIP_METADATA_ERROR_CHECKSUM;
	}

	metadata->data = data;
	metadata->size = (size_t)file_size;
	metadata->directory_count = directory_count;
	metadata->directory_offset = directory_offset;
	metadata->rows_offset = rows_offset;
	metadata->choices_offset = choices_offset;
	metadata->strings_offset = strings_offset;
	metadata->strings_size = strings_size;

	for (i = 0; i < directory_count; i++)
	{
		const uint8_t *entry = data + directory_offset + i * DIP_METADATA_DIRECTORY_SIZE;
		uint8_t language = entry[16];
		uint16_t count = read_u16_le(entry + 18);
		uint32_t first_row = read_u32_le(entry + 20);
		if (entry[0] == '\0' || memchr(entry, '\0', DIP_METADATA_PROFILE_NAME_SIZE) == NULL
			|| language >= DIP_METADATA_LANG_COUNT || count == 0 || count > MAX_DIPSWITCHS
			|| first_row > row_count || count > row_count - first_row
			|| read_u32_le(entry + 24) != 0)
		{
			dip_metadata_unload(metadata);
			return DIP_METADATA_ERROR_FORMAT;
		}
	}

	for (i = 0; i < row_count; i++)
	{
		const uint8_t *row = data + rows_offset + i * DIP_METADATA_ROW_SIZE;
		uint32_t label_offset = read_u32_le(row);
		uint8_t value_max = row[6];
		uint8_t count = row[7];
		uint32_t first_choice = read_u32_le(row + 8);
		uint32_t j;

		if (dip_metadata_string(metadata, label_offset) == NULL
			|| row[4] > 1 || count > MAX_DIPSWITCHS + 1
			|| (count == 0 && value_max != 0)
			|| (count != 0 && value_max != count - 1)
			|| first_choice > choice_count || count > choice_count - first_choice
			|| read_u32_le(row + 12) != 0)
		{
			dip_metadata_unload(metadata);
			return DIP_METADATA_ERROR_FORMAT;
		}
		for (j = 0; j < count; j++)
		{
			uint32_t offset = read_u32_le(
				data + choices_offset + (first_choice + j) * sizeof(uint32_t));
			if (dip_metadata_string(metadata, offset) == NULL)
			{
				dip_metadata_unload(metadata);
				return DIP_METADATA_ERROR_FORMAT;
			}
		}
	}
	return DIP_METADATA_OK;
}

dip_metadata_error_t dip_metadata_load_profile(
	dip_metadata_t *metadata,
	const char *path,
	const char *profile,
	dip_metadata_language_t language)
{
	dip_metadata_error_t error;
	uint32_t i;

	if (metadata == NULL || path == NULL || profile == NULL
		|| (unsigned int)language >= DIP_METADATA_LANG_COUNT)
		return DIP_METADATA_ERROR_ARGUMENT;
	dip_metadata_unload(metadata);
	error = load_file(metadata, path);
	if (error != DIP_METADATA_OK)
		return error;

	for (i = 0; i < metadata->directory_count; i++)
	{
		const uint8_t *entry = metadata->data + metadata->directory_offset
			+ i * DIP_METADATA_DIRECTORY_SIZE;
		uint16_t row_count;
		uint32_t first_row;
		uint32_t row_index;

		if (entry[16] != (uint8_t)language || strcmp((const char *)entry, profile) != 0)
			continue;
		row_count = read_u16_le(entry + 18);
		first_row = read_u32_le(entry + 20);
		metadata->active = (dipswitch_t *)calloc(row_count, sizeof(*metadata->active));
		if (metadata->active == NULL)
		{
			dip_metadata_unload(metadata);
			return DIP_METADATA_ERROR_MEMORY;
		}
		metadata->active_count = row_count;

		for (row_index = 0; row_index < row_count; row_index++)
		{
			const uint8_t *row = metadata->data + metadata->rows_offset
				+ (first_row + row_index) * DIP_METADATA_ROW_SIZE;
			dipswitch_t *active = &metadata->active[row_index];
			uint8_t choice_count = row[7];
			uint32_t first_choice = read_u32_le(row + 8);
			uint32_t choice;

			active->label = dip_metadata_string(metadata, read_u32_le(row));
			active->enable = row[4];
			active->mask = row[5];
			active->value = 0;
			active->value_max = row[6];
			for (choice = 0; choice < choice_count; choice++)
			{
				uint32_t offset = read_u32_le(metadata->data + metadata->choices_offset
					+ (first_choice + choice) * sizeof(uint32_t));
				active->values_label[choice] = dip_metadata_string(metadata, offset);
			}
		}
		return DIP_METADATA_OK;
	}

	dip_metadata_unload(metadata);
	return DIP_METADATA_ERROR_PROFILE;
}

dipswitch_t *dip_metadata_active(dip_metadata_t *metadata)
{
	return metadata != NULL ? metadata->active : NULL;
}

const char *dip_metadata_error_string(dip_metadata_error_t error)
{
	switch (error)
	{
	case DIP_METADATA_OK: return "ok";
	case DIP_METADATA_ERROR_ARGUMENT: return "invalid argument";
	case DIP_METADATA_ERROR_OPEN: return "file not found";
	case DIP_METADATA_ERROR_READ: return "file read failed";
	case DIP_METADATA_ERROR_MEMORY: return "out of memory";
	case DIP_METADATA_ERROR_FORMAT: return "invalid format";
	case DIP_METADATA_ERROR_VERSION: return "unsupported version";
	case DIP_METADATA_ERROR_CHECKSUM: return "checksum mismatch";
	case DIP_METADATA_ERROR_PROFILE: return "profile not found";
	default: return "unknown error";
	}
}
