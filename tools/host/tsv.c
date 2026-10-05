#include "tsv.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int line_is_blank(const char *line)
{
    while (*line != '\0') {
        if (!isspace((unsigned char)*line))
            return 0;
        ++line;
    }
    return 1;
}

static int split_row(char *line, size_t line_number, host_tsv_row_t *row)
{
    char *cursor;
    size_t count = 1;
    size_t index = 0;

    for (cursor = line; *cursor != '\0'; ++cursor) {
        if (*cursor == '\t')
            ++count;
    }
    row->fields = (char **)malloc(count * sizeof(*row->fields));
    if (row->fields == NULL)
        return 0;
    row->count = count;
    row->line_number = line_number;
    row->fields[index++] = line;
    for (cursor = line; *cursor != '\0'; ++cursor) {
        if (*cursor == '\t') {
            *cursor = '\0';
            row->fields[index++] = cursor + 1;
        }
    }
    return 1;
}

void host_tsv_init(host_tsv_t *table)
{
    host_buffer_init(&table->storage);
    table->header.fields = NULL;
    table->header.count = 0;
    table->header.line_number = 0;
    table->rows = NULL;
    table->row_count = 0;
}

void host_tsv_free(host_tsv_t *table)
{
    size_t i;

    free(table->header.fields);
    for (i = 0; i < table->row_count; ++i)
        free(table->rows[i].fields);
    free(table->rows);
    host_buffer_free(&table->storage);
    host_tsv_init(table);
}

int host_tsv_load(const char *path, host_tsv_t *table, char *error, size_t error_size)
{
    char *cursor;
    size_t line_number = 0;

    host_tsv_free(table);
    if (!host_read_file(path, &table->storage)) {
        snprintf(error, error_size, "could not read %s", path);
        return 0;
    }

    cursor = (char *)table->storage.data;
    while (*cursor != '\0') {
        char *line = cursor;
        char *end = strchr(cursor, '\n');
        host_tsv_row_t row;
        host_tsv_row_t *rows;

        ++line_number;
        if (end != NULL) {
            *end = '\0';
            cursor = end + 1;
        } else {
            cursor += strlen(cursor);
        }
        end = line + strlen(line);
        if (end > line && end[-1] == '\r')
            end[-1] = '\0';
        if (line_is_blank(line) || line[0] == '#')
            continue;
        if (!split_row(line, line_number, &row)) {
            snprintf(error, error_size, "out of memory parsing %s", path);
            host_tsv_free(table);
            return 0;
        }
        if (table->header.fields == NULL) {
            table->header = row;
            continue;
        }
        rows = (host_tsv_row_t *)realloc(
            table->rows, (table->row_count + 1) * sizeof(*table->rows));
        if (rows == NULL) {
            free(row.fields);
            snprintf(error, error_size, "out of memory parsing %s", path);
            host_tsv_free(table);
            return 0;
        }
        table->rows = rows;
        table->rows[table->row_count++] = row;
    }

    if (table->header.fields == NULL) {
        snprintf(error, error_size, "%s: missing TSV header", path);
        host_tsv_free(table);
        return 0;
    }
    return 1;
}

int host_tsv_header_equals(const host_tsv_t *table, const char *const *fields, size_t count)
{
    size_t i;

    if (table->header.count != count)
        return 0;
    for (i = 0; i < count; ++i) {
        if (strcmp(table->header.fields[i], fields[i]) != 0)
            return 0;
    }
    return 1;
}

int host_tsv_column(const host_tsv_t *table, const char *name, size_t *index)
{
    size_t i;

    for (i = 0; i < table->header.count; ++i) {
        if (strcmp(table->header.fields[i], name) == 0) {
            *index = i;
            return 1;
        }
    }
    return 0;
}
