#ifndef MVS_PROCESSED_ASSETS_H
#define MVS_PROCESSED_ASSETS_H

#include <stddef.h>

enum
{
    MVS_PROCESSED_INFO = 0,
    MVS_PROCESSED_CROM,
    MVS_PROCESSED_SROM,
    MVS_PROCESSED_VROM
};

int mvs_processed_asset_open(int type);
size_t mvs_processed_asset_zip_read(int type, const char *name, void *buf, size_t size);

#endif
