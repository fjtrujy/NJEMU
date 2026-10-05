#include "commands.h"

#include "c_source.h"
#include "file.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GLYPH_COUNT 0x5e80u
#define GLYPH_WIDTH 14u
#define GLYPH_HEIGHT 14u
#define GLYPH_BYTES ((GLYPH_WIDTH * GLYPH_HEIGHT) / 2u)
#define GBK_TABLE_COUNT 0x7dc0u

typedef struct metric_expectation {
    const char *name;
    uint32_t value;
} metric_expectation_t;

static int parse_array(const char *path, const char *name, host_u32_array_t *array)
{
    char error[256];

    if (host_parse_c_u32_array(path, name, array, error, sizeof(error)))
        return 1;
    fprintf(stderr, "font: %s\n", error);
    return 0;
}

static int validate_font_source(const char *path, host_buffer_t *bitmap)
{
    static const metric_expectation_t metrics[] = {
        {"gbk_s14_width", GLYPH_WIDTH},
        {"gbk_s14_height", GLYPH_HEIGHT},
        {"gbk_s14f_skipx", 0},
        {"gbk_s14p_skipx", 0},
        {"gbk_s14_skipy", 0},
        {"gbk_s14_pitch", GLYPH_WIDTH},
    };
    host_u32_array_t values;
    size_t expected_bitmap_size = GLYPH_COUNT * GLYPH_BYTES;
    size_t i;

    if (!parse_array(path, "gbk_s14", &values))
        return 0;
    if (values.count != expected_bitmap_size) {
        fprintf(stderr, "font: gbk_s14 has %lu bytes, expected %lu\n",
            (unsigned long)values.count, (unsigned long)expected_bitmap_size);
        host_u32_array_free(&values);
        return 0;
    }
    if (!host_buffer_reserve(bitmap, expected_bitmap_size)) {
        fprintf(stderr, "font: out of memory building font bitmap\n");
        host_u32_array_free(&values);
        return 0;
    }
    for (i = 0; i < values.count; ++i) {
        if (values.values[i] > 0xffu) {
            fprintf(stderr, "font: gbk_s14[%lu] exceeds byte range\n", (unsigned long)i);
            host_u32_array_free(&values);
            return 0;
        }
        bitmap->data[i] = (uint8_t)values.values[i];
    }
    bitmap->size = expected_bitmap_size;
    host_u32_array_free(&values);

    if (!parse_array(path, "gbk_s14_pos", &values))
        return 0;
    if (values.count != GLYPH_COUNT) {
        fprintf(stderr, "font: gbk_s14_pos has %lu entries, expected %u\n",
            (unsigned long)values.count, GLYPH_COUNT);
        host_u32_array_free(&values);
        return 0;
    }
    for (i = 0; i < values.count; ++i) {
        uint32_t expected = (uint32_t)i * GLYPH_BYTES;
        if (values.values[i] != expected) {
            fprintf(stderr, "font: gbk_s14_pos[%lu]=%u, expected fixed offset %u\n",
                (unsigned long)i, values.values[i], expected);
            host_u32_array_free(&values);
            return 0;
        }
    }
    host_u32_array_free(&values);

    for (i = 0; i < sizeof(metrics) / sizeof(metrics[0]); ++i) {
        size_t entry;
        if (!parse_array(path, metrics[i].name, &values))
            return 0;
        if (values.count != GLYPH_COUNT) {
            fprintf(stderr, "font: %s has %lu entries, expected %u\n",
                metrics[i].name, (unsigned long)values.count, GLYPH_COUNT);
            host_u32_array_free(&values);
            return 0;
        }
        for (entry = 0; entry < values.count; ++entry) {
            if (values.values[entry] != metrics[i].value) {
                fprintf(stderr, "font: %s is not uniformly %u\n",
                    metrics[i].name, metrics[i].value);
                host_u32_array_free(&values);
                return 0;
            }
        }
        host_u32_array_free(&values);
    }
    return 1;
}

static int validate_gbk_table(const char *path)
{
    host_u32_array_t table;
    unsigned lead;

    if (!parse_array(path, "gbk_table", &table))
        return 0;
    if (table.count != GBK_TABLE_COUNT) {
        fprintf(stderr, "font: gbk_table has %lu entries, expected %u\n",
            (unsigned long)table.count, GBK_TABLE_COUNT);
        host_u32_array_free(&table);
        return 0;
    }
    for (lead = 0x81; lead < 0xff; ++lead) {
        unsigned trail;
        for (trail = 0x40; trail < 0xff; ++trail) {
            uint32_t table_index;
            uint32_t expected;
            if (trail == 0x7f || trail == 0xff)
                continue;
            table_index = ((uint32_t)lead << 8 | trail) - 0x8140u;
            expected = (lead - 0x81u) * 0xc0u + (trail - 0x40u);
            if (table.values[table_index] != expected) {
                fprintf(stderr,
                    "font: gbk_table[0x%x]=0x%x, expected arithmetic glyph 0x%x\n",
                    table_index, table.values[table_index], expected);
                host_u32_array_free(&table);
                return 0;
            }
        }
    }
    host_u32_array_free(&table);
    return 1;
}

static void usage(void)
{
    fprintf(stderr, "usage: njemu-tool font --source FILE --table-source FILE [--output FILE]\n");
}

int command_font(int argc, char **argv)
{
    const char *source = NULL;
    const char *table_source = NULL;
    const char *output = NULL;
    host_buffer_t bitmap;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--source") == 0 && i + 1 < argc)
            source = argv[++i];
        else if (strcmp(argv[i], "--table-source") == 0 && i + 1 < argc)
            table_source = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc)
            output = argv[++i];
        else {
            usage();
            return 2;
        }
    }
    if (source == NULL || table_source == NULL) {
        usage();
        return 2;
    }

    host_buffer_init(&bitmap);
    if (!validate_font_source(source, &bitmap) || !validate_gbk_table(table_source)) {
        host_buffer_free(&bitmap);
        return 1;
    }
    if (output != NULL && !host_write_file_if_different(output, bitmap.data, bitmap.size)) {
        fprintf(stderr, "font: could not write %s\n", output);
        host_buffer_free(&bitmap);
        return 1;
    }
    printf("validated %u fixed 14x14 glyphs (%lu bytes) and arithmetic GBK mapping\n",
        GLYPH_COUNT, (unsigned long)bitmap.size);
    host_buffer_free(&bitmap);
    return 0;
}
