#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <miniz.h>

#include "zfile.h"


#define ZIP_NOTOPEN   0
#define ZIP_READOPEN  1
#define ZIP_WRITEOPEN 2

static mz_zip_archive zip_archive;
static mz_zip_reader_extract_iter_state *zip_reader;
static mz_zip_archive_file_stat zip_file_stat;
static mz_uint zip_find_index;
static int zip_archive_open;
static int zip_mode = ZIP_NOTOPEN;

static char basedir[PATH_MAX];
static char *basedirend;
static unsigned char zip_cache[4096];
static size_t zip_cached_len;
static size_t zip_filepos;
static mz_uint64 zip_streamed_len;
static int64_t zip_fd = -1;

static FILE *zip_write_file;
static char zip_write_name[PATH_MAX];


static int zip_close_reader(void)
{
    mz_bool complete;
    mz_bool ok;

    if (zip_reader == NULL)
        return 0;

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
    file->length = (uint32_t)stat.m_uncomp_size;
    file->crc32 = stat.m_crc32;
    return 1;
}


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
    ok = mz_zip_writer_add_cfile(&zip_archive,
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
    int length;

    if (zip_mode != ZIP_NOTOPEN)
        zip_close();

    memset(&zip_archive, 0, sizeof(zip_archive));
    basedir[0] = '\0';
    basedirend = NULL;

    if (!strcmp(mode, "rb"))
    {
        zip_mode = ZIP_READOPEN;
        if (mz_zip_reader_init_file(&zip_archive, path, 0))
        {
            zip_archive_open = 1;
            return 0;
        }

        length = snprintf(basedir, sizeof(basedir), "%s/", path);
        if (length >= 0 && (size_t)length < sizeof(basedir))
            basedirend = basedir + length;
        return -1;
    }

    if (!strcmp(mode, "wb"))
    {
        if (mz_zip_writer_init_file(&zip_archive, path, 0))
        {
            zip_mode = ZIP_WRITEOPEN;
            zip_archive_open = 1;
            return 0;
        }
    }

    zip_mode = ZIP_NOTOPEN;
    return -1;
}


void zip_close(void)
{
    if (zip_fd != -1)
        zclose(zip_fd);

    if (zip_mode == ZIP_READOPEN)
    {
        zip_close_reader();
        if (zip_archive_open)
            mz_zip_reader_end(&zip_archive);
    }
    else if (zip_mode == ZIP_WRITEOPEN)
    {
        zip_close_writer_file();
        if (zip_archive_open)
        {
            mz_zip_writer_finalize_archive(&zip_archive);
            mz_zip_writer_end(&zip_archive);
        }
    }

    memset(&zip_archive, 0, sizeof(zip_archive));
    zip_archive_open = 0;
    zip_mode = ZIP_NOTOPEN;
    basedirend = NULL;
}


int zip_findfirst(struct zip_find_t *file)
{
    if (zip_mode != ZIP_READOPEN || !zip_archive_open ||
        mz_zip_reader_get_num_files(&zip_archive) == 0)
        return 0;

    zip_find_index = 0;
    return zip_stat(zip_find_index, file);
}


int zip_findnext(struct zip_find_t *file)
{
    if (zip_mode != ZIP_READOPEN || !zip_archive_open)
        return 0;

    zip_find_index++;
    if (zip_find_index >= mz_zip_reader_get_num_files(&zip_archive))
        return 0;

    return zip_stat(zip_find_index, file);
}


int64_t zopen(const char *filename)
{
    zip_cached_len = 0;
    zip_filepos = 0;
    zip_streamed_len = 0;

    if (zip_mode == ZIP_READOPEN)
    {
        int file_index;

        if (!zip_archive_open)
        {
            int fd_plain;
            int length;
            size_t remaining;

            if (basedirend == NULL)
                return -1;

            remaining = (size_t)(basedir + sizeof(basedir) - basedirend);
            length = snprintf(basedirend, remaining, "%s", filename);
            if (length < 0 || (size_t)length >= remaining)
                return -1;

            fd_plain = open(basedir, O_RDONLY);
            if (fd_plain < 0)
                return -1;

            zip_fd = (int64_t)fd_plain;
            return zip_fd;
        }

        zip_close_reader();
        file_index = mz_zip_reader_locate_file(&zip_archive, filename, NULL, 0);
        if (file_index < 0 ||
            !mz_zip_reader_file_stat(&zip_archive, (mz_uint)file_index, &zip_file_stat))
            return -1;

        zip_reader = mz_zip_reader_extract_iter_new(&zip_archive, (mz_uint)file_index, 0);
        return (zip_reader != NULL) ? 0 : -1;
    }

    if (zip_mode == ZIP_WRITEOPEN && zip_archive_open)
    {
        if (zip_close_writer_file() != 0)
            return -1;

        if (snprintf(zip_write_name, sizeof(zip_write_name), "%s", filename) < 0 ||
            strlen(filename) >= sizeof(zip_write_name))
            return -1;

        zip_write_file = tmpfile();
        return (zip_write_file != NULL) ? 0 : -1;
    }

    return -1;
}


int zclose(int64_t fd)
{
    zip_cached_len = 0;
    zip_filepos = 0;

    if (zip_mode == ZIP_READOPEN)
    {
        if (!zip_archive_open)
        {
            if (fd != -1)
                close((int)fd);
            zip_fd = -1;
            return 0;
        }
        return zip_close_reader();
    }

    if (zip_mode == ZIP_WRITEOPEN)
        return zip_close_writer_file();

    return -1;
}


size_t zread(int64_t fd, void *buf, unsigned size)
{
    if (zip_mode != ZIP_READOPEN)
        return 0;

    if (!zip_archive_open)
    {
        ssize_t result = read((int)fd, buf, size);
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


int zwrite(int64_t fd, void *buf, unsigned size)
{
    (void)fd;

    if (zip_mode != ZIP_WRITEOPEN || zip_write_file == NULL)
        return -1;

    return (fwrite(buf, 1, size, zip_write_file) == size) ? 0 : -1;
}


int zgetc(int64_t fd)
{
    if (zip_mode != ZIP_READOPEN)
        return -1;

    if (zip_cached_len == 0)
    {
        zip_cached_len = zread(fd, zip_cache, sizeof(zip_cache));
        if (zip_cached_len == 0)
            return -1;
        zip_filepos = 0;
    }

    zip_cached_len--;
    return zip_cache[zip_filepos++] & 0xff;
}


size_t zsize(int64_t fd)
{
    if (zip_mode != ZIP_READOPEN)
        return 0;

    if (!zip_archive_open)
    {
        off_t pos = lseek((int)fd, 0, SEEK_CUR);
        off_t len = lseek((int)fd, 0, SEEK_END);

        if (pos >= 0)
            lseek((int)fd, pos, SEEK_SET);
        return (len < 0) ? 0 : (size_t)len;
    }

    return (size_t)zip_file_stat.m_uncomp_size;
}


int zcrc(int64_t fd)
{
    (void)fd;

    if (zip_mode == ZIP_READOPEN && zip_archive_open)
        return (int)zip_file_stat.m_crc32;
    return 0;
}
