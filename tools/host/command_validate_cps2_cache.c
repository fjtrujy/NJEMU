#include "commands.h"

#include "text.h"
#include "tsv.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const layout_fields[] = {
    "name",
    "object_start", "object_end",
    "scroll1_start", "scroll1_end",
    "scroll2_start", "scroll2_end",
    "scroll3_start", "scroll3_end",
    "object2_start", "object2_end",
};

static int is_ascii(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    for (; *p != '\0'; ++p) {
        if (*p >= 0x80)
            return 0;
    }
    return 1;
}

static int metadata_has_name(const host_tsv_t *metadata, size_t name_column, const char *name)
{
    size_t i;
    for (i = 0; i < metadata->row_count; ++i) {
        if (metadata->rows[i].count > name_column
            && strcmp(metadata->rows[i].fields[name_column], name) == 0)
            return 1;
    }
    return 0;
}

static int name_seen_before(const host_tsv_t *layouts, size_t row_index, const char *name)
{
    size_t i;
    for (i = 0; i < row_index; ++i) {
        if (strcmp(layouts->rows[i].fields[0], name) == 0)
            return 1;
    }
    return 0;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: njemu-tool validate-cps2-cache --layouts FILE --metadata FILE\n");
}

int command_validate_cps2_cache(int argc, char **argv)
{
    const char *layouts_path = NULL;
    const char *metadata_path = NULL;
    host_tsv_t layouts;
    host_tsv_t metadata;
    size_t metadata_name_column = 0;
    char error[256];
    size_t i;
    int arg;
    int result = 1;

    for (arg = 1; arg < argc; ++arg) {
        if (strcmp(argv[arg], "--layouts") == 0 && arg + 1 < argc)
            layouts_path = argv[++arg];
        else if (strcmp(argv[arg], "--metadata") == 0 && arg + 1 < argc)
            metadata_path = argv[++arg];
        else {
            usage();
            return 2;
        }
    }
    if (layouts_path == NULL || metadata_path == NULL) {
        usage();
        return 2;
    }

    host_tsv_init(&layouts);
    host_tsv_init(&metadata);
    if (!host_tsv_load(metadata_path, &metadata, error, sizeof(error))) {
        fprintf(stderr, "cps2_cache_layouts: %s\n", error);
        goto out;
    }
    if (!host_tsv_column(&metadata, "name", &metadata_name_column)) {
        fprintf(stderr, "cps2_cache_layouts: %s: missing name column\n", metadata_path);
        goto out;
    }
    if (!host_tsv_load(layouts_path, &layouts, error, sizeof(error))) {
        fprintf(stderr, "cps2_cache_layouts: %s\n", error);
        goto out;
    }
    if (!host_tsv_header_equals(&layouts, layout_fields,
            sizeof(layout_fields) / sizeof(layout_fields[0]))) {
        fprintf(stderr, "cps2_cache_layouts: %s: unexpected columns\n", layouts_path);
        goto out;
    }
    if (layouts.row_count == 0) {
        fprintf(stderr, "cps2_cache_layouts: %s: no cache layouts\n", layouts_path);
        goto out;
    }

    for (i = 0; i < layouts.row_count; ++i) {
        const host_tsv_row_t *row = &layouts.rows[i];
        const char *name;
        uint32_t values[10];
        size_t field;

        if (row->count != sizeof(layout_fields) / sizeof(layout_fields[0])) {
            fprintf(stderr, "cps2_cache_layouts: %s:%lu: expected %lu fields\n",
                layouts_path, (unsigned long)row->line_number,
                (unsigned long)(sizeof(layout_fields) / sizeof(layout_fields[0])));
            goto out;
        }
        name = row->fields[0];
        if (*name == '\0') {
            fprintf(stderr, "cps2_cache_layouts: %s:%lu: empty name\n",
                layouts_path, (unsigned long)row->line_number);
            goto out;
        }
        if (strlen(name) > 15 || !is_ascii(name)) {
            fprintf(stderr, "cps2_cache_layouts: %s:%lu: invalid name '%s'\n",
                layouts_path, (unsigned long)row->line_number, name);
            goto out;
        }
        if (name_seen_before(&layouts, i, name)) {
            fprintf(stderr, "cps2_cache_layouts: %s:%lu: duplicate name %s\n",
                layouts_path, (unsigned long)row->line_number, name);
            goto out;
        }
        if (!metadata_has_name(&metadata, metadata_name_column, name)) {
            fprintf(stderr, "cps2_cache_layouts: %s:%lu: unknown CPS2 game %s\n",
                layouts_path, (unsigned long)row->line_number, name);
            goto out;
        }

        for (field = 1; field < row->count; ++field) {
            char *trimmed = host_trim(row->fields[field]);
            if (!host_parse_u32(trimmed, &values[field - 1])) {
                fprintf(stderr, "cps2_cache_layouts: %s:%lu: invalid %s value '%s'\n",
                    layouts_path, (unsigned long)row->line_number,
                    layout_fields[field], trimmed);
                goto out;
            }
        }
        for (field = 0; field < 10; field += 2) {
            uint32_t start = values[field];
            uint32_t end = values[field + 1];
            if (start == 0 && end == 0)
                continue;
            if (start > end) {
                fprintf(stderr, "cps2_cache_layouts: %s:%lu: %s exceeds %s\n",
                    layouts_path, (unsigned long)row->line_number,
                    layout_fields[field + 1], layout_fields[field + 2]);
                goto out;
            }
        }
    }

    printf("validated %lu CPS2 ROMCNV cache layouts\n", (unsigned long)layouts.row_count);
    result = 0;
out:
    host_tsv_free(&layouts);
    host_tsv_free(&metadata);
    return result;
}
