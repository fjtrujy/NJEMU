#include "string_pool.h"

#include <stdlib.h>
#include <string.h>

int host_string_pool_init(host_string_pool_t *pool)
{
    host_buffer_init(&pool->data);
    pool->offsets = NULL;
    pool->count = 0;
    return host_buffer_append_byte(&pool->data, 0);
}

void host_string_pool_free(host_string_pool_t *pool)
{
    host_buffer_free(&pool->data);
    free(pool->offsets);
    pool->offsets = NULL;
    pool->count = 0;
}

int host_string_pool_add(host_string_pool_t *pool, const char *text, uint32_t *offset)
{
    size_t i;
    size_t length;
    uint32_t *offsets;

    if (*text == '\0') {
        *offset = 0;
        return 1;
    }
    for (i = 0; i < pool->count; ++i) {
        const char *existing = (const char *)pool->data.data + pool->offsets[i];
        if (strcmp(existing, text) == 0) {
            *offset = pool->offsets[i];
            return 1;
        }
    }
    length = strlen(text) + 1;
    if (pool->data.size > 0xffffffffu || length > 0xffffffffu - pool->data.size)
        return 0;
    *offset = (uint32_t)pool->data.size;
    if (!host_buffer_append(&pool->data, text, length))
        return 0;
    offsets = (uint32_t *)realloc(pool->offsets, (pool->count + 1) * sizeof(*pool->offsets));
    if (offsets == NULL)
        return 0;
    pool->offsets = offsets;
    pool->offsets[pool->count++] = *offset;
    return 1;
}
