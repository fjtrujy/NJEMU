#include "translation_source.h"

#include "file.h"
#include "text.h"
#include "utf8.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct manifest_entry {
    unsigned long id;
    char *name;
} manifest_entry_t;

void host_translation_manifest_init(host_translation_manifest_t *manifest)
{
    manifest->names = NULL;
    manifest->count = 0;
}

void host_translation_manifest_free(host_translation_manifest_t *manifest)
{
    size_t i;
    for (i = 0; i < manifest->count; ++i)
        free(manifest->names[i]);
    free(manifest->names);
    host_translation_manifest_init(manifest);
}

void host_translation_catalog_init(host_translation_catalog_t *catalog)
{
    catalog->values = NULL;
    catalog->count = 0;
    catalog->total_bytes = 0;
}

void host_translation_catalog_free(host_translation_catalog_t *catalog)
{
    size_t i;
    for (i = 0; i < catalog->count; ++i)
        free(catalog->values[i].data);
    free(catalog->values);
    host_translation_catalog_init(catalog);
}

static int compare_manifest_entry(const void *left, const void *right)
{
    const manifest_entry_t *a = (const manifest_entry_t *)left;
    const manifest_entry_t *b = (const manifest_entry_t *)right;
    if (a->id < b->id) return -1;
    if (a->id > b->id) return 1;
    return strcmp(a->name, b->name);
}

static int parse_manifest_line(char *line, const char *macro_name,
    char **name, unsigned long *id)
{
    char *cursor = line;
    char *end;
    size_t macro_length = strlen(macro_name);

    if (strncmp(cursor, macro_name, macro_length) != 0)
        return 0;
    cursor += macro_length;
    while (isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor++ != '(')
        return 0;
    while (isspace((unsigned char)*cursor)) ++cursor;
    end = cursor;
    if (!(isalpha((unsigned char)*end) || *end == '_'))
        return 0;
    while (isalnum((unsigned char)*end) || *end == '_') ++end;
    *name = host_strndup(cursor, (size_t)(end - cursor));
    if (*name == NULL)
        return 0;
    cursor = end;
    while (isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor++ != ',')
        goto fail;
    while (isspace((unsigned char)*cursor)) ++cursor;
    if (!isdigit((unsigned char)*cursor))
        goto fail;
    *id = strtoul(cursor, &end, 10);
    if (end == cursor)
        goto fail;
    cursor = end;
    while (isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor++ != ')')
        goto fail;
    while (isspace((unsigned char)*cursor)) ++cursor;
    if (*cursor != '\0')
        goto fail;
    return 1;
fail:
    free(*name);
    *name = NULL;
    return 0;
}

int host_translation_load_manifest(const char *path, const char *macro_name,
    host_translation_manifest_t *manifest, char *error, size_t error_size)
{
    host_buffer_t file;
    char *cursor;
    manifest_entry_t *entries = NULL;
    size_t count = 0;
    size_t line_number = 0;
    size_t i;
    int ok = 0;

    host_translation_manifest_free(manifest);
    host_buffer_init(&file);
    if (!host_read_file(path, &file)) {
        snprintf(error, error_size, "missing stable manifest: %s", path);
        return 0;
    }
    if (!host_utf8_validate(file.data, file.size)) {
        snprintf(error, error_size, "%s: manifest must be valid UTF-8", path);
        goto out;
    }
    cursor = (char *)file.data;
    while (*cursor != '\0') {
        char *line = cursor;
        char *end = strchr(cursor, '\n');
        char *trimmed;
        manifest_entry_t entry;
        manifest_entry_t *next;

        ++line_number;
        if (end != NULL) {
            *end = '\0';
            cursor = end + 1;
        } else {
            cursor += strlen(cursor);
        }
        end = line + strlen(line);
        if (end > line && end[-1] == '\r') end[-1] = '\0';
        trimmed = host_trim(line);
        if (*trimmed == '\0' || trimmed[0] == '#'
            || strncmp(trimmed, "/*", 2) == 0 || trimmed[0] == '*')
            continue;
        entry.name = NULL;
        if (!parse_manifest_line(trimmed, macro_name, &entry.name, &entry.id)) {
            snprintf(error, error_size, "%s:%lu: unsupported manifest line",
                path, (unsigned long)line_number);
            goto out;
        }
        next = (manifest_entry_t *)realloc(entries, (count + 1) * sizeof(*entries));
        if (next == NULL) {
            free(entry.name);
            snprintf(error, error_size, "out of memory parsing %s", path);
            goto out;
        }
        entries = next;
        entries[count++] = entry;
    }
    if (count == 0) {
        snprintf(error, error_size, "stable manifest is empty");
        goto out;
    }
    qsort(entries, count, sizeof(*entries), compare_manifest_entry);
    manifest->names = (char **)calloc(count, sizeof(*manifest->names));
    if (manifest->names == NULL) {
        snprintf(error, error_size, "out of memory parsing %s", path);
        goto out;
    }
    manifest->count = count;
    for (i = 0; i < count; ++i) {
        size_t previous;
        if (entries[i].id != i) {
            snprintf(error, error_size, "stable manifest IDs are not contiguous from zero");
            goto out;
        }
        for (previous = 0; previous < i; ++previous) {
            if (strcmp(manifest->names[previous], entries[i].name) == 0) {
                snprintf(error, error_size, "stable manifest contains duplicate names");
                goto out;
            }
        }
        manifest->names[i] = entries[i].name;
        entries[i].name = NULL;
    }
    ok = 1;
out:
    if (entries != NULL) {
        for (i = 0; i < count; ++i)
            free(entries[i].name);
    }
    free(entries);
    host_buffer_free(&file);
    if (!ok)
        host_translation_manifest_free(manifest);
    return ok;
}

static int graphic_token(const uint8_t *text, size_t length, uint32_t *codepoint)
{
    static const struct { const char *token; uint32_t codepoint; } tokens[] = {
        {"<UPARROW>", 0xe000}, {"<DOWNARROW>", 0xe001},
        {"<LEFTARROW>", 0xe002}, {"<RIGHTARROW>", 0xe003},
        {"<CIRCLE>", 0xe004}, {"<CROSS>", 0xe005},
        {"<SQUARE>", 0xe006}, {"<TRIANGLE>", 0xe007},
        {"<LTRIGGER>", 0xe008}, {"<RTRIGGER>", 0xe009},
        {"<UPTRIANGLE>", 0xe00b}, {"<DOWNTRIANGLE>", 0xe00c},
        {"<LEFTTRIANGLE>", 0xe00d}, {"<RIGHTTRIANGLE>", 0xe00e},
    };
    size_t i;
    for (i = 0; i < sizeof(tokens) / sizeof(tokens[0]); ++i) {
        size_t token_length = strlen(tokens[i].token);
        if (length == token_length && memcmp(text, tokens[i].token, length) == 0) {
            *codepoint = tokens[i].codepoint;
            return 1;
        }
    }
    return 0;
}

static int decode_value(const uint8_t *input, size_t size, host_translation_value_t *value,
    char *error, size_t error_size, const char *context)
{
    host_buffer_t output;
    size_t index = 0;

    memset(value, 0, sizeof(*value));
    if (size == 6 && memcmp(input, "<NULL>", 6) == 0) {
        value->is_null = 1;
        return 1;
    }
    host_buffer_init(&output);
    while (index < size) {
        uint8_t byte = input[index];
        if (byte == '<') {
            size_t end = index + 1;
            uint32_t codepoint;
            uint8_t encoded[4];
            size_t encoded_size;
            while (end < size && input[end] != '>') ++end;
            if (end >= size) {
                snprintf(error, error_size, "%s: unterminated graphic token", context);
                goto fail;
            }
            if (!graphic_token(input + index, end - index + 1, &codepoint)) {
                snprintf(error, error_size, "%s: unknown graphic token", context);
                goto fail;
            }
            if (!host_utf8_encode(codepoint, encoded, &encoded_size)
                || !host_buffer_append(&output, encoded, encoded_size))
                goto oom;
            index = end + 1;
            continue;
        }
        if (byte != '\\') {
            if (!host_buffer_append_byte(&output, byte)) goto oom;
            ++index;
            continue;
        }
        if (index + 1 >= size) {
            snprintf(error, error_size, "%s: trailing backslash", context);
            goto fail;
        }
        switch (input[index + 1]) {
        case 'n': byte = '\n'; break;
        case 'r': byte = '\r'; break;
        case 't': byte = '\t'; break;
        case '\\': byte = '\\'; break;
        case '=': byte = '='; break;
        case '#': byte = '#'; break;
        case '<': byte = '<'; break;
        case '>': byte = '>'; break;
        default:
            snprintf(error, error_size, "%s: unsupported escape \\%c", context,
                input[index + 1]);
            goto fail;
        }
        if (!host_buffer_append_byte(&output, byte)) goto oom;
        index += 2;
    }
    value->data = output.data;
    value->size = output.size;
    output.data = NULL;
    host_buffer_free(&output);
    return 1;
oom:
    snprintf(error, error_size, "out of memory decoding %s", context);
fail:
    host_buffer_free(&output);
    return 0;
}

int host_translation_load_catalog(const char *path,
    const host_translation_manifest_t *manifest, host_translation_catalog_t *catalog,
    char *error, size_t error_size)
{
    host_buffer_t file;
    char *cursor;
    size_t line_number = 0;
    size_t message_index = 0;
    int ok = 0;

    host_translation_catalog_free(catalog);
    catalog->values = (host_translation_value_t *)calloc(
        manifest->count, sizeof(*catalog->values));
    if (catalog->values == NULL && manifest->count != 0) {
        snprintf(error, error_size, "out of memory loading %s", path);
        return 0;
    }
    catalog->count = manifest->count;
    host_buffer_init(&file);
    if (!host_read_file(path, &file)) {
        snprintf(error, error_size, "missing translation source: %s", path);
        goto out;
    }
    if (!host_utf8_validate(file.data, file.size)) {
        snprintf(error, error_size, "%s: file must be valid UTF-8", path);
        goto out;
    }
    cursor = (char *)file.data;
    while (*cursor != '\0') {
        char *line = cursor;
        char *end = strchr(cursor, '\n');
        char *equals;
        char *lstrip;
        size_t key_length;
        char context[512];

        ++line_number;
        if (end != NULL) {
            *end = '\0';
            cursor = end + 1;
        } else {
            cursor += strlen(cursor);
        }
        end = line + strlen(line);
        if (end > line && end[-1] == '\r') end[-1] = '\0';
        if (*line == '\0')
            continue;
        lstrip = line;
        while (*lstrip == ' ' || *lstrip == '\t') ++lstrip;
        if (*lstrip == '#')
            continue;
        equals = strchr(line, '=');
        if (equals == NULL) {
            snprintf(error, error_size, "%s:%lu: expected KEY=value",
                path, (unsigned long)line_number);
            goto out;
        }
        key_length = (size_t)(equals - line);
        if (key_length == 0 || message_index >= manifest->count
            || strlen(manifest->names[message_index]) != key_length
            || memcmp(line, manifest->names[message_index], key_length) != 0) {
            snprintf(error, error_size,
                "%s:%lu: keys must follow manifest order and contain no unknown entries",
                path, (unsigned long)line_number);
            goto out;
        }
        snprintf(context, sizeof(context), "%s:%lu (%s)", path,
            (unsigned long)line_number, manifest->names[message_index]);
        if (!decode_value((const uint8_t *)equals + 1, strlen(equals + 1),
                &catalog->values[message_index], error, error_size, context))
            goto out;
        if (!catalog->values[message_index].is_null)
            catalog->total_bytes += catalog->values[message_index].size;
        ++message_index;
    }
    if (message_index != manifest->count) {
        snprintf(error, error_size, "%s: missing translation keys", path);
        goto out;
    }
    ok = 1;
out:
    host_buffer_free(&file);
    if (!ok)
        host_translation_catalog_free(catalog);
    return ok;
}

static int is_flag(uint8_t byte)
{
    return byte == '-' || byte == '+' || byte == ' ' || byte == '#'
        || byte == '0';
}

static int is_conversion(uint8_t byte)
{
    return strchr("diuoxXfFeEgGaAcspn%", byte) != NULL;
}

static int parse_conversion(const uint8_t *data, size_t size, size_t start, size_t *end)
{
    size_t p = start + 1;
    while (p < size && is_flag(data[p])) ++p;
    if (p < size && data[p] == '*') {
        ++p;
    } else {
        while (p < size && isdigit((unsigned char)data[p])) ++p;
    }
    if (p < size && data[p] == '.') {
        ++p;
        if (p < size && data[p] == '*') {
            ++p;
        } else {
            size_t before = p;
            while (p < size && isdigit((unsigned char)data[p])) ++p;
            if (p == before)
                return 0;
        }
    }
    if (p + 1 < size && ((data[p] == 'h' && data[p + 1] == 'h')
            || (data[p] == 'l' && data[p + 1] == 'l'))) {
        p += 2;
    } else if (p < size && strchr("hljztL", data[p]) != NULL) {
        ++p;
    }
    if (p >= size || !is_conversion(data[p]))
        return 0;
    *end = p + 1;
    return 1;
}

static int build_contract(const host_translation_value_t *value, host_buffer_t *contract,
    char *error, size_t error_size, const char *context)
{
    size_t index = 0;
    host_buffer_init(contract);
    if (value->is_null) {
        if (!host_buffer_append_byte(contract, 0xff))
            return 0;
        return 1;
    }
    while (index < value->size) {
        size_t percent = index;
        size_t end;
        while (percent < value->size && value->data[percent] != '%') ++percent;
        if (percent == value->size)
            break;
        if (parse_conversion(value->data, value->size, percent, &end)) {
            if (!host_buffer_append(contract, value->data + percent, end - percent)
                || !host_buffer_append_byte(contract, 0))
                return 0;
            index = end;
        } else {
            int alpha = percent + 1 < value->size
                && ((value->data[percent + 1] >= 'A' && value->data[percent + 1] <= 'Z')
                    || (value->data[percent + 1] >= 'a' && value->data[percent + 1] <= 'z'));
            if (alpha) {
                snprintf(error, error_size, "%s: unsupported printf conversion", context);
                host_buffer_free(contract);
                return 0;
            }
            if (!host_buffer_append_string(contract, "%literal")
                || !host_buffer_append_byte(contract, 0))
                return 0;
            index = percent + 1;
        }
    }
    return 1;
}

int host_translation_validate_printf(const host_translation_manifest_t *manifest,
    const host_translation_catalog_t *english, const host_translation_catalog_t *candidate,
    const char *candidate_language, char *error, size_t error_size)
{
    size_t i;
    for (i = 0; i < manifest->count; ++i) {
        host_buffer_t expected;
        host_buffer_t actual;
        char context[256];
        int same;
        snprintf(context, sizeof(context), "en:%s", manifest->names[i]);
        if (!build_contract(&english->values[i], &expected, error, error_size, context))
            return 0;
        snprintf(context, sizeof(context), "%s:%s", candidate_language, manifest->names[i]);
        if (!build_contract(&candidate->values[i], &actual, error, error_size, context)) {
            host_buffer_free(&expected);
            return 0;
        }
        same = expected.size == actual.size
            && (expected.size == 0 || memcmp(expected.data, actual.data, expected.size) == 0);
        host_buffer_free(&actual);
        host_buffer_free(&expected);
        if (!same) {
            snprintf(error, error_size, "%s:%s: printf contract does not match English",
                candidate_language, manifest->names[i]);
            return 0;
        }
    }
    return 1;
}
