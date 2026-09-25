/******************************************************************************
 *
 *    zip_archive.h
 *
 *    Explicit ZIP archive and entry objects backed by miniz
 *
 ******************************************************************************/

#ifndef ZIP_ARCHIVE_H
#define ZIP_ARCHIVE_H

#include <stdbool.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <miniz.h>

typedef struct zip_archive_t
{
    mz_zip_archive archive;
    bool is_open;
} zip_archive_t;

typedef struct zip_entry_t
{
    mz_zip_reader_extract_iter_state *reader;
    uint64_t size;
    uint64_t bytes_read;
    uint32_t crc32;
    unsigned char *byte_cache;
    size_t byte_cache_pos;
    size_t byte_cache_len;
} zip_entry_t;

typedef struct zip_entry_info_t
{
    char name[PATH_MAX];
    uint64_t size;
    uint32_t crc32;
} zip_entry_info_t;

/* Objects must be zero-initialized before their first open. */
bool zip_archive_open(zip_archive_t *archive, const char *path);
void zip_archive_close(zip_archive_t *archive);

bool zip_archive_stat(zip_archive_t *archive,
                      const char *name,
                      zip_entry_info_t *info);

bool zip_archive_find_crc(zip_archive_t *archive,
                          uint32_t crc32,
                          zip_entry_info_t *info);

bool zip_entry_open(zip_archive_t *archive,
                    const char *name,
                    zip_entry_t *entry);

size_t zip_entry_read(zip_entry_t *entry, void *dst, size_t size);
int zip_entry_getc(zip_entry_t *entry);
bool zip_entry_close(zip_entry_t *entry);

#endif /* ZIP_ARCHIVE_H */
