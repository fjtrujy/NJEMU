#ifndef NJEMU_HOST_GAME_DATABASE_H
#define NJEMU_HOST_GAME_DATABASE_H

#include <stddef.h>

#include "file.h"
#include "game_metadata.h"
#include "rominfo.h"

typedef struct host_game_database_stats {
    size_t games;
    size_t regions;
    size_t roms;
} host_game_database_stats_t;

int host_game_database_build(const host_game_metadata_t *metadata,
    const host_rominfo_t *rominfo, host_buffer_t *output,
    host_game_database_stats_t *stats, char *error, size_t error_size);

#endif
