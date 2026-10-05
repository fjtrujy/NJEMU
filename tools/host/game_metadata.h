#ifndef NJEMU_HOST_GAME_METADATA_H
#define NJEMU_HOST_GAME_METADATA_H

#include <stddef.h>
#include <stdint.h>

#include "file.h"
#include "rominfo.h"

typedef enum host_game_core {
    HOST_CORE_CPS1 = 1,
    HOST_CORE_CPS2 = 2,
    HOST_CORE_MVS = 3,
    HOST_CORE_NCDZ = 4
} host_game_core_t;

typedef struct host_metadata_record {
    char *name;
    char *titles[4];
    uint8_t display_flags;
    uint8_t core_flags;
    char *aux_name;
    uint32_t data[3];
} host_metadata_record_t;

typedef struct host_game_metadata {
    host_game_core_t core;
    host_metadata_record_t *records;
    size_t count;
} host_game_metadata_t;

void host_game_metadata_init(host_game_metadata_t *metadata);
void host_game_metadata_free(host_game_metadata_t *metadata);
int host_game_core_from_name(const char *name, host_game_core_t *core);
const char *host_game_core_name(host_game_core_t core);
int host_game_metadata_load(const char *path, host_game_core_t core,
    host_game_metadata_t *metadata, char *error, size_t error_size);
int host_game_metadata_validate(const host_game_metadata_t *metadata,
    const host_rominfo_t *rominfo, char *error, size_t error_size);
const host_metadata_record_t *host_game_metadata_find(
    const host_game_metadata_t *metadata, const char *name);
int host_game_metadata_sorted(const host_game_metadata_t *metadata,
    const host_metadata_record_t ***records);
int host_game_metadata_build_blob(const host_game_metadata_t *metadata,
    host_buffer_t *output, char *error, size_t error_size);
int host_game_metadata_build_gamelist(const host_game_metadata_t *metadata,
    host_buffer_t *output, char *error, size_t error_size);

#endif
