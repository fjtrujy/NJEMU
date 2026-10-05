#include "translations.h"

#include "endian.h"
#include "file.h"
#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACK_VERSION 2u
#define PACK_NULL_OFFSET 0xffffu
#define PACK_MAX_BLOB_SIZE 0xfffeu
#define PACK_HEADER_SIZE 20u

typedef struct gbk_unicode_entry {
    uint32_t codepoint;
    uint16_t glyph;
} gbk_unicode_entry_t;

static const gbk_unicode_entry_t gbk_unicode_map[] = {
#include "gbk_unicode_map.inc"
};

static const char *schema_compat_name(const char *name)
{
    static const struct {
        const char *current;
        const char *stable;
    } aliases[] = {
        {"COULD_NOT_OPEN_GAME_METADATA", "COULD_NOT_OPEN_ZIPNAME_DAT"},
        {"UNSUPPORTED_DEVICE_CONFIGURATION", "THIS_PROGRAM_REQUIRES_PSP2000"},
        {"CPU_CLOCK", "PSP_CLOCK"},
        {"DISPLAY_MODE", "STRETCH_SCREEN"},
        {"DISPLAY_ORIGINAL_SIZE", "STRETCH_320X224_4_3"},
        {"DISPLAY_ORIGINAL_ASPECT", "STRETCH_360X270_4_3"},
        {"DISPLAY_4_3", "STRETCH_366X270_19_14"},
        {"DISPLAY_FULLSCREEN", "STRETCH_384X270_24_17"},
        {"DISPLAY_RESERVED_1", "STRETCH_420X270_14_9"},
        {"DISPLAY_RESERVED_2", "STRETCH_466X272_12_7"},
        {"DISPLAY_RESERVED_3", "STRETCH_480X270_16_9"},
        {"WAITING_FOR_ANOTHER_PLAYER_TO_JOIN", "WAITING_FOR_ANOTHER_PSP_TO_JOIN"},
        {"INPUT_BUTTON_NAME1", "PSP_BUTTON_NAME1"},
        {"INPUT_BUTTON_NAME2", "PSP_BUTTON_NAME2"},
        {"INPUT_BUTTON_NAME3", "PSP_BUTTON_NAME3"},
        {"INPUT_BUTTON_NAME4", "PSP_BUTTON_NAME4"},
        {"INPUT_BUTTON_NAME5", "PSP_BUTTON_NAME5"},
        {"INPUT_BUTTON_NAME6", "PSP_BUTTON_NAME6"},
        {"INPUT_BUTTON_NAME7", "PSP_BUTTON_NAME7"},
        {"INPUT_BUTTON_NAME8", "PSP_BUTTON_NAME8"},
        {"INPUT_BUTTON_NAME9", "PSP_BUTTON_NAME9"},
        {"INPUT_BUTTON_NAME10", "PSP_BUTTON_NAME10"},
        {"INPUT_BUTTON_NAME11", "PSP_BUTTON_NAME11"},
        {"INPUT_BUTTON_NAME12", "PSP_BUTTON_NAME12"},
    };
    size_t i;
    for (i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i) {
        if (strcmp(name, aliases[i].current) == 0)
            return aliases[i].stable;
    }
    return name;
}

uint32_t host_translation_schema_hash(const host_translation_manifest_t *manifest)
{
    uint32_t value = 0x811c9dc5u;
    size_t i;
    for (i = 0; i < manifest->count; ++i) {
        char prefix[48];
        const char *name = schema_compat_name(manifest->names[i]);
        int length = snprintf(prefix, sizeof(prefix), "%lu:", (unsigned long)i);
        const unsigned char *cursor;
        size_t j;
        if (length < 0)
            return 0;
        for (j = 0; j < (size_t)length; ++j) {
            value ^= (uint8_t)prefix[j];
            value *= 0x01000193u;
        }
        cursor = (const unsigned char *)name;
        while (*cursor != '\0') {
            value ^= *cursor++;
            value *= 0x01000193u;
        }
        value ^= (uint8_t)'\n';
        value *= 0x01000193u;
    }
    return value;
}

int host_translation_build_pack(unsigned language_id,
    const host_translation_manifest_t *manifest,
    const host_translation_catalog_t *catalog,
    host_buffer_t *output, char *error, size_t error_size)
{
    host_buffer_t blob;
    uint16_t *offsets = NULL;
    size_t i;
    int ok = 0;

    host_buffer_free(output);
    host_buffer_init(output);
    host_buffer_init(&blob);
    if (language_id > 4) {
        snprintf(error, error_size, "unknown language id %u", language_id);
        goto out;
    }
    if (manifest->count != catalog->count || manifest->count > 0xffffu) {
        snprintf(error, error_size, ".lng V2 message count is invalid");
        goto out;
    }
    offsets = (uint16_t *)calloc(manifest->count, sizeof(*offsets));
    if (offsets == NULL && manifest->count != 0) {
        snprintf(error, error_size, "out of memory building translation pack");
        goto out;
    }
    for (i = 0; i < manifest->count; ++i) {
        const host_translation_value_t *value = &catalog->values[i];
        if (value->is_null) {
            offsets[i] = PACK_NULL_OFFSET;
            continue;
        }
        if (memchr(value->data, 0, value->size) != NULL) {
            snprintf(error, error_size,
                "message %lu: embedded NUL cannot be represented in .lng V2",
                (unsigned long)i);
            goto out;
        }
        if (blob.size > PACK_MAX_BLOB_SIZE) {
            snprintf(error, error_size, ".lng V2 string blob exceeds 16-bit offset limit");
            goto out;
        }
        offsets[i] = (uint16_t)blob.size;
        if (!host_buffer_append(&blob, value->data, value->size)
            || !host_buffer_append_byte(&blob, 0)) {
            snprintf(error, error_size, "out of memory building translation pack");
            goto out;
        }
    }
    if (blob.size > PACK_MAX_BLOB_SIZE) {
        snprintf(error, error_size,
            ".lng string blob is %lu bytes; .lng V2 allows at most %u",
            (unsigned long)blob.size, PACK_MAX_BLOB_SIZE);
        goto out;
    }
    if (!host_buffer_append(output, "NJTL", 4)
        || !host_append_le16(output, PACK_VERSION)
        || !host_append_le16(output, (uint16_t)language_id)
        || !host_append_le16(output, (uint16_t)manifest->count)
        || !host_append_le16(output, 0)
        || !host_append_le32(output, (uint32_t)blob.size)
        || !host_append_le32(output, host_translation_schema_hash(manifest))) {
        snprintf(error, error_size, "out of memory building translation pack");
        goto out;
    }
    for (i = 0; i < manifest->count; ++i) {
        if (!host_append_le16(output, offsets[i])) {
            snprintf(error, error_size, "out of memory building translation pack");
            goto out;
        }
    }
    if (!host_buffer_append(output, blob.data, blob.size)) {
        snprintf(error, error_size, "out of memory building translation pack");
        goto out;
    }
    ok = 1;
out:
    free(offsets);
    host_buffer_free(&blob);
    if (!ok)
        host_buffer_free(output);
    return ok;
}

int host_translation_validate_pack(const host_buffer_t *pack, unsigned language_id,
    const host_translation_manifest_t *manifest,
    const host_translation_catalog_t *catalog,
    char *error, size_t error_size)
{
    uint16_t version;
    uint16_t stored_language;
    uint16_t count;
    uint16_t reserved;
    uint32_t blob_size;
    uint32_t schema;
    size_t blob_start;
    size_t i;

    if (pack->size < PACK_HEADER_SIZE) {
        snprintf(error, error_size, ".lng file is smaller than the V2 header");
        return 0;
    }
    if (memcmp(pack->data, "NJTL", 4) != 0) {
        snprintf(error, error_size, "invalid .lng magic");
        return 0;
    }
    version = host_read_le16(pack->data + 4);
    stored_language = host_read_le16(pack->data + 6);
    count = host_read_le16(pack->data + 8);
    reserved = host_read_le16(pack->data + 10);
    blob_size = host_read_le32(pack->data + 12);
    schema = host_read_le32(pack->data + 16);
    if (version != PACK_VERSION || stored_language != language_id || reserved != 0
        || count != manifest->count || schema != host_translation_schema_hash(manifest)) {
        snprintf(error, error_size, "invalid .lng V2 header");
        return 0;
    }
    blob_start = PACK_HEADER_SIZE + (size_t)count * 2u;
    if (blob_size > PACK_MAX_BLOB_SIZE || blob_start > pack->size
        || blob_size != pack->size - blob_start) {
        snprintf(error, error_size, "invalid .lng V2 size");
        return 0;
    }
    for (i = 0; i < count; ++i) {
        uint16_t offset = host_read_le16(pack->data + PACK_HEADER_SIZE + i * 2u);
        const host_translation_value_t *expected = &catalog->values[i];
        const uint8_t *start;
        const uint8_t *terminator;
        size_t actual_size;
        if (expected->is_null) {
            if (offset != PACK_NULL_OFFSET) {
                snprintf(error, error_size, "translation pack null mismatch at %lu",
                    (unsigned long)i);
                return 0;
            }
            continue;
        }
        if (offset == PACK_NULL_OFFSET || offset >= blob_size) {
            snprintf(error, error_size, "translation pack offset mismatch at %lu",
                (unsigned long)i);
            return 0;
        }
        start = pack->data + blob_start + offset;
        terminator = (const uint8_t *)memchr(start, 0, blob_size - offset);
        if (terminator == NULL) {
            snprintf(error, error_size, "translation pack string is not NUL-terminated");
            return 0;
        }
        actual_size = (size_t)(terminator - start);
        if (actual_size != expected->size
            || (actual_size != 0 && memcmp(start, expected->data, actual_size) != 0)
            || !host_utf8_validate(start, actual_size)) {
            snprintf(error, error_size, "translation pack round-trip mismatch at %lu",
                (unsigned long)i);
            return 0;
        }
    }
    return 1;
}

static int is_graphic_codepoint(uint32_t codepoint)
{
    return codepoint >= 0xe000u && codepoint <= 0xe00eu && codepoint != 0xe00au;
}

static int append_codepoint(uint32_t **values, size_t *count, size_t *capacity,
    uint32_t codepoint)
{
    uint32_t *next;
    if (codepoint < 0x80u || (codepoint >= 0x00a0u && codepoint <= 0x00ffu)
        || is_graphic_codepoint(codepoint))
        return 1;
    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 128 : *capacity * 2;
        next = (uint32_t *)realloc(*values, new_capacity * sizeof(**values));
        if (next == NULL)
            return 0;
        *values = next;
        *capacity = new_capacity;
    }
    (*values)[(*count)++] = codepoint;
    return 1;
}

static int collect_utf8_codepoints(const uint8_t *data, size_t size,
    uint32_t **values, size_t *count, size_t *capacity,
    char *error, size_t error_size, const char *context)
{
    const uint8_t *cursor = data;
    const uint8_t *end = data + size;
    while (cursor < end) {
        uint32_t codepoint;
        if (!host_utf8_next(&cursor, end, &codepoint)) {
            snprintf(error, error_size, "%s: value is not valid UTF-8", context);
            return 0;
        }
        if (!append_codepoint(values, count, capacity, codepoint)) {
            snprintf(error, error_size, "out of memory collecting Unicode glyphs");
            return 0;
        }
    }
    return 1;
}

static int compare_u32(const void *left, const void *right)
{
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

static int lookup_gbk_glyph(uint32_t codepoint, uint16_t *glyph)
{
    size_t lo = 0;
    size_t hi = sizeof(gbk_unicode_map) / sizeof(gbk_unicode_map[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (gbk_unicode_map[mid].codepoint < codepoint)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo >= sizeof(gbk_unicode_map) / sizeof(gbk_unicode_map[0])
        || gbk_unicode_map[lo].codepoint != codepoint)
        return 0;
    *glyph = gbk_unicode_map[lo].glyph;
    return 1;
}

int host_translation_collect_unicode(
    const host_translation_catalog_t *catalogs, size_t catalog_count,
    const char *const *extra_sources, size_t extra_source_count,
    host_unicode_glyph_t **entries, size_t *entry_count,
    char *error, size_t error_size)
{
    uint32_t *required = NULL;
    size_t required_count = 0;
    size_t capacity = 0;
    size_t catalog_index;
    size_t i;
    size_t unique_count;
    host_unicode_glyph_t *result = NULL;

    *entries = NULL;
    *entry_count = 0;
    for (catalog_index = 0; catalog_index < catalog_count; ++catalog_index) {
        const host_translation_catalog_t *catalog = &catalogs[catalog_index];
        for (i = 0; i < catalog->count; ++i) {
            char context[64];
            if (catalog->values[i].is_null)
                continue;
            snprintf(context, sizeof(context), "catalog %lu message %lu",
                (unsigned long)catalog_index, (unsigned long)i);
            if (!collect_utf8_codepoints(catalog->values[i].data, catalog->values[i].size,
                    &required, &required_count, &capacity, error, error_size, context))
                goto fail;
        }
    }
    for (i = 0; i < extra_source_count; ++i) {
        host_buffer_t file;
        host_buffer_init(&file);
        if (!host_read_file(extra_sources[i], &file)) {
            snprintf(error, error_size, "missing Unicode source: %s", extra_sources[i]);
            host_buffer_free(&file);
            goto fail;
        }
        if (!collect_utf8_codepoints(file.data, file.size, &required, &required_count,
                &capacity, error, error_size, extra_sources[i])) {
            host_buffer_free(&file);
            goto fail;
        }
        host_buffer_free(&file);
    }
    if (required_count == 0) {
        free(required);
        return 1;
    }
    qsort(required, required_count, sizeof(*required), compare_u32);
    unique_count = 1;
    for (i = 1; i < required_count; ++i) {
        if (required[i] != required[unique_count - 1])
            required[unique_count++] = required[i];
    }
    result = (host_unicode_glyph_t *)calloc(unique_count, sizeof(*result));
    if (result == NULL) {
        snprintf(error, error_size, "out of memory collecting Unicode glyphs");
        goto fail;
    }
    for (i = 0; i < unique_count; ++i) {
        uint16_t glyph;
        if (!lookup_gbk_glyph(required[i], &glyph)) {
            snprintf(error, error_size,
                "translation font is missing Unicode glyph U+%04lX",
                (unsigned long)required[i]);
            goto fail;
        }
        result[i].codepoint = required[i];
        result[i].glyph = glyph;
    }
    free(required);
    *entries = result;
    *entry_count = unique_count;
    return 1;
fail:
    free(required);
    free(result);
    return 0;
}

int host_translation_render_unicode_source(const host_unicode_glyph_t *entries,
    size_t entry_count, host_buffer_t *output)
{
    size_t i;
    host_buffer_free(output);
    host_buffer_init(output);
    if (!host_buffer_append_string(output,
            "/* Generated by tools/build_translations.py; do not edit. */\n"
            "#include <stddef.h>\n"
            "#include <stdint.h>\n\n"
            "#include \"common/ui_unicode_glyph.h\"\n\n"
            "static const ui_unicode_glyph_entry_t ui_unicode_glyphs[] = {\n"))
        return 0;
    for (i = 0; i < entry_count; ++i) {
        if (!host_buffer_append_format(output, "\t{ 0x%04lxu, 0x%04xu },\n",
                (unsigned long)entries[i].codepoint, (unsigned)entries[i].glyph))
            return 0;
    }
    return host_buffer_append_string(output,
        "};\n\n"
        "int ui_unicode_glyph_lookup(uint32_t codepoint, uint16_t *glyph)\n"
        "{\n"
        "\tsize_t lo = 0;\n"
        "\tsize_t hi = sizeof(ui_unicode_glyphs) / sizeof(ui_unicode_glyphs[0]);\n\n"
        "\twhile (lo < hi) {\n"
        "\t\tsize_t mid = lo + (hi - lo) / 2;\n"
        "\t\tuint32_t candidate = ui_unicode_glyphs[mid].codepoint;\n"
        "\t\tif (candidate < codepoint)\n"
        "\t\t\tlo = mid + 1;\n"
        "\t\telse\n"
        "\t\t\thi = mid;\n"
        "\t}\n"
        "\tif (lo >= sizeof(ui_unicode_glyphs) / sizeof(ui_unicode_glyphs[0])\n"
        "\t\t|| ui_unicode_glyphs[lo].codepoint != codepoint)\n"
        "\t\treturn 0;\n"
        "\tif (glyph != NULL)\n"
        "\t\t*glyph = ui_unicode_glyphs[lo].glyph;\n"
        "\treturn 1;\n"
        "}\n");
}

static int append_c_string(host_buffer_t *output, const host_translation_value_t *value)
{
    size_t i;
    if (value->is_null)
        return host_buffer_append_string(output, "NULL");
    if (!host_buffer_append_byte(output, '"'))
        return 0;
    for (i = 0; i < value->size; ++i) {
        uint8_t byte = value->data[i];
        const char *escape = NULL;
        char octal[5];
        switch (byte) {
        case '\t': escape = "\\t"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        default: break;
        }
        if (escape != NULL) {
            if (!host_buffer_append_string(output, escape))
                return 0;
        } else if (byte >= 0x20 && byte <= 0x7e) {
            if (!host_buffer_append_byte(output, byte))
                return 0;
        } else {
            snprintf(octal, sizeof(octal), "\\%03o", (unsigned)byte);
            if (!host_buffer_append_string(output, octal))
                return 0;
        }
    }
    return host_buffer_append_byte(output, '"');
}

static int append_romcnv_catalog(host_buffer_t *output, const char *symbol,
    const host_translation_manifest_t *manifest,
    const host_translation_catalog_t *catalog)
{
    size_t i;
    if (!host_buffer_append_format(output,
            "static const char *const romcnv_catalog_%s[ROMCNV_TEXT_COUNT] = {\n", symbol))
        return 0;
    for (i = 0; i < manifest->count; ++i) {
        if (!host_buffer_append_byte(output, '\t')
            || !append_c_string(output, &catalog->values[i])
            || !host_buffer_append_format(output, ", /* %s */\n", manifest->names[i]))
            return 0;
    }
    return host_buffer_append_string(output, "};\n");
}

int host_romcnv_render_translation_include(
    const host_translation_manifest_t *manifest,
    const host_translation_catalog_t *english,
    const host_translation_catalog_t *zh_hans,
    host_buffer_t *output)
{
    host_buffer_free(output);
    host_buffer_init(output);
    return host_buffer_append_string(output,
            "/* Generated by tools/build_romcnv_translations.py; do not edit. */\n\n")
        && append_romcnv_catalog(output, "en", manifest, english)
        && host_buffer_append_byte(output, '\n')
        && append_romcnv_catalog(output, "zh_hans", manifest, zh_hans);
}
