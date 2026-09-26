#ifndef ZIP_READER_H
#define ZIP_READER_H

#include <stdbool.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

typedef struct zip_reader_archive_t
{
    void *state;
} zip_reader_archive_t;

typedef struct zip_reader_entry_t
{
    void *state;
} zip_reader_entry_t;

typedef struct zip_reader_entry_info_t
{
    char name[PATH_MAX];
    uint64_t size;
    uint32_t crc32;
} zip_reader_entry_info_t;

bool zip_reader_archive_open(zip_reader_archive_t *archive, const char *path);
void zip_reader_archive_close(zip_reader_archive_t *archive);

bool zip_reader_archive_stat(zip_reader_archive_t *archive,
                             const char *name,
                             zip_reader_entry_info_t *info);

bool zip_reader_archive_find_crc(zip_reader_archive_t *archive,
                                 uint32_t crc32,
                                 zip_reader_entry_info_t *info);

bool zip_reader_entry_open(zip_reader_archive_t *archive,
                           const char *name,
                           zip_reader_entry_t *entry);

size_t zip_reader_entry_read(zip_reader_entry_t *entry, void *dst, size_t size);
int zip_reader_entry_getc(zip_reader_entry_t *entry);
bool zip_reader_entry_is_open(const zip_reader_entry_t *entry);
bool zip_reader_entry_close(zip_reader_entry_t *entry);

#endif /* ZIP_READER_H */
