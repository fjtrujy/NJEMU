#include "file.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

void host_buffer_init(host_buffer_t *buffer)
{
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;
}

void host_buffer_free(host_buffer_t *buffer)
{
    free(buffer->data);
    host_buffer_init(buffer);
}

int host_buffer_reserve(host_buffer_t *buffer, size_t capacity)
{
    uint8_t *data;
    size_t next;

    if (capacity <= buffer->capacity)
        return 1;
    next = buffer->capacity != 0 ? buffer->capacity : 256;
    while (next < capacity) {
        if (next > (size_t)-1 / 2) {
            next = capacity;
            break;
        }
        next *= 2;
    }
    data = (uint8_t *)realloc(buffer->data, next);
    if (data == NULL)
        return 0;
    buffer->data = data;
    buffer->capacity = next;
    return 1;
}

int host_buffer_append(host_buffer_t *buffer, const void *data, size_t size)
{
    if (size > (size_t)-1 - buffer->size)
        return 0;
    if (!host_buffer_reserve(buffer, buffer->size + size))
        return 0;
    if (size != 0)
        memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return 1;
}

int host_buffer_append_byte(host_buffer_t *buffer, uint8_t value)
{
    return host_buffer_append(buffer, &value, 1);
}

int host_buffer_append_string(host_buffer_t *buffer, const char *text)
{
    return host_buffer_append(buffer, text, strlen(text));
}

int host_buffer_append_format(host_buffer_t *buffer, const char *format, ...)
{
    va_list args;
    va_list copy;
    int length;
    char *target;

    va_start(args, format);
    va_copy(copy, args);
    length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (length < 0 || !host_buffer_reserve(buffer, buffer->size + (size_t)length + 1)) {
        va_end(args);
        return 0;
    }
    target = (char *)buffer->data + buffer->size;
    if (vsnprintf(target, (size_t)length + 1, format, args) != length) {
        va_end(args);
        return 0;
    }
    va_end(args);
    buffer->size += (size_t)length;
    return 1;
}

int host_read_file(const char *path, host_buffer_t *buffer)
{
    FILE *file;
    long length;
    size_t got;

    host_buffer_free(buffer);
    file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    if (!host_buffer_reserve(buffer, (size_t)length + 1)) {
        fclose(file);
        return 0;
    }
    got = fread(buffer->data, 1, (size_t)length, file);
    if (got != (size_t)length || ferror(file)) {
        fclose(file);
        host_buffer_free(buffer);
        return 0;
    }
    fclose(file);
    buffer->size = got;
    buffer->data[buffer->size] = 0;
    return 1;
}

int host_write_file(const char *path, const void *data, size_t size)
{
    FILE *file = fopen(path, "wb");

    if (file == NULL)
        return 0;
    if (size != 0 && fwrite(data, 1, size, file) != size) {
        fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

int host_write_file_if_different(const char *path, const void *data, size_t size)
{
    host_buffer_t current;
    int same = 0;

    host_buffer_init(&current);
    if (host_read_file(path, &current))
        same = current.size == size && (size == 0 || memcmp(current.data, data, size) == 0);
    host_buffer_free(&current);
    return same || host_write_file(path, data, size);
}

int host_file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL)
        return 0;
    fclose(file);
    return 1;
}

static int make_one_directory(const char *path)
{
#ifdef _WIN32
    if (_mkdir(path) == 0 || errno == EEXIST)
#else
    if (mkdir(path, 0777) == 0 || errno == EEXIST)
#endif
        return 1;
    return 0;
}

int host_make_directories(const char *path)
{
    char *copy;
    char *cursor;
    size_t length;

    if (path == NULL || *path == '\0')
        return 1;
    copy = (char *)malloc(strlen(path) + 1);
    if (copy == NULL)
        return 0;
    strcpy(copy, path);
    length = strlen(copy);
    while (length > 1 && (copy[length - 1] == '/' || copy[length - 1] == '\\'))
        copy[--length] = '\0';
    for (cursor = copy + 1; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') {
            char separator = *cursor;
            *cursor = '\0';
            if (*copy != '\0' && !make_one_directory(copy)) {
                free(copy);
                return 0;
            }
            *cursor = separator;
        }
    }
    if (!make_one_directory(copy)) {
        free(copy);
        return 0;
    }
    free(copy);
    return 1;
}

int host_make_parent_directories(const char *path)
{
    char *copy;
    char *slash;
    char *backslash;
    int result;

    copy = (char *)malloc(strlen(path) + 1);
    if (copy == NULL)
        return 0;
    strcpy(copy, path);
    slash = strrchr(copy, '/');
    backslash = strrchr(copy, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash))
        slash = backslash;
    if (slash == NULL) {
        free(copy);
        return 1;
    }
    if (slash == copy) {
        free(copy);
        return 1;
    }
    *slash = '\0';
    result = host_make_directories(copy);
    free(copy);
    return result;
}
