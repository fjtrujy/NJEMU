#ifndef MVS_PROCESSED_ASSETS_H
#define MVS_PROCESSED_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
    MVS_PROCESSED_INFO = 0,
    MVS_PROCESSED_CROM,
    MVS_PROCESSED_SROM,
    MVS_PROCESSED_VROM
};

int mvs_processed_asset_open(int type);
size_t mvs_processed_asset_zip_read(int type, const char *name, void *buf, size_t size);
bool mvs_processed_info_read(char version[8], uint8_t *usage, size_t usage_size);
bool mvs_processed_crom_read(uint8_t *dst, size_t size);

#endif
