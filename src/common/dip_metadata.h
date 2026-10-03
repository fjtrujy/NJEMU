#ifndef DIP_METADATA_H
#define DIP_METADATA_H

#include <stddef.h>
#include <stdint.h>
#include "common/dip_menu.h"

typedef enum dip_metadata_language
{
	DIP_METADATA_LANG_ENGLISH = 0,
	DIP_METADATA_LANG_JAPANESE = 1,
	DIP_METADATA_LANG_CHINESE_SIMPLIFIED = 2,
	DIP_METADATA_LANG_CHINESE_TRADITIONAL = 3,
	DIP_METADATA_LANG_COUNT
} dip_metadata_language_t;

typedef enum dip_metadata_error
{
	DIP_METADATA_OK = 0,
	DIP_METADATA_ERROR_ARGUMENT,
	DIP_METADATA_ERROR_OPEN,
	DIP_METADATA_ERROR_READ,
	DIP_METADATA_ERROR_MEMORY,
	DIP_METADATA_ERROR_FORMAT,
	DIP_METADATA_ERROR_VERSION,
	DIP_METADATA_ERROR_CHECKSUM,
	DIP_METADATA_ERROR_PROFILE
} dip_metadata_error_t;

typedef struct dip_metadata
{
	uint8_t *data;
	size_t size;
	uint32_t directory_count;
	uint32_t directory_offset;
	uint32_t rows_offset;
	uint32_t choices_offset;
	uint32_t strings_offset;
	uint32_t strings_size;
	dipswitch_t *active;
	uint16_t active_count;
} dip_metadata_t;

dip_metadata_error_t dip_metadata_load_profile(
	dip_metadata_t *metadata,
	const char *path,
	const char *profile,
	dip_metadata_language_t language);
void dip_metadata_unload(dip_metadata_t *metadata);
dipswitch_t *dip_metadata_active(dip_metadata_t *metadata);
const char *dip_metadata_error_string(dip_metadata_error_t error);

#endif /* DIP_METADATA_H */
