#ifndef NJEMU_HOST_JSON_H
#define NJEMU_HOST_JSON_H

#include <stddef.h>
#include <stdint.h>

typedef enum host_json_type {
    HOST_JSON_NULL,
    HOST_JSON_BOOL,
    HOST_JSON_NUMBER,
    HOST_JSON_STRING,
    HOST_JSON_ARRAY,
    HOST_JSON_OBJECT
} host_json_type_t;

typedef struct host_json_value host_json_value_t;

typedef struct host_json_string {
    char *data;
    size_t length;
} host_json_string_t;

typedef struct host_json_array {
    host_json_value_t **items;
    size_t count;
} host_json_array_t;

typedef struct host_json_member {
    host_json_string_t key;
    host_json_value_t *value;
} host_json_member_t;

typedef struct host_json_object {
    host_json_member_t *members;
    size_t count;
} host_json_object_t;

struct host_json_value {
    host_json_type_t type;
    union {
        int boolean;
        int64_t number;
        host_json_string_t string;
        host_json_array_t array;
        host_json_object_t object;
    } as;
};

host_json_value_t *host_json_parse_file(const char *path, char *error, size_t error_size);
void host_json_free(host_json_value_t *value);
const host_json_value_t *host_json_object_get(const host_json_value_t *object, const char *key);
int host_json_object_has_exact_keys(const host_json_value_t *object,
    const char *const *keys, size_t count);

#endif
