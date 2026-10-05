#ifndef NJEMU_HOST_STRING_POOL_H
#define NJEMU_HOST_STRING_POOL_H

#include <stddef.h>
#include <stdint.h>

#include "file.h"

typedef struct host_string_pool {
    host_buffer_t data;
    uint32_t *offsets;
    size_t count;
} host_string_pool_t;

int host_string_pool_init(host_string_pool_t *pool);
void host_string_pool_free(host_string_pool_t *pool);
int host_string_pool_add(host_string_pool_t *pool, const char *text, uint32_t *offset);

#endif
