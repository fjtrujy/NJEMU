/******************************************************************************

    zfile.c

    ZIP File Operation Functions backed by miniz

******************************************************************************/

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <miniz.h>

#include "emumain.h"
#include "zip/zip_archive.h"
#include "zip/zfile.h"

/*
 * Transitional compatibility state. New code must use zip_archive_t and
 * zip_entry_t directly. Z3-Z5 will remove this adapter with the legacy API.
 */
static zip_archive_t legacy_archive;
static zip_entry_t legacy_entry;

static char basedir[PATH_MAX];
static char *basedirend;

int zip_open(const char *path)
{
    int length;

    zip_close();

    if (zip_archive_open(&legacy_archive, path))
        return 0;

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
    zip_entry_close(&legacy_entry);

    zip_archive_close(&legacy_archive);
}

int64_t zopen(const char *filename)
{
    if (!legacy_archive.is_open)
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

    zip_entry_close(&legacy_entry);
    return zip_entry_open(&legacy_archive, filename, &legacy_entry) ? 0 : -1;
}

int zclose(int64_t fd)
{
    if (!legacy_archive.is_open)
    {
        if (fd != -1)
            close((int32_t)fd);
        return 0;
    }

    return zip_entry_close(&legacy_entry) ? 0 : -1;
}

size_t zread(int64_t fd, void *buf, size_t size)
{
    if (!legacy_archive.is_open)
    {
        ssize_t result = read((int32_t)fd, buf, size);
        return (result < 0) ? 0 : (size_t)result;
    }

    return zip_entry_read(&legacy_entry, buf, size);
}

size_t zsize(int64_t fd)
{
    if (!legacy_archive.is_open)
    {
        off_t pos = lseek((int32_t)fd, 0, SEEK_CUR);
        off_t len = lseek((int32_t)fd, 0, SEEK_END);
        lseek((int32_t)fd, pos, SEEK_SET);
        return (len < 0) ? 0 : (size_t)len;
    }

    return (size_t)legacy_entry.size;
}

#if (EMU_SYSTEM == NCDZ)
int zlength(const char *filename)
{
    if (legacy_archive.is_open)
    {
        zip_entry_info_t info;

        if (!zip_archive_stat(&legacy_archive, filename, &info))
            return -1;
        return (int)info.size;
    }

    int64_t fd = zopen(filename);
    size_t length;

    if (fd == -1)
        return -1;

    length = zsize(fd);
    zclose(fd);
    return (int)length;
}
#endif
