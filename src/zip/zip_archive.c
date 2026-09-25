/******************************************************************************
 *
 *    zip_archive.c
 *
 *    Explicit ZIP archive and entry objects backed by miniz
 *
 ******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "zip/zip_archive.h"

static bool zip_entry_info_from_stat(const mz_zip_archive_file_stat *stat,
                                     zip_entry_info_t *info)
{
    if (stat == NULL || info == NULL)
        return false;

    strncpy(info->name, stat->m_filename, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    info->size = stat->m_uncomp_size;
    info->crc32 = stat->m_crc32;
    return true;
}

bool zip_archive_open(zip_archive_t *archive, const char *path)
{
    if (archive == NULL || path == NULL || archive->is_open)
        return false;

    memset(archive, 0, sizeof(*archive));
    if (!mz_zip_reader_init_file(&archive->archive, path, 0))
        return false;

    archive->is_open = true;
    return true;
}

void zip_archive_close(zip_archive_t *archive)
{
    if (archive == NULL)
        return;

    if (archive->is_open)
        mz_zip_reader_end(&archive->archive);

    memset(archive, 0, sizeof(*archive));
}

bool zip_archive_stat(zip_archive_t *archive,
                      const char *name,
                      zip_entry_info_t *info)
{
    mz_zip_archive_file_stat stat;
    int index;

    if (archive == NULL || !archive->is_open || name == NULL || info == NULL)
        return false;

    index = mz_zip_reader_locate_file(&archive->archive, name, NULL, 0);
    if (index < 0)
        return false;
    if (!mz_zip_reader_file_stat(&archive->archive, (mz_uint)index, &stat))
        return false;

    return zip_entry_info_from_stat(&stat, info);
}

bool zip_archive_find_crc(zip_archive_t *archive,
                          uint32_t crc32,
                          zip_entry_info_t *info)
{
    mz_uint i;
    mz_uint count;

    if (archive == NULL || !archive->is_open || info == NULL)
        return false;

    count = mz_zip_reader_get_num_files(&archive->archive);
    for (i = 0; i < count; ++i)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(&archive->archive, i, &stat))
            continue;
        if (stat.m_crc32 == crc32)
            return zip_entry_info_from_stat(&stat, info);
    }

    return false;
}

bool zip_entry_open(zip_archive_t *archive,
                    const char *name,
                    zip_entry_t *entry)
{
    mz_zip_archive_file_stat stat;
    int index;

    if (archive == NULL || !archive->is_open || name == NULL || entry == NULL)
        return false;
    if (entry->reader != NULL)
        return false;

    memset(entry, 0, sizeof(*entry));

    index = mz_zip_reader_locate_file(&archive->archive, name, NULL, 0);
    if (index < 0)
        return false;
    if (!mz_zip_reader_file_stat(&archive->archive, (mz_uint)index, &stat))
        return false;

    entry->reader = mz_zip_reader_extract_iter_new(&archive->archive,
                                                   (mz_uint)index,
                                                   0);
    if (entry->reader == NULL)
        return false;

    entry->size = stat.m_uncomp_size;
    entry->crc32 = stat.m_crc32;
    return true;
}

size_t zip_entry_read(zip_entry_t *entry, void *dst, size_t size)
{
    size_t result;

    if (entry == NULL || entry->reader == NULL || dst == NULL || size == 0)
        return 0;

    result = mz_zip_reader_extract_iter_read(entry->reader, dst, size);
    entry->bytes_read += result;
    return result;
}

int zip_entry_getc(zip_entry_t *entry)
{
    if (entry == NULL || entry->reader == NULL)
        return EOF;

    if (entry->byte_cache_pos >= entry->byte_cache_len)
    {
        entry->byte_cache_len = zip_entry_read(entry,
                                               entry->byte_cache,
                                               sizeof(entry->byte_cache));
        entry->byte_cache_pos = 0;
        if (entry->byte_cache_len == 0)
            return EOF;
    }

    return entry->byte_cache[entry->byte_cache_pos++] & 0xff;
}

bool zip_entry_close(zip_entry_t *entry)
{
    bool complete;
    mz_bool ok;

    if (entry == NULL)
        return false;
    if (entry->reader == NULL)
    {
        memset(entry, 0, sizeof(*entry));
        return true;
    }

    /* Match the legacy reader: a CRC failure matters only after the complete
       uncompressed entry has been consumed. */
    complete = entry->bytes_read >= entry->size;
    ok = mz_zip_reader_extract_iter_free(entry->reader);
    memset(entry, 0, sizeof(*entry));
    return !complete || ok;
}
