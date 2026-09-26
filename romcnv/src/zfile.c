#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <miniz.h>

#include "zfile.h"

static mz_zip_archive zip_writer;
static int zip_writer_open;

static FILE *zip_write_file;
static char zip_write_name[PATH_MAX];

static int zip_close_writer_file(void)
{
    long size;
    time_t modified;
    mz_bool ok;

    if (zip_write_file == NULL)
        return 0;

    if (fflush(zip_write_file) != 0 || fseek(zip_write_file, 0, SEEK_END) != 0)
    {
        fclose(zip_write_file);
        zip_write_file = NULL;
        return -1;
    }

    size = ftell(zip_write_file);
    if (size < 0 || fseek(zip_write_file, 0, SEEK_SET) != 0)
    {
        fclose(zip_write_file);
        zip_write_file = NULL;
        return -1;
    }

    modified = time(NULL);
    ok = mz_zip_writer_add_cfile(&zip_writer,
                                 zip_write_name,
                                 zip_write_file,
                                 (mz_uint64)size,
                                 &modified,
                                 NULL,
                                 0,
                                 MZ_BEST_COMPRESSION,
                                 NULL,
                                 0,
                                 NULL,
                                 0);

    fclose(zip_write_file);
    zip_write_file = NULL;
    zip_write_name[0] = '\0';
    return ok ? 0 : -1;
}

int64_t zip_open(const char *path, const char *mode)
{
    if (zip_writer_open)
        zip_close();

    if (path == NULL || mode == NULL || strcmp(mode, "wb") != 0)
        return -1;

    memset(&zip_writer, 0, sizeof(zip_writer));
    if (!mz_zip_writer_init_file(&zip_writer, path, 0))
        return -1;

    zip_writer_open = 1;
    return 0;
}

void zip_close(void)
{
    if (!zip_writer_open)
        return;

    zip_close_writer_file();
    mz_zip_writer_finalize_archive(&zip_writer);
    mz_zip_writer_end(&zip_writer);

    memset(&zip_writer, 0, sizeof(zip_writer));
    zip_writer_open = 0;
}

int64_t zopen(const char *filename)
{
    int length;

    if (!zip_writer_open || filename == NULL)
        return -1;
    if (zip_close_writer_file() != 0)
        return -1;

    length = snprintf(zip_write_name, sizeof(zip_write_name), "%s", filename);
    if (length < 0 || (size_t)length >= sizeof(zip_write_name))
        return -1;

    zip_write_file = tmpfile();
    return (zip_write_file != NULL) ? 0 : -1;
}

int zwrite(int64_t fd, void *buf, unsigned size)
{
    (void)fd;

    if (!zip_writer_open || zip_write_file == NULL)
        return -1;

    return (fwrite(buf, 1, size, zip_write_file) == size) ? 0 : -1;
}

int zclose(int64_t fd)
{
    (void)fd;

    if (!zip_writer_open)
        return -1;

    return zip_close_writer_file();
}
