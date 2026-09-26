/******************************************************************************
 *
 *    zip_archive.c
 *
 *    Explicit ZIP archive and entry objects backed by miniz
 *
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <miniz.h>

#include "common/zip_archive.h"

#define ZIP_ENTRY_BYTE_CACHE_SIZE 4096

typedef struct zip_archive_state_t
{
    mz_zip_archive archive;
} zip_archive_state_t;

typedef struct zip_entry_state_t
{
    mz_zip_reader_extract_iter_state *reader;
    uint64_t size;
    uint64_t bytes_read;
    uint32_t crc32;
    unsigned char *byte_cache;
    size_t byte_cache_pos;
    size_t byte_cache_len;
} zip_entry_state_t;

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
    zip_archive_state_t *state;

    if (archive == NULL || path == NULL || archive->state != NULL)
        return false;

    state = calloc(1, sizeof(*state));
    if (state == NULL)
        return false;

    if (!mz_zip_reader_init_file(&state->archive, path, 0))
    {
        free(state);
        return false;
    }

    archive->state = state;
    return true;
}

void zip_archive_close(zip_archive_t *archive)
{
    zip_archive_state_t *state;

    if (archive == NULL)
        return;

    state = archive->state;
    if (state != NULL)
    {
        mz_zip_reader_end(&state->archive);
        free(state);
    }

    memset(archive, 0, sizeof(*archive));
}

bool zip_archive_stat(zip_archive_t *archive,
                      const char *name,
                      zip_entry_info_t *info)
{
    zip_archive_state_t *state;
    mz_zip_archive_file_stat stat;
    int index;

    if (archive == NULL || name == NULL || info == NULL)
        return false;

    state = archive->state;
    if (state == NULL)
        return false;

    index = mz_zip_reader_locate_file(&state->archive, name, NULL, 0);
    if (index < 0)
        return false;
    if (!mz_zip_reader_file_stat(&state->archive, (mz_uint)index, &stat))
        return false;

    return zip_entry_info_from_stat(&stat, info);
}

bool zip_archive_find_crc(zip_archive_t *archive,
                          uint32_t crc32,
                          zip_entry_info_t *info)
{
    zip_archive_state_t *state;
    mz_uint i;
    mz_uint count;

    if (archive == NULL || info == NULL)
        return false;

    state = archive->state;
    if (state == NULL)
        return false;

    count = mz_zip_reader_get_num_files(&state->archive);
    for (i = 0; i < count; ++i)
    {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(&state->archive, i, &stat))
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
    zip_archive_state_t *archive_state;
    zip_entry_state_t *entry_state;
    mz_zip_archive_file_stat stat;
    int index;

    if (archive == NULL || name == NULL || entry == NULL || entry->state != NULL)
        return false;

    archive_state = archive->state;
    if (archive_state == NULL)
        return false;

    entry_state = calloc(1, sizeof(*entry_state));
    if (entry_state == NULL)
        return false;

    index = mz_zip_reader_locate_file(&archive_state->archive, name, NULL, 0);
    if (index < 0)
        goto error;
    if (!mz_zip_reader_file_stat(&archive_state->archive, (mz_uint)index, &stat))
        goto error;

    entry_state->reader = mz_zip_reader_extract_iter_new(&archive_state->archive,
                                                        (mz_uint)index,
                                                        0);
    if (entry_state->reader == NULL)
        goto error;

    entry_state->size = stat.m_uncomp_size;
    entry_state->crc32 = stat.m_crc32;
    entry->state = entry_state;
    return true;

error:
    free(entry_state);
    return false;
}

size_t zip_entry_read(zip_entry_t *entry, void *dst, size_t size)
{
    zip_entry_state_t *state;
    size_t result;

    if (entry == NULL || dst == NULL || size == 0)
        return 0;

    state = entry->state;
    if (state == NULL || state->reader == NULL)
        return 0;

    result = mz_zip_reader_extract_iter_read(state->reader, dst, size);
    state->bytes_read += result;
    return result;
}

int zip_entry_getc(zip_entry_t *entry)
{
    zip_entry_state_t *state;

    if (entry == NULL)
        return EOF;

    state = entry->state;
    if (state == NULL || state->reader == NULL)
        return EOF;

    if (state->byte_cache == NULL)
    {
        state->byte_cache = malloc(ZIP_ENTRY_BYTE_CACHE_SIZE);
        if (state->byte_cache == NULL)
        {
            unsigned char value;
            return zip_entry_read(entry, &value, 1) == 1 ? value : EOF;
        }
    }

    if (state->byte_cache_pos >= state->byte_cache_len)
    {
        state->byte_cache_len = zip_entry_read(entry,
                                               state->byte_cache,
                                               ZIP_ENTRY_BYTE_CACHE_SIZE);
        state->byte_cache_pos = 0;
        if (state->byte_cache_len == 0)
            return EOF;
    }

    return state->byte_cache[state->byte_cache_pos++] & 0xff;
}

bool zip_entry_is_open(const zip_entry_t *entry)
{
    return entry != NULL && entry->state != NULL;
}

bool zip_entry_close(zip_entry_t *entry)
{
    zip_entry_state_t *state;
    bool complete;
    bool ok = true;

    if (entry == NULL)
        return false;

    state = entry->state;
    if (state != NULL)
    {
        /* Preserve the established behavior: a CRC failure matters only after
           the complete uncompressed entry has been consumed. */
        complete = state->bytes_read >= state->size;
        if (state->reader != NULL)
        {
            mz_bool reader_ok = mz_zip_reader_extract_iter_free(state->reader);
            ok = !complete || reader_ok != 0;
        }
        free(state->byte_cache);
        free(state);
    }

    memset(entry, 0, sizeof(*entry));
    return ok;
}
