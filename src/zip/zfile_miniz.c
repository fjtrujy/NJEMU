/******************************************************************************

    zfile_miniz.c

    ZIP File Operation Functions backed by miniz

******************************************************************************/

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <miniz.h>

#include "emumain.h"
#include "zip/zfile.h"

static mz_zip_archive zip_archive;
static mz_zip_reader_extract_iter_state *zip_reader;
static mz_zip_archive_file_stat zip_file_stat;
static mz_uint zip_find_index;
static int zip_archive_open;

static char basedir[PATH_MAX];
static char *basedirend;
static unsigned char zip_cache[4096];
static size_t zip_cached_len;
static size_t zip_filepos;
static mz_uint64 zip_streamed_len;

static int zip_close_reader(void)
{
    mz_bool complete;
    mz_bool ok;

    if (zip_reader == NULL)
        return 0;

    /* Legacy MiniZip only reported CRC failure after the whole entry had been
       consumed; closing a partially-read entry was otherwise successful. */
    complete = zip_streamed_len >= zip_file_stat.m_uncomp_size;
    ok = mz_zip_reader_extract_iter_free(zip_reader);
    zip_reader = NULL;
    zip_streamed_len = 0;
    return (!complete || ok) ? 0 : -1;
}

static int zip_stat(mz_uint index, struct zip_find_t *file)
{
    mz_zip_archive_file_stat stat;

    if (!mz_zip_reader_file_stat(&zip_archive, index, &stat))
        return 0;

    strncpy(file->name, stat.m_filename, sizeof(file->name) - 1);
    file->name[sizeof(file->name) - 1] = '\0';
    file->length = (size_t)stat.m_uncomp_size;
    file->crc32 = stat.m_crc32;
    return 1;
}

int zip_open(const char *path)
{
    int length;

    if (zip_archive_open)
        zip_close();

    memset(&zip_archive, 0, sizeof(zip_archive));
    if (mz_zip_reader_init_file(&zip_archive, path, 0))
    {
        zip_archive_open = 1;
        return 0;
    }

    /* A non-ZIP path is the legacy directory backend used by NCDZ/cache data. */
    length = snprintf(basedir, sizeof(basedir), "%s/", path);
    if (length < 0 || (size_t)length >= sizeof(basedir))
    {
        basedir[0] = '\0';
        basedirend = NULL;
        return -1;
    }
    basedirend = basedir + length;
    return -1;
}

void zip_close(void)
{
    zip_close_reader();

    if (zip_archive_open)
    {
        mz_zip_reader_end(&zip_archive);
        memset(&zip_archive, 0, sizeof(zip_archive));
        zip_archive_open = 0;
    }
}

int zip_findfirst(struct zip_find_t *file)
{
    if (!zip_archive_open || mz_zip_reader_get_num_files(&zip_archive) == 0)
        return 0;

    zip_find_index = 0;
    return zip_stat(zip_find_index, file);
}

int zip_findnext(struct zip_find_t *file)
{
    if (!zip_archive_open)
        return 0;

    zip_find_index++;
    if (zip_find_index >= mz_zip_reader_get_num_files(&zip_archive))
        return 0;

    return zip_stat(zip_find_index, file);
}

int64_t zopen(const char *filename)
{
    int file_index;

    zip_cached_len = 0;
    zip_filepos = 0;
    zip_streamed_len = 0;

    if (!zip_archive_open)
    {
        int32_t fd;
        int length;
        size_t remaining;

        if (basedirend == NULL)
            return -1;
        remaining = (size_t)(basedir + sizeof(basedir) - basedirend);
        length = snprintf(basedirend, remaining, "%s", filename);
        if (length < 0 || (size_t)length >= remaining)
            return -1;
        fd = open(basedir, O_RDONLY, 0777);
        return (fd < 0) ? -1 : (int64_t)fd;
    }

    zip_close_reader();

    file_index = mz_zip_reader_locate_file(&zip_archive, filename, NULL, 0);
    if (file_index < 0 || !mz_zip_reader_file_stat(&zip_archive, (mz_uint)file_index, &zip_file_stat))
        return -1;

    zip_reader = mz_zip_reader_extract_iter_new(&zip_archive, (mz_uint)file_index, 0);
    return (zip_reader != NULL) ? 0 : -1;
}

int zclose(int64_t fd)
{
    (void)fd;
    zip_cached_len = 0;
    zip_filepos = 0;

    if (!zip_archive_open)
    {
        if (fd != -1)
            close((int32_t)fd);
        return 0;
    }

    return zip_close_reader();
}

size_t zread(int64_t fd, void *buf, size_t size)
{
    if (!zip_archive_open)
    {
        ssize_t result = read((int32_t)fd, buf, size);
        return (result < 0) ? 0 : (size_t)result;
    }

    if (zip_reader != NULL)
    {
        size_t result = mz_zip_reader_extract_iter_read(zip_reader, buf, size);
        zip_streamed_len += result;
        return result;
    }
    return 0;
}

int zgetc(int64_t fd)
{
    if (zip_cached_len == 0)
    {
        zip_cached_len = zread(fd, zip_cache, sizeof(zip_cache));
        if (zip_cached_len == 0)
            return EOF;
        zip_filepos = 0;
    }

    zip_cached_len--;
    return zip_cache[zip_filepos++] & 0xff;
}

size_t zsize(int64_t fd)
{
    if (!zip_archive_open)
    {
        off_t pos = lseek((int32_t)fd, 0, SEEK_CUR);
        off_t len = lseek((int32_t)fd, 0, SEEK_END);
        lseek((int32_t)fd, pos, SEEK_SET);
        return (len < 0) ? 0 : (size_t)len;
    }

    return (size_t)zip_file_stat.m_uncomp_size;
}

#if (EMU_SYSTEM == NCDZ)
int zlength(const char *filename)
{
    int64_t fd = zopen(filename);
    size_t length;

    if (fd == -1)
        return -1;

    length = zsize(fd);
    zclose(fd);
    return (int)length;
}
#endif
