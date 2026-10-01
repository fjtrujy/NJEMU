#include <fcntl.h>
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
