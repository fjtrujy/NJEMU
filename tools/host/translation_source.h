#ifndef NJEMU_HOST_TRANSLATION_SOURCE_H
#define NJEMU_HOST_TRANSLATION_SOURCE_H

#include <stddef.h>
#include <stdint.h>

typedef struct host_translation_value {
    uint8_t *data;
    size_t size;
    int is_null;
} host_translation_value_t;

typedef struct host_translation_manifest {
    char **names;
    size_t count;
} host_translation_manifest_t;

typedef struct host_translation_catalog {
    host_translation_value_t *values;
    size_t count;
    size_t total_bytes;
} host_translation_catalog_t;

void host_translation_manifest_init(host_translation_manifest_t *manifest);
void host_translation_manifest_free(host_translation_manifest_t *manifest);
void host_translation_catalog_init(host_translation_catalog_t *catalog);
void host_translation_catalog_free(host_translation_catalog_t *catalog);
int host_translation_load_manifest(const char *path, const char *macro_name,
    host_translation_manifest_t *manifest, char *error, size_t error_size);
int host_translation_load_catalog(const char *path,
    const host_translation_manifest_t *manifest, host_translation_catalog_t *catalog,
    char *error, size_t error_size);
int host_translation_validate_printf(const host_translation_manifest_t *manifest,
    const host_translation_catalog_t *english, const host_translation_catalog_t *candidate,
    const char *candidate_language, char *error, size_t error_size);

#endif
