#include "dip_metadata.h"

#include "crc32.h"
#include "endian.h"
#include "json.h"
#include "string_pool.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIP_HEADER_SIZE 40u
#define DIP_DIRECTORY_SIZE 28u
#define DIP_ROW_SIZE 16u
#define DIP_MAX_PROFILE_NAME 15u
#define DIP_MAX_ROWS 32u
#define DIP_MAX_VALUES 33u

static const char *const languages[] = {"en", "ja", "zh-Hans", "zh-Hant"};
static const char *const top_keys[] = {"version", "languages", "profiles"};
static const char *const row_keys[] = {"label", "enable", "mask", "value_max", "values"};

typedef struct dip_row_view {
    const host_json_string_t *label;
    uint8_t enable;
    uint8_t mask;
    uint8_t value_max;
    const host_json_array_t *values;
} dip_row_view_t;

static int json_u8(const host_json_value_t *value, uint8_t *result)
{
    if (value == NULL || value->type != HOST_JSON_NUMBER
        || value->as.number < 0 || value->as.number > 255)
        return 0;
    *result = (uint8_t)value->as.number;
    return 1;
}

static int string_equals(const host_json_string_t *string, const char *text)
{
    size_t length = strlen(text);
    return string->length == length && memcmp(string->data, text, length) == 0;
}

static int string_has_embedded_nul(const host_json_string_t *string)
{
    return memchr(string->data, 0, string->length) != NULL;
}

static int ascii_string(const host_json_string_t *string)
{
    size_t i;
    for (i = 0; i < string->length; ++i) {
        if ((unsigned char)string->data[i] >= 0x80 || string->data[i] == '\0')
            return 0;
    }
    return 1;
}

static int parse_row(const char *profile, const char *language, size_t index,
    const host_json_value_t *value, dip_row_view_t *row, char *error, size_t error_size)
{
    const host_json_value_t *label;
    const host_json_value_t *values;
    uint8_t enable;
    uint8_t mask;
    uint8_t value_max;
    size_t i;

    if (!host_json_object_has_exact_keys(value, row_keys, sizeof(row_keys) / sizeof(row_keys[0]))) {
        snprintf(error, error_size, "%s/%s/row %lu: fields must be label, enable, mask, value_max and values",
            profile, language, (unsigned long)index);
        return 0;
    }
    label = host_json_object_get(value, "label");
    values = host_json_object_get(value, "values");
    if (label == NULL || label->type != HOST_JSON_STRING) {
        snprintf(error, error_size, "%s/%s/row %lu: label must be a string",
            profile, language, (unsigned long)index);
        return 0;
    }
    if (values == NULL || values->type != HOST_JSON_ARRAY) {
        snprintf(error, error_size, "%s/%s/row %lu: values must be a string list",
            profile, language, (unsigned long)index);
        return 0;
    }
    if (values->as.array.count > DIP_MAX_VALUES) {
        snprintf(error, error_size, "%s/%s/row %lu: too many value labels",
            profile, language, (unsigned long)index);
        return 0;
    }
    for (i = 0; i < values->as.array.count; ++i) {
        if (values->as.array.items[i]->type != HOST_JSON_STRING) {
            snprintf(error, error_size, "%s/%s/row %lu: values must be a string list",
                profile, language, (unsigned long)index);
            return 0;
        }
    }
    if (!json_u8(host_json_object_get(value, "enable"), &enable) || enable > 1) {
        snprintf(error, error_size, "%s/%s/row %lu: enable must be 0 or 1",
            profile, language, (unsigned long)index);
        return 0;
    }
    if (!json_u8(host_json_object_get(value, "mask"), &mask)) {
        snprintf(error, error_size, "%s/%s/row %lu: mask is out of range",
            profile, language, (unsigned long)index);
        return 0;
    }
    if (!json_u8(host_json_object_get(value, "value_max"), &value_max)) {
        snprintf(error, error_size, "%s/%s/row %lu: value_max is out of range",
            profile, language, (unsigned long)index);
        return 0;
    }
    if (values->as.array.count != 0 && value_max != values->as.array.count - 1) {
        snprintf(error, error_size, "%s/%s/row %lu: value_max %u does not match %lu labels",
            profile, language, (unsigned long)index, value_max,
            (unsigned long)values->as.array.count);
        return 0;
    }
    if (values->as.array.count == 0 && value_max != 0) {
        snprintf(error, error_size, "%s/%s/row %lu: no labels for non-zero value_max",
            profile, language, (unsigned long)index);
        return 0;
    }
    row->label = &label->as.string;
    row->enable = enable;
    row->mask = mask;
    row->value_max = value_max;
    row->values = &values->as.array;
    return 1;
}

static int compare_members(const void *left, const void *right)
{
    const host_json_member_t *const *a = (const host_json_member_t *const *)left;
    const host_json_member_t *const *b = (const host_json_member_t *const *)right;
    size_t minimum = (*a)->key.length < (*b)->key.length ? (*a)->key.length : (*b)->key.length;
    int result = memcmp((*a)->key.data, (*b)->key.data, minimum);
    if (result != 0)
        return result;
    if ((*a)->key.length < (*b)->key.length) return -1;
    if ((*a)->key.length > (*b)->key.length) return 1;
    return 0;
}

static int add_json_string(host_string_pool_t *pool, const host_json_string_t *string,
    int menu_end, uint32_t *offset)
{
    if (menu_end) {
        *offset = 0;
        return 1;
    }
    if (string_has_embedded_nul(string))
        return 0;
    return host_string_pool_add(pool, string->data, offset);
}

int host_dip_metadata_build(const char *source_path, host_buffer_t *output,
    host_dip_metadata_stats_t *stats, char *error, size_t error_size)
{
    host_json_value_t *document = NULL;
    const host_json_value_t *version;
    const host_json_value_t *language_list;
    const host_json_value_t *profiles;
    const host_json_member_t **sorted_profiles = NULL;
    host_string_pool_t strings;
    host_buffer_t directories;
    host_buffer_t rows;
    host_buffer_t choices;
    host_buffer_t payload;
    size_t profile_index;
    uint32_t row_index = 0;
    uint32_t choice_index = 0;
    int strings_ready = 0;
    int ok = 0;

    host_buffer_free(output);
    stats->profiles = 0;
    stats->localized_rows = 0;
    host_buffer_init(&directories);
    host_buffer_init(&rows);
    host_buffer_init(&choices);
    host_buffer_init(&payload);

    document = host_json_parse_file(source_path, error, error_size);
    if (document == NULL)
        goto out;
    if (!host_json_object_has_exact_keys(document, top_keys, sizeof(top_keys) / sizeof(top_keys[0]))) {
        snprintf(error, error_size, "top-level fields must be version, languages and profiles");
        goto out;
    }
    version = host_json_object_get(document, "version");
    language_list = host_json_object_get(document, "languages");
    profiles = host_json_object_get(document, "profiles");
    if (version == NULL || version->type != HOST_JSON_NUMBER || version->as.number != 1) {
        snprintf(error, error_size, "source version must be 1");
        goto out;
    }
    if (language_list == NULL || language_list->type != HOST_JSON_ARRAY
        || language_list->as.array.count != sizeof(languages) / sizeof(languages[0])) {
        snprintf(error, error_size, "languages must be en, ja, zh-Hans, zh-Hant");
        goto out;
    }
    for (profile_index = 0; profile_index < language_list->as.array.count; ++profile_index) {
        const host_json_value_t *language = language_list->as.array.items[profile_index];
        if (language->type != HOST_JSON_STRING
            || !string_equals(&language->as.string, languages[profile_index])) {
            snprintf(error, error_size, "languages must be en, ja, zh-Hans, zh-Hant");
            goto out;
        }
    }
    if (profiles == NULL || profiles->type != HOST_JSON_OBJECT || profiles->as.object.count == 0) {
        snprintf(error, error_size, "profiles must be a non-empty object");
        goto out;
    }
    sorted_profiles = (const host_json_member_t **)malloc(
        profiles->as.object.count * sizeof(*sorted_profiles));
    if (sorted_profiles == NULL) {
        snprintf(error, error_size, "out of memory sorting DIP profiles");
        goto out;
    }
    for (profile_index = 0; profile_index < profiles->as.object.count; ++profile_index)
        sorted_profiles[profile_index] = &profiles->as.object.members[profile_index];
    qsort(sorted_profiles, profiles->as.object.count, sizeof(*sorted_profiles), compare_members);
    if (!host_string_pool_init(&strings)) {
        snprintf(error, error_size, "out of memory building DIP metadata");
        goto out;
    }
    strings_ready = 1;

    for (profile_index = 0; profile_index < profiles->as.object.count; ++profile_index) {
        const host_json_member_t *profile = sorted_profiles[profile_index];
        const host_json_value_t *localized = profile->value;
        dip_row_view_t *english_rows = NULL;
        size_t english_count = 0;
        size_t language_index;

        if (profile->key.length == 0 || profile->key.length > DIP_MAX_PROFILE_NAME
            || !ascii_string(&profile->key)) {
            snprintf(error, error_size, "invalid profile name");
            goto profile_fail;
        }
        if (localized->type != HOST_JSON_OBJECT
            || localized->as.object.count != sizeof(languages) / sizeof(languages[0])) {
            snprintf(error, error_size, "%s: must define exactly en, ja, zh-Hans, zh-Hant",
                profile->key.data);
            goto profile_fail;
        }
        for (language_index = 0; language_index < sizeof(languages) / sizeof(languages[0]); ++language_index) {
            const host_json_value_t *raw_rows = host_json_object_get(localized, languages[language_index]);
            dip_row_view_t *parsed_rows;
            size_t count;
            size_t row_number;
            uint32_t first_row = row_index;
            uint8_t name_field[16] = {0};

            if (raw_rows == NULL || raw_rows->type != HOST_JSON_ARRAY || raw_rows->as.array.count == 0) {
                snprintf(error, error_size, "%s/%s: rows must be non-empty",
                    profile->key.data, languages[language_index]);
                goto profile_fail;
            }
            if (raw_rows->as.array.count > DIP_MAX_ROWS) {
                snprintf(error, error_size, "%s/%s: too many DIP rows",
                    profile->key.data, languages[language_index]);
                goto profile_fail;
            }
            count = raw_rows->as.array.count;
            parsed_rows = (dip_row_view_t *)calloc(count, sizeof(*parsed_rows));
            if (parsed_rows == NULL) {
                snprintf(error, error_size, "out of memory validating DIP rows");
                goto profile_fail;
            }
            for (row_number = 0; row_number < count; ++row_number) {
                if (!parse_row(profile->key.data, languages[language_index], row_number,
                        raw_rows->as.array.items[row_number], &parsed_rows[row_number],
                        error, error_size)) {
                    free(parsed_rows);
                    goto profile_fail;
                }
            }
            if (!(parsed_rows[count - 1].label->length == 1
                    && parsed_rows[count - 1].label->data[0] == '\0')) {
                snprintf(error, error_size, "%s/%s: final row must be MENU_END",
                    profile->key.data, languages[language_index]);
                free(parsed_rows);
                goto profile_fail;
            }
            if (language_index == 0) {
                english_rows = parsed_rows;
                english_count = count;
            } else {
                if (count != english_count) {
                    snprintf(error, error_size, "%s/%s: row count differs from English",
                        profile->key.data, languages[language_index]);
                    free(parsed_rows);
                    goto profile_fail;
                }
                for (row_number = 0; row_number < count; ++row_number) {
                    if (parsed_rows[row_number].enable != english_rows[row_number].enable
                        || parsed_rows[row_number].mask != english_rows[row_number].mask
                        || parsed_rows[row_number].value_max != english_rows[row_number].value_max
                        || parsed_rows[row_number].values->count != english_rows[row_number].values->count) {
                        snprintf(error, error_size,
                            "%s/%s/row %lu: structural fields differ from English",
                            profile->key.data, languages[language_index], (unsigned long)row_number);
                        free(parsed_rows);
                        goto profile_fail;
                    }
                }
            }

            for (row_number = 0; row_number < count; ++row_number) {
                const dip_row_view_t *row = &parsed_rows[row_number];
                uint32_t first_choice = choice_index;
                uint32_t label_offset;
                size_t value_index;
                int menu_end = row->label->length == 1 && row->label->data[0] == '\0';

                for (value_index = 0; value_index < row->values->count; ++value_index) {
                    const host_json_string_t *value = &row->values->items[value_index]->as.string;
                    uint32_t value_offset;
                    if (!add_json_string(&strings, value, 0, &value_offset)) {
                        snprintf(error, error_size, "embedded NUL in DIP value label");
                        if (language_index != 0) free(parsed_rows);
                        goto profile_fail;
                    }
                    if (!host_append_le32(&choices, value_offset)) {
                        if (language_index != 0) free(parsed_rows);
                        goto oom_profile;
                    }
                    ++choice_index;
                }
                if (!add_json_string(&strings, row->label, menu_end, &label_offset)) {
                    snprintf(error, error_size, "embedded NUL is only allowed for MENU_END");
                    if (language_index != 0) free(parsed_rows);
                    goto profile_fail;
                }
                if (!host_append_le32(&rows, label_offset)
                    || !host_buffer_append_byte(&rows, row->enable)
                    || !host_buffer_append_byte(&rows, row->mask)
                    || !host_buffer_append_byte(&rows, row->value_max)
                    || !host_buffer_append_byte(&rows, (uint8_t)row->values->count)
                    || !host_append_le32(&rows, first_choice)
                    || !host_append_le32(&rows, 0)) {
                    if (language_index != 0) free(parsed_rows);
                    goto oom_profile;
                }
                ++row_index;
            }
            memcpy(name_field, profile->key.data, profile->key.length);
            if (!host_buffer_append(&directories, name_field, sizeof(name_field))
                || !host_buffer_append_byte(&directories, (uint8_t)language_index)
                || !host_buffer_append_byte(&directories, 0)
                || !host_append_le16(&directories, (uint16_t)count)
                || !host_append_le32(&directories, first_row)
                || !host_append_le32(&directories, 0)) {
                if (language_index != 0) free(parsed_rows);
                goto oom_profile;
            }
            stats->localized_rows += count;
            if (language_index != 0)
                free(parsed_rows);
        }
        free(english_rows);
        ++stats->profiles;
        continue;

oom_profile:
        snprintf(error, error_size, "out of memory building DIP metadata");
profile_fail:
        free(english_rows);
        goto out;
    }

    if (directories.size != stats->profiles * 4u * DIP_DIRECTORY_SIZE
        || rows.size != stats->localized_rows * DIP_ROW_SIZE) {
        snprintf(error, error_size, "internal DIP metadata record size mismatch");
        goto out;
    }
    if (!host_buffer_append(&payload, directories.data, directories.size)
        || !host_buffer_append(&payload, rows.data, rows.size)
        || !host_buffer_append(&payload, choices.data, choices.size)
        || !host_buffer_append(&payload, strings.data.data, strings.data.size)) {
        snprintf(error, error_size, "out of memory building DIP metadata");
        goto out;
    }
    {
        uint32_t directory_offset = DIP_HEADER_SIZE;
        uint32_t rows_offset = directory_offset + (uint32_t)directories.size;
        uint32_t choices_offset = rows_offset + (uint32_t)rows.size;
        uint32_t strings_offset = choices_offset + (uint32_t)choices.size;
        if (!host_buffer_append(output, "NJDP", 4)
            || !host_append_le16(output, 1)
            || !host_append_le16(output, 4)
            || !host_append_le32(output, (uint32_t)stats->profiles)
            || !host_append_le32(output, (uint32_t)(stats->profiles * 4u))
            || !host_append_le32(output, directory_offset)
            || !host_append_le32(output, rows_offset)
            || !host_append_le32(output, choices_offset)
            || !host_append_le32(output, strings_offset)
            || !host_append_le32(output, (uint32_t)strings.data.size)
            || !host_append_le32(output, host_crc32(payload.data, payload.size))
            || !host_buffer_append(output, payload.data, payload.size)) {
            snprintf(error, error_size, "out of memory building DIP metadata");
            goto out;
        }
    }
    ok = 1;
out:
    if (strings_ready)
        host_string_pool_free(&strings);
    host_buffer_free(&directories);
    host_buffer_free(&rows);
    host_buffer_free(&choices);
    host_buffer_free(&payload);
    free(sorted_profiles);
    host_json_free(document);
    if (!ok)
        host_buffer_free(output);
    return ok;
}
