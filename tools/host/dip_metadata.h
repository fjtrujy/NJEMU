#ifndef NJEMU_HOST_DIP_METADATA_H
#define NJEMU_HOST_DIP_METADATA_H

#include <stddef.h>

#include "file.h"

typedef struct host_dip_metadata_stats {
    size_t profiles;
    size_t localized_rows;
} host_dip_metadata_stats_t;

int host_dip_metadata_build(const char *source_path, host_buffer_t *output,
    host_dip_metadata_stats_t *stats, char *error, size_t error_size);

#endif
