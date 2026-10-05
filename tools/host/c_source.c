#include "c_source.h"

#include "file.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_identifier_char(unsigned char value)
{
    return isalnum(value) || value == '_';
}

static const char *find_array_body(const char *text, const char *name)
{
    size_t name_length = strlen(name);
    const char *cursor = text;

    while ((cursor = strstr(cursor, name)) != NULL) {
        const char *p;
        if ((cursor == text || !is_identifier_char((unsigned char)cursor[-1]))
            && !is_identifier_char((unsigned char)cursor[name_length])) {
            p = cursor + name_length;
            while (isspace((unsigned char)*p))
                ++p;
            if (*p == '[') {
                p = strchr(p, ']');
                if (p != NULL) {
                    ++p;
                    while (isspace((unsigned char)*p))
                        ++p;
                    if (*p == '=') {
                        ++p;
                        while (isspace((unsigned char)*p))
                            ++p;
                        if (*p == '{')
                            return p + 1;
                    }
                }
            }
        }
        cursor += name_length;
    }
    return NULL;
}

static int append_value(host_u32_array_t *array, size_t *capacity, uint32_t value)
{
    uint32_t *values;
    size_t next;

    if (array->count < *capacity) {
        array->values[array->count++] = value;
        return 1;
    }
    next = *capacity != 0 ? *capacity * 2 : 256;
    if (next < *capacity || next > (size_t)-1 / sizeof(*values))
        return 0;
    values = (uint32_t *)realloc(array->values, next * sizeof(*values));
    if (values == NULL)
        return 0;
    array->values = values;
    *capacity = next;
    array->values[array->count++] = value;
    return 1;
}

void host_u32_array_free(host_u32_array_t *array)
{
    free(array->values);
    array->values = NULL;
    array->count = 0;
}

int host_parse_c_u32_array(const char *path, const char *name, host_u32_array_t *array,
    char *error, size_t error_size)
{
    host_buffer_t file;
    const char *cursor;
    size_t capacity = 0;

    array->values = NULL;
    array->count = 0;
    host_buffer_init(&file);
    if (!host_read_file(path, &file)) {
        snprintf(error, error_size, "could not read %s", path);
        return 0;
    }
    cursor = find_array_body((const char *)file.data, name);
    if (cursor == NULL) {
        snprintf(error, error_size, "missing C array %s", name);
        host_buffer_free(&file);
        return 0;
    }

    while (*cursor != '\0') {
        char *end;
        unsigned long value;

        if (cursor[0] == '/' && cursor[1] == '*') {
            const char *comment_end = strstr(cursor + 2, "*/");
            if (comment_end == NULL) {
                snprintf(error, error_size, "unterminated block comment in %s", name);
                goto fail;
            }
            cursor = comment_end + 2;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '/') {
            const char *line_end = strchr(cursor + 2, '\n');
            cursor = line_end != NULL ? line_end + 1 : cursor + strlen(cursor);
            continue;
        }
        if (*cursor == '}') {
            host_buffer_free(&file);
            return 1;
        }
        if (!isdigit((unsigned char)*cursor)) {
            ++cursor;
            continue;
        }

        value = strtoul(cursor, &end, 0);
        if (end == cursor || value > 0xfffffffful) {
            snprintf(error, error_size, "invalid numeric value in %s", name);
            goto fail;
        }
        if (!append_value(array, &capacity, (uint32_t)value)) {
            snprintf(error, error_size, "out of memory parsing %s", name);
            goto fail;
        }
        cursor = end;
    }

    snprintf(error, error_size, "unterminated C array %s", name);
fail:
    host_buffer_free(&file);
    host_u32_array_free(array);
    return 0;
}
