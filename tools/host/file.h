#ifndef NJEMU_HOST_FILE_H
#define NJEMU_HOST_FILE_H

#include <stddef.h>
#include <stdint.h>

typedef struct host_buffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
} host_buffer_t;

void host_buffer_init(host_buffer_t *buffer);
void host_buffer_free(host_buffer_t *buffer);
int host_buffer_reserve(host_buffer_t *buffer, size_t capacity);
int host_buffer_append(host_buffer_t *buffer, const void *data, size_t size);
int host_buffer_append_byte(host_buffer_t *buffer, uint8_t value);
int host_buffer_append_string(host_buffer_t *buffer, const char *text);
int host_buffer_append_format(host_buffer_t *buffer, const char *format, ...);

int host_read_file(const char *path, host_buffer_t *buffer);
int host_write_file(const char *path, const void *data, size_t size);
int host_write_file_if_different(const char *path, const void *data, size_t size);
int host_file_exists(const char *path);
int host_make_directories(const char *path);
int host_make_parent_directories(const char *path);

#endif
