#ifndef NJEMU_HOST_C_SOURCE_H
#define NJEMU_HOST_C_SOURCE_H

#include <stddef.h>
#include <stdint.h>

typedef struct host_u32_array {
    uint32_t *values;
    size_t count;
} host_u32_array_t;

void host_u32_array_free(host_u32_array_t *array);
int host_parse_c_u32_array(const char *path, const char *name, host_u32_array_t *array,
    char *error, size_t error_size);

#endif
