#ifndef NJEMU_HOST_ROMINFO_H
#define NJEMU_HOST_ROMINFO_H

#include <stddef.h>
#include <stdint.h>

typedef struct host_rom_record {
    uint32_t load_type;
    char *name;
    uint32_t offset;
    uint32_t length;
    uint32_t crc;
    uint32_t group;
    uint32_t skip;
    int is_romx;
} host_rom_record_t;

typedef struct host_region_record {
    char *name;
    uint32_t size;
    uint32_t flags;
    host_rom_record_t *roms;
    size_t rom_count;
} host_region_record_t;

typedef struct host_game_record {
    char *name;
    char *parent;
    uint32_t machine;
    uint32_t input;
    uint32_t init;
    uint32_t rotation;
    host_region_record_t *regions;
    size_t region_count;
} host_game_record_t;

typedef struct host_rominfo {
    host_game_record_t *games;
    size_t game_count;
} host_rominfo_t;

void host_rominfo_init(host_rominfo_t *rominfo);
void host_rominfo_free(host_rominfo_t *rominfo);
int host_rominfo_load(const char *path, host_rominfo_t *rominfo, char *error, size_t error_size);
int host_rominfo_load_filenames(const char *path, host_rominfo_t *rominfo,
    char *error, size_t error_size);
int host_rominfo_validate_parents(const host_rominfo_t *rominfo, const char *root_parent,
    char *error, size_t error_size);
const host_game_record_t *host_rominfo_find(const host_rominfo_t *rominfo, const char *name);

#endif
