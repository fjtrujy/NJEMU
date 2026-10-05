#include "json.h"

#include "file.h"
#include "utf8.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct json_parser {
    const uint8_t *cursor;
    const uint8_t *end;
    const char *path;
} json_parser_t;

static void skip_ws(json_parser_t *parser)
{
    while (parser->cursor < parser->end
        && (*parser->cursor == ' ' || *parser->cursor == '\t'
            || *parser->cursor == '\r' || *parser->cursor == '\n'))
        ++parser->cursor;
}

static host_json_value_t *new_value(host_json_type_t type)
{
    host_json_value_t *value = (host_json_value_t *)calloc(1, sizeof(*value));
    if (value != NULL)
        value->type = type;
    return value;
}

void host_json_free(host_json_value_t *value)
{
    size_t i;
    if (value == NULL)
        return;
    if (value->type == HOST_JSON_STRING) {
        free(value->as.string.data);
    } else if (value->type == HOST_JSON_ARRAY) {
        for (i = 0; i < value->as.array.count; ++i)
            host_json_free(value->as.array.items[i]);
        free(value->as.array.items);
    } else if (value->type == HOST_JSON_OBJECT) {
        for (i = 0; i < value->as.object.count; ++i) {
            free(value->as.object.members[i].key.data);
            host_json_free(value->as.object.members[i].value);
        }
        free(value->as.object.members);
    }
    free(value);
}

static int hex_digit(uint8_t value)
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

static int parse_hex4(json_parser_t *parser, uint32_t *value)
{
    unsigned i;
    uint32_t result = 0;
    if ((size_t)(parser->end - parser->cursor) < 4)
        return 0;
    for (i = 0; i < 4; ++i) {
        int digit = hex_digit(parser->cursor[i]);
        if (digit < 0)
            return 0;
        result = (result << 4) | (uint32_t)digit;
    }
    parser->cursor += 4;
    *value = result;
    return 1;
}

static int parse_string_data(json_parser_t *parser, host_json_string_t *string)
{
    host_buffer_t buffer;
    int ok = 0;

    string->data = NULL;
    string->length = 0;
    if (parser->cursor >= parser->end || *parser->cursor != '"')
        return 0;
    ++parser->cursor;
    host_buffer_init(&buffer);
    while (parser->cursor < parser->end) {
        uint8_t value = *parser->cursor++;
        if (value == '"') {
            if (!host_buffer_append_byte(&buffer, 0))
                goto out;
            string->data = (char *)buffer.data;
            string->length = buffer.size - 1;
            buffer.data = NULL;
            buffer.size = 0;
            buffer.capacity = 0;
            ok = 1;
            goto out;
        }
        if (value < 0x20)
            goto out;
        if (value != '\\') {
            if (!host_buffer_append_byte(&buffer, value))
                goto out;
            continue;
        }
        if (parser->cursor >= parser->end)
            goto out;
        value = *parser->cursor++;
        if (value == '"' || value == '\\' || value == '/') {
            if (!host_buffer_append_byte(&buffer, value)) goto out;
        } else if (value == 'b') {
            if (!host_buffer_append_byte(&buffer, '\b')) goto out;
        } else if (value == 'f') {
            if (!host_buffer_append_byte(&buffer, '\f')) goto out;
        } else if (value == 'n') {
            if (!host_buffer_append_byte(&buffer, '\n')) goto out;
        } else if (value == 'r') {
            if (!host_buffer_append_byte(&buffer, '\r')) goto out;
        } else if (value == 't') {
            if (!host_buffer_append_byte(&buffer, '\t')) goto out;
        } else if (value == 'u') {
            uint32_t codepoint;
            uint8_t bytes[4];
            size_t byte_count;
            if (!parse_hex4(parser, &codepoint))
                goto out;
            if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                uint32_t low;
                if ((size_t)(parser->end - parser->cursor) < 6
                    || parser->cursor[0] != '\\' || parser->cursor[1] != 'u')
                    goto out;
                parser->cursor += 2;
                if (!parse_hex4(parser, &low) || low < 0xdc00 || low > 0xdfff)
                    goto out;
                codepoint = 0x10000u + ((codepoint - 0xd800u) << 10) + (low - 0xdc00u);
            } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                goto out;
            }
            if (!host_utf8_encode(codepoint, bytes, &byte_count)
                || !host_buffer_append(&buffer, bytes, byte_count))
                goto out;
        } else {
            goto out;
        }
    }
out:
    if (!ok)
        host_buffer_free(&buffer);
    return ok;
}

static host_json_value_t *parse_value(json_parser_t *parser);

static host_json_value_t *parse_array(json_parser_t *parser)
{
    host_json_value_t *value = new_value(HOST_JSON_ARRAY);
    if (value == NULL)
        return NULL;
    ++parser->cursor;
    skip_ws(parser);
    if (parser->cursor < parser->end && *parser->cursor == ']') {
        ++parser->cursor;
        return value;
    }
    for (;;) {
        host_json_value_t *item;
        host_json_value_t **items;
        skip_ws(parser);
        item = parse_value(parser);
        if (item == NULL)
            goto fail;
        items = (host_json_value_t **)realloc(value->as.array.items,
            (value->as.array.count + 1) * sizeof(*items));
        if (items == NULL) {
            host_json_free(item);
            goto fail;
        }
        value->as.array.items = items;
        value->as.array.items[value->as.array.count++] = item;
        skip_ws(parser);
        if (parser->cursor >= parser->end)
            goto fail;
        if (*parser->cursor == ']') {
            ++parser->cursor;
            return value;
        }
        if (*parser->cursor != ',')
            goto fail;
        ++parser->cursor;
    }
fail:
    host_json_free(value);
    return NULL;
}

static host_json_value_t *parse_object(json_parser_t *parser)
{
    host_json_value_t *value = new_value(HOST_JSON_OBJECT);
    if (value == NULL)
        return NULL;
    ++parser->cursor;
    skip_ws(parser);
    if (parser->cursor < parser->end && *parser->cursor == '}') {
        ++parser->cursor;
        return value;
    }
    for (;;) {
        host_json_string_t key;
        host_json_value_t *member_value;
        host_json_member_t *members;
        size_t i;
        skip_ws(parser);
        if (!parse_string_data(parser, &key))
            goto fail;
        for (i = 0; i < value->as.object.count; ++i) {
            const host_json_string_t *existing = &value->as.object.members[i].key;
            if (existing->length == key.length
                && memcmp(existing->data, key.data, key.length) == 0) {
                free(key.data);
                goto fail;
            }
        }
        skip_ws(parser);
        if (parser->cursor >= parser->end || *parser->cursor != ':') {
            free(key.data);
            goto fail;
        }
        ++parser->cursor;
        skip_ws(parser);
        member_value = parse_value(parser);
        if (member_value == NULL) {
            free(key.data);
            goto fail;
        }
        members = (host_json_member_t *)realloc(value->as.object.members,
            (value->as.object.count + 1) * sizeof(*members));
        if (members == NULL) {
            free(key.data);
            host_json_free(member_value);
            goto fail;
        }
        value->as.object.members = members;
        value->as.object.members[value->as.object.count].key = key;
        value->as.object.members[value->as.object.count].value = member_value;
        ++value->as.object.count;
        skip_ws(parser);
        if (parser->cursor >= parser->end)
            goto fail;
        if (*parser->cursor == '}') {
            ++parser->cursor;
            return value;
        }
        if (*parser->cursor != ',')
            goto fail;
        ++parser->cursor;
    }
fail:
    host_json_free(value);
    return NULL;
}

static int match_literal(json_parser_t *parser, const char *literal)
{
    size_t length = strlen(literal);
    if ((size_t)(parser->end - parser->cursor) < length
        || memcmp(parser->cursor, literal, length) != 0)
        return 0;
    parser->cursor += length;
    return 1;
}

static host_json_value_t *parse_number(json_parser_t *parser)
{
    const uint8_t *start = parser->cursor;
    char text[64];
    size_t length;
    char *end;
    long long number;
    host_json_value_t *value;

    if (*parser->cursor == '-')
        ++parser->cursor;
    if (parser->cursor >= parser->end)
        return NULL;
    if (*parser->cursor == '0') {
        ++parser->cursor;
    } else if (*parser->cursor >= '1' && *parser->cursor <= '9') {
        while (parser->cursor < parser->end && isdigit((unsigned char)*parser->cursor))
            ++parser->cursor;
    } else {
        return NULL;
    }
    if (parser->cursor < parser->end
        && (*parser->cursor == '.' || *parser->cursor == 'e' || *parser->cursor == 'E'))
        return NULL;
    length = (size_t)(parser->cursor - start);
    if (length == 0 || length >= sizeof(text))
        return NULL;
    memcpy(text, start, length);
    text[length] = '\0';
    errno = 0;
    number = strtoll(text, &end, 10);
    if (errno != 0 || *end != '\0')
        return NULL;
    value = new_value(HOST_JSON_NUMBER);
    if (value != NULL)
        value->as.number = (int64_t)number;
    return value;
}

static host_json_value_t *parse_value(json_parser_t *parser)
{
    host_json_value_t *value;
    skip_ws(parser);
    if (parser->cursor >= parser->end)
        return NULL;
    if (*parser->cursor == '{')
        return parse_object(parser);
    if (*parser->cursor == '[')
        return parse_array(parser);
    if (*parser->cursor == '"') {
        value = new_value(HOST_JSON_STRING);
        if (value == NULL)
            return NULL;
        if (!parse_string_data(parser, &value->as.string)) {
            host_json_free(value);
            return NULL;
        }
        return value;
    }
    if (*parser->cursor == '-' || isdigit((unsigned char)*parser->cursor))
        return parse_number(parser);
    if (match_literal(parser, "true")) {
        value = new_value(HOST_JSON_BOOL);
        if (value != NULL) value->as.boolean = 1;
        return value;
    }
    if (match_literal(parser, "false")) {
        value = new_value(HOST_JSON_BOOL);
        if (value != NULL) value->as.boolean = 0;
        return value;
    }
    if (match_literal(parser, "null"))
        return new_value(HOST_JSON_NULL);
    return NULL;
}

host_json_value_t *host_json_parse_file(const char *path, char *error, size_t error_size)
{
    host_buffer_t file;
    json_parser_t parser;
    host_json_value_t *value;

    host_buffer_init(&file);
    if (!host_read_file(path, &file)) {
        snprintf(error, error_size, "could not read %s", path);
        return NULL;
    }
    if (!host_utf8_validate(file.data, file.size)) {
        snprintf(error, error_size, "%s: source must be UTF-8", path);
        host_buffer_free(&file);
        return NULL;
    }
    parser.cursor = file.data;
    parser.end = file.data + file.size;
    parser.path = path;
    value = parse_value(&parser);
    if (value == NULL) {
        snprintf(error, error_size, "%s: invalid JSON", path);
        host_buffer_free(&file);
        return NULL;
    }
    skip_ws(&parser);
    if (parser.cursor != parser.end) {
        snprintf(error, error_size, "%s: trailing data after JSON document", path);
        host_json_free(value);
        host_buffer_free(&file);
        return NULL;
    }
    host_buffer_free(&file);
    return value;
}

const host_json_value_t *host_json_object_get(const host_json_value_t *object, const char *key)
{
    size_t key_length = strlen(key);
    size_t i;
    if (object == NULL || object->type != HOST_JSON_OBJECT)
        return NULL;
    for (i = 0; i < object->as.object.count; ++i) {
        const host_json_string_t *candidate = &object->as.object.members[i].key;
        if (candidate->length == key_length && memcmp(candidate->data, key, key_length) == 0)
            return object->as.object.members[i].value;
    }
    return NULL;
}

int host_json_object_has_exact_keys(const host_json_value_t *object,
    const char *const *keys, size_t count)
{
    size_t i;
    if (object == NULL || object->type != HOST_JSON_OBJECT || object->as.object.count != count)
        return 0;
    for (i = 0; i < count; ++i) {
        if (host_json_object_get(object, keys[i]) == NULL)
            return 0;
    }
    return 1;
}
