#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <miniz.h>

#include "zip_writer.h"

typedef struct zip_writer_state_t
{
    mz_zip_archive archive;
} zip_writer_state_t;

typedef struct zip_writer_segment_reader_t
{
    const zip_writer_segment_t *segments;
    size_t segment_count;
    uint64_t total_size;
} zip_writer_segment_reader_t;

static size_t zip_writer_segment_read(void *opaque,
                                      mz_uint64 file_offset,
                                      void *buffer,
                                      size_t size)
{
    zip_writer_segment_reader_t *reader = (zip_writer_segment_reader_t *)opaque;
    unsigned char *dst = (unsigned char *)buffer;
    uint64_t offset = file_offset;
    size_t remaining = size;
    size_t copied = 0;
    size_t i;

    if (reader == NULL || buffer == NULL || file_offset >= reader->total_size)
        return 0;

    if ((uint64_t)remaining > reader->total_size - file_offset)
        remaining = (size_t)(reader->total_size - file_offset);

    for (i = 0; i < reader->segment_count && remaining != 0; ++i)
    {
        const zip_writer_segment_t *segment = &reader->segments[i];
        size_t take;

        if (offset >= segment->size)
        {
            offset -= segment->size;
            continue;
        }

        take = segment->size - (size_t)offset;
        if (take > remaining)
            take = remaining;

        if (take != 0)
        {
            memcpy(dst + copied,
                   (const unsigned char *)segment->data + (size_t)offset,
                   take);
            copied += take;
            remaining -= take;
        }
        offset = 0;
    }

    return copied;
}

bool zip_writer_open(zip_writer_t *writer, const char *path)
{
    zip_writer_state_t *state;

    if (writer == NULL || path == NULL || writer->state != NULL)
        return false;

    state = calloc(1, sizeof(*state));
    if (state == NULL)
        return false;

    if (!mz_zip_writer_init_file(&state->archive, path, 0))
    {
        free(state);
        return false;
    }

    writer->state = state;
    return true;
}

bool zip_writer_add_mem(zip_writer_t *writer,
                        const char *name,
                        const void *data,
                        size_t size)
{
    zip_writer_state_t *state;

    if (writer == NULL || name == NULL)
        return false;
    if (size != 0 && data == NULL)
        return false;

    state = writer->state;
    if (state == NULL)
        return false;

    return mz_zip_writer_add_mem(&state->archive,
                                 name,
                                 data,
                                 size,
                                 MZ_BEST_COMPRESSION) != 0;
}

bool zip_writer_add_segments(zip_writer_t *writer,
                             const char *name,
                             const zip_writer_segment_t *segments,
                             size_t segment_count)
{
    zip_writer_state_t *state;
    zip_writer_segment_reader_t reader;
    uint64_t total_size = 0;
    size_t i;

    if (writer == NULL || name == NULL || segments == NULL || segment_count == 0)
        return false;

    state = writer->state;
    if (state == NULL)
        return false;

    for (i = 0; i < segment_count; ++i)
    {
        if (segments[i].size != 0 && segments[i].data == NULL)
            return false;
        if (UINT64_MAX - total_size < segments[i].size)
            return false;
        total_size += segments[i].size;
    }

    reader.segments = segments;
    reader.segment_count = segment_count;
    reader.total_size = total_size;

    return mz_zip_writer_add_read_buf_callback(&state->archive,
                                               name,
                                               zip_writer_segment_read,
                                               &reader,
                                               total_size,
                                               NULL,
                                               NULL,
                                               0,
                                               MZ_BEST_COMPRESSION,
                                               NULL,
                                               0,
                                               NULL,
                                               0) != 0;
}

bool zip_writer_close(zip_writer_t *writer)
{
    zip_writer_state_t *state;
    mz_bool finalized;
    mz_bool ended;

    if (writer == NULL)
        return false;
    state = writer->state;
    if (state == NULL)
        return true;

    finalized = mz_zip_writer_finalize_archive(&state->archive);
    ended = mz_zip_writer_end(&state->archive);
    free(state);
    memset(writer, 0, sizeof(*writer));
    return finalized != 0 && ended != 0;
}

void zip_writer_abort(zip_writer_t *writer)
{
    zip_writer_state_t *state;

    if (writer == NULL)
        return;

    state = writer->state;
    if (state != NULL)
    {
        mz_zip_writer_end(&state->archive);
        free(state);
    }

    memset(writer, 0, sizeof(*writer));
}
