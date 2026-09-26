/******************************************************************************
 *
 *    resource_source.h
 *
 *    Explicit NCDZ directory or ZIP resource source
 *
 ******************************************************************************/

#ifndef NCDZ_RESOURCE_SOURCE_H
#define NCDZ_RESOURCE_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/zip_archive.h"

typedef enum resource_source_type_t
{
    RESOURCE_SOURCE_NONE = 0,
    RESOURCE_SOURCE_DIRECTORY,
    RESOURCE_SOURCE_ZIP
} resource_source_type_t;

typedef struct resource_source_t
{
    resource_source_type_t type;
    union
    {
        char directory[PATH_MAX];
        zip_archive_t zip;
    } backend;
} resource_source_t;

typedef struct resource_file_t
{
    resource_source_type_t type;
    union
    {
        int fd;
        zip_entry_t zip;
    } backend;
} resource_file_t;

typedef struct resource_file_info_t
{
    uint64_t size;
} resource_file_info_t;

extern resource_source_t ncdz_game_source;

bool resource_source_open_directory(resource_source_t *source, const char *path);
bool resource_source_open_zip(resource_source_t *source, const char *path);
void resource_source_close(resource_source_t *source);

bool resource_source_stat(resource_source_t *source,
                          const char *name,
                          resource_file_info_t *info);

bool resource_file_open(resource_source_t *source,
                        const char *name,
                        resource_file_t *file);

size_t resource_file_read(resource_file_t *file, void *dst, size_t size);
bool resource_file_close(resource_file_t *file);

#endif /* NCDZ_RESOURCE_SOURCE_H */
