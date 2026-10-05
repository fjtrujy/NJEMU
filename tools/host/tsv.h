#ifndef NJEMU_HOST_TSV_H
#define NJEMU_HOST_TSV_H

#include <stddef.h>

#include "file.h"

typedef struct host_tsv_row {
    char **fields;
    size_t count;
    size_t line_number;
} host_tsv_row_t;

typedef struct host_tsv {
    host_buffer_t storage;
    host_tsv_row_t header;
    host_tsv_row_t *rows;
    size_t row_count;
} host_tsv_t;

void host_tsv_init(host_tsv_t *table);
void host_tsv_free(host_tsv_t *table);
int host_tsv_load(const char *path, host_tsv_t *table, char *error, size_t error_size);
int host_tsv_header_equals(const host_tsv_t *table, const char *const *fields, size_t count);
int host_tsv_column(const host_tsv_t *table, const char *name, size_t *index);

#endif
