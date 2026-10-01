#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include "mvs/memintrf.h"
#include "mvs/processed_assets.h"
#include "common/path_utils.h"
#include "common/runtime_paths.h"
#include "common/zip_archive.h"

static int processed_asset_uses_parent(int type)
{
    switch (type)
    {
    case MVS_PROCESSED_CROM: return use_parent_crom;
    case MVS_PROCESSED_SROM: return use_parent_srom;
    case MVS_PROCESSED_VROM: return use_parent_vrom;
    case MVS_PROCESSED_INFO:
    default:
        return use_parent_crom && use_parent_srom && use_parent_vrom;
    }
}

static const char *processed_asset_name(int type)
{
    switch (type)
    {
    case MVS_PROCESSED_INFO: return "cache_info";
    case MVS_PROCESSED_CROM: return "crom";
    case MVS_PROCESSED_SROM: return "srom";
    case MVS_PROCESSED_VROM: return "vrom";
    default: return NULL;
    }
}

int mvs_processed_asset_open(int type)
{
    const char *name = processed_asset_name(type);
    char path[PATH_MAX];
    int fd = -1;

    if (name == NULL)
        return -1;
    if (processed_asset_uses_parent(type) && parent_name[0])
    {
        if (path_format(path, sizeof(path), "%s/%s_cache/%s", cache_dir, parent_name, name))
            fd = open(path, O_RDONLY, 0777);
    }
    if (fd < 0 && path_format(path, sizeof(path), "%s/%s_cache/%s", cache_dir, game_name, name))
        fd = open(path, O_RDONLY, 0777);
    return fd;
}

size_t mvs_processed_asset_zip_read(int type, const char *name, void *buf, size_t size)
{
    zip_archive_t archive = {0};
    zip_entry_t entry = {0};
    char path[PATH_MAX];
    size_t bytes = 0;

    if (processed_asset_uses_parent(type) && parent_name[0] &&
        path_format(path, sizeof(path), "%s/%s_cache.zip", cache_dir, parent_name) &&
        zip_archive_open(&archive, path))
    {
        if (zip_entry_open(&archive, name, &entry))
        {
            bytes = zip_entry_read(&entry, buf, size);
            if (!zip_entry_close(&entry)) bytes = 0;
            zip_archive_close(&archive);
            return bytes;
        }
        zip_archive_close(&archive);
    }

    if (!path_format(path, sizeof(path), "%s/%s_cache.zip", cache_dir, game_name) ||
        !zip_archive_open(&archive, path))
        return 0;
    if (zip_entry_open(&archive, name, &entry))
    {
        bytes = zip_entry_read(&entry, buf, size);
        if (!zip_entry_close(&entry)) bytes = 0;
    }
    zip_archive_close(&archive);
    return bytes;
}


static bool processed_asset_read_exact_fd(int fd, void *dst, size_t size)
{
    uint8_t *out = dst;
    size_t done = 0;

    while (done < size)
    {
        ssize_t got = read(fd, out + done, size - done);
        if (got <= 0)
            return false;
        done += (size_t)got;
    }
    return true;
}

static bool processed_asset_open_zip(int type, zip_archive_t *archive)
{
    char path[PATH_MAX];

    if (processed_asset_uses_parent(type) && parent_name[0] &&
        path_format(path, sizeof(path), "%s/%s_cache.zip", cache_dir, parent_name) &&
        zip_archive_open(archive, path))
        return true;

    return path_format(path, sizeof(path), "%s/%s_cache.zip", cache_dir, game_name) &&
        zip_archive_open(archive, path);
}

bool mvs_processed_info_read(char version[8], uint8_t *usage, size_t usage_size)
{
    int fd = mvs_processed_asset_open(MVS_PROCESSED_INFO);

    if (fd >= 0)
    {
        bool ok = processed_asset_read_exact_fd(fd, version, 8) &&
            processed_asset_read_exact_fd(fd, usage, usage_size);
        close(fd);
        return ok;
    }

    zip_archive_t archive = {0};
    zip_entry_t entry = {0};
    bool ok = false;

    if (!processed_asset_open_zip(MVS_PROCESSED_INFO, &archive))
        return false;
    if (zip_entry_open(&archive, "cache_info", &entry))
    {
        ok = zip_entry_read(&entry, version, 8) == 8 &&
            zip_entry_read(&entry, usage, usage_size) == usage_size &&
            zip_entry_close(&entry);
    }
    zip_archive_close(&archive);
    return ok;
}

bool mvs_processed_crom_read(uint8_t *dst, size_t size)
{
    int fd = mvs_processed_asset_open(MVS_PROCESSED_CROM);

    if (fd >= 0)
    {
        bool ok = processed_asset_read_exact_fd(fd, dst, size);
        close(fd);
        return ok;
    }

    zip_archive_t archive = {0};
    bool ok = false;
    size_t offset = 0;
    unsigned block = 0;

    if (!processed_asset_open_zip(MVS_PROCESSED_CROM, &archive))
        return false;

    ok = true;
    while (offset < size)
    {
        static const char hex[] = "0123456789abcdef";
        char name[4];
        zip_entry_t entry = {0};
        size_t bytes = size - offset;

        if (bytes > 0x10000)
            bytes = 0x10000;
        if (block > 0xfff)
        {
            ok = false;
            break;
        }
        name[0] = hex[(block >> 8) & 0xf];
        name[1] = hex[(block >> 4) & 0xf];
        name[2] = hex[block & 0xf];
        name[3] = '\0';

        if (!zip_entry_open(&archive, name, &entry) ||
            zip_entry_read(&entry, dst + offset, bytes) != bytes ||
            !zip_entry_close(&entry))
        {
            if (zip_entry_is_open(&entry))
                zip_entry_close(&entry);
            ok = false;
            break;
        }
        offset += bytes;
        block++;
    }
    zip_archive_close(&archive);
    return ok;
}
