#ifndef ZIP_WRITER_H
#define ZIP_WRITER_H

#include <stdbool.h>
#include <stddef.h>

#include <miniz.h>

typedef struct zip_writer_t
{
    mz_zip_archive archive;
    bool is_open;
} zip_writer_t;

typedef struct zip_writer_segment_t
{
    const void *data;
    size_t size;
} zip_writer_segment_t;

bool zip_writer_open(zip_writer_t *writer, const char *path);
bool zip_writer_add_mem(zip_writer_t *writer,
                        const char *name,
                        const void *data,
                        size_t size);
bool zip_writer_add_segments(zip_writer_t *writer,
                             const char *name,
                             const zip_writer_segment_t *segments,
                             size_t segment_count);
bool zip_writer_close(zip_writer_t *writer);
void zip_writer_abort(zip_writer_t *writer);

#endif /* ZIP_WRITER_H */
