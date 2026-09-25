/******************************************************************************
 *
 *    resource_source.c
 *
 *    Explicit NCDZ directory or ZIP resource source
 *
 ******************************************************************************/

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ncdz/resource_source.h"

resource_source_t ncdz_game_source;

bool resource_source_open_directory(resource_source_t *source, const char *path)
{
    struct stat st;
    int length;

    if (source == NULL || path == NULL || source->type != RESOURCE_SOURCE_NONE)
        return false;
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
        return false;

    length = snprintf(source->backend.directory,
                      sizeof(source->backend.directory),
                      "%s", path);
    if (length < 0 || (size_t)length >= sizeof(source->backend.directory))
    {
        memset(source, 0, sizeof(*source));
        return false;
    }

    source->type = RESOURCE_SOURCE_DIRECTORY;
    return true;
}

bool resource_source_open_zip(resource_source_t *source, const char *path)
{
    if (source == NULL || path == NULL || source->type != RESOURCE_SOURCE_NONE)
        return false;

    if (!zip_archive_open(&source->backend.zip, path))
    {
        memset(source, 0, sizeof(*source));
        return false;
    }

    source->type = RESOURCE_SOURCE_ZIP;
    return true;
}

void resource_source_close(resource_source_t *source)
{
    if (source == NULL)
        return;

    if (source->type == RESOURCE_SOURCE_ZIP)
        zip_archive_close(&source->backend.zip);

    memset(source, 0, sizeof(*source));
}

bool resource_source_stat(resource_source_t *source,
                          const char *name,
                          resource_file_info_t *info)
{
    if (source == NULL || name == NULL || info == NULL)
        return false;

    if (source->type == RESOURCE_SOURCE_DIRECTORY)
    {
        char path[PATH_MAX];
        struct stat st;
        int length = snprintf(path, sizeof(path), "%s/%s",
                              source->backend.directory, name);

        if (length < 0 || (size_t)length >= sizeof(path))
            return false;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            return false;

        info->size = (uint64_t)st.st_size;
        return true;
    }

    if (source->type == RESOURCE_SOURCE_ZIP)
    {
        zip_entry_info_t zip_info;

        if (!zip_archive_stat(&source->backend.zip, name, &zip_info))
            return false;

        info->size = zip_info.size;
        return true;
    }

    return false;
}

bool resource_file_open(resource_source_t *source,
                        const char *name,
                        resource_file_t *file)
{
    if (source == NULL || name == NULL || file == NULL ||
        file->type != RESOURCE_SOURCE_NONE)
        return false;

    if (source->type == RESOURCE_SOURCE_DIRECTORY)
    {
        char path[PATH_MAX];
        int length = snprintf(path, sizeof(path), "%s/%s",
                              source->backend.directory, name);
        int fd;

        if (length < 0 || (size_t)length >= sizeof(path))
            return false;

        fd = open(path, O_RDONLY, 0777);
        if (fd < 0)
            return false;

        file->type = RESOURCE_SOURCE_DIRECTORY;
        file->backend.fd = fd;
        return true;
    }

    if (source->type == RESOURCE_SOURCE_ZIP)
    {
        if (!zip_entry_open(&source->backend.zip, name, &file->backend.zip))
            return false;

        file->type = RESOURCE_SOURCE_ZIP;
        return true;
    }

    return false;
}

size_t resource_file_read(resource_file_t *file, void *dst, size_t size)
{
    if (file == NULL || dst == NULL || size == 0)
        return 0;

    if (file->type == RESOURCE_SOURCE_DIRECTORY)
    {
        ssize_t result = read(file->backend.fd, dst, size);
        return result < 0 ? 0 : (size_t)result;
    }

    if (file->type == RESOURCE_SOURCE_ZIP)
        return zip_entry_read(&file->backend.zip, dst, size);

    return 0;
}

bool resource_file_close(resource_file_t *file)
{
    bool ok = true;

    if (file == NULL)
        return false;

    if (file->type == RESOURCE_SOURCE_DIRECTORY)
        ok = close(file->backend.fd) == 0;
    else if (file->type == RESOURCE_SOURCE_ZIP)
        ok = zip_entry_close(&file->backend.zip);

    memset(file, 0, sizeof(*file));
    return ok;
}
