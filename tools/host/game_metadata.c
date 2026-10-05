#include "game_metadata.h"

#include "crc32.h"
#include "endian.h"
#include "string_pool.h"
#include "text.h"
#include "tsv.h"
#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAME_METADATA_HEADER_SIZE 32u
#define GAME_METADATA_RECORD_SIZE 52u
#define GAME_METADATA_NAME_BYTES 16u
#define GAME_METADATA_TITLE_BYTES 128u

#define DISPLAY_NOT_WORK 0x01u
#define DISPLAY_BOOTLEG  0x02u
#define DISPLAY_HACK     0x04u

#define CPS2_PHOENIX               0x01u
#define CPS2_CACHE_PARENT_OVERRIDE 0x02u
#define CPS2_CACHE_INDEPENDENT     0x04u

#define MVS_OWNS_CROM 0x01u
#define MVS_OWNS_SROM 0x02u
#define MVS_OWNS_VROM 0x04u

static const char *const metadata_fields[] = {
    "name", "title_en", "title_ja", "title_zh_hans", "title_zh_hant",
    "display_flags", "core_flags", "aux_name", "data0", "data1", "data2"
};

void host_game_metadata_init(host_game_metadata_t *metadata)
{
    metadata->core = HOST_CORE_CPS1;
    metadata->records = NULL;
    metadata->count = 0;
}

void host_game_metadata_free(host_game_metadata_t *metadata)
{
    size_t i;
    for (i = 0; i < metadata->count; ++i) {
        size_t title;
        free(metadata->records[i].name);
        for (title = 0; title < 4; ++title)
            free(metadata->records[i].titles[title]);
        free(metadata->records[i].aux_name);
    }
    free(metadata->records);
    host_game_metadata_init(metadata);
}

int host_game_core_from_name(const char *name, host_game_core_t *core)
{
    if (strcmp(name, "cps1") == 0)
        *core = HOST_CORE_CPS1;
    else if (strcmp(name, "cps2") == 0)
        *core = HOST_CORE_CPS2;
    else if (strcmp(name, "mvs") == 0)
        *core = HOST_CORE_MVS;
    else if (strcmp(name, "ncdz") == 0)
        *core = HOST_CORE_NCDZ;
    else
        return 0;
    return 1;
}

const char *host_game_core_name(host_game_core_t core)
{
    switch (core) {
    case HOST_CORE_CPS1: return "cps1";
    case HOST_CORE_CPS2: return "cps2";
    case HOST_CORE_MVS: return "mvs";
    case HOST_CORE_NCDZ: return "ncdz";
    default: return "unknown";
    }
}

const host_metadata_record_t *host_game_metadata_find(
    const host_game_metadata_t *metadata, const char *name)
{
    size_t i;
    for (i = 0; i < metadata->count; ++i) {
        if (strcmp(metadata->records[i].name, name) == 0)
            return &metadata->records[i];
    }
    return NULL;
}

static int append_record(host_game_metadata_t *metadata, host_metadata_record_t **record)
{
    host_metadata_record_t *records = (host_metadata_record_t *)realloc(
        metadata->records, (metadata->count + 1) * sizeof(*metadata->records));
    if (records == NULL)
        return 0;
    metadata->records = records;
    *record = &metadata->records[metadata->count++];
    memset(*record, 0, sizeof(**record));
    return 1;
}

static int parse_flag_tokens(const char *raw, host_game_core_t core, int display,
    uint8_t *result, const char *name, char *error, size_t error_size)
{
    char *copy = host_strdup(raw);
    char *cursor;
    uint8_t value = 0;

    if (copy == NULL) {
        snprintf(error, error_size, "out of memory parsing flags");
        return 0;
    }
    cursor = copy;
    while (*cursor != '\0') {
        char *separator = strchr(cursor, '|');
        char *token;
        uint8_t flag = 0;
        if (separator != NULL)
            *separator = '\0';
        token = host_trim(cursor);
        if (display) {
            if (strcmp(token, "not_work") == 0) flag = DISPLAY_NOT_WORK;
            else if (strcmp(token, "bootleg") == 0) flag = DISPLAY_BOOTLEG;
            else if (strcmp(token, "hack") == 0) flag = DISPLAY_HACK;
        } else if (core == HOST_CORE_CPS2) {
            if (strcmp(token, "phoenix") == 0) flag = CPS2_PHOENIX;
            else if (strcmp(token, "cache_parent_override") == 0) flag = CPS2_CACHE_PARENT_OVERRIDE;
            else if (strcmp(token, "cache_independent") == 0) flag = CPS2_CACHE_INDEPENDENT;
        } else if (core == HOST_CORE_MVS) {
            if (strcmp(token, "owns_crom") == 0) flag = MVS_OWNS_CROM;
            else if (strcmp(token, "owns_srom") == 0) flag = MVS_OWNS_SROM;
            else if (strcmp(token, "owns_vrom") == 0) flag = MVS_OWNS_VROM;
        }
        if (flag == 0) {
            snprintf(error, error_size, "%s: unknown %s token '%s'", name,
                display ? "display_flags" : "core_flags", token);
            free(copy);
            return 0;
        }
        value |= flag;
        if (separator == NULL)
            break;
        cursor = separator + 1;
    }
    free(copy);
    *result = value;
    return 1;
}

static int copy_trimmed(const char *source, char **target)
{
    char *copy = host_strdup(source);
    char *trimmed;
    char *final;
    if (copy == NULL)
        return 0;
    trimmed = host_trim(copy);
    final = host_strdup(trimmed);
    free(copy);
    if (final == NULL)
        return 0;
    *target = final;
    return 1;
}

int host_game_metadata_load(const char *path, host_game_core_t core,
    host_game_metadata_t *metadata, char *error, size_t error_size)
{
    host_tsv_t table;
    size_t i;
    int ok = 0;

    host_game_metadata_free(metadata);
    metadata->core = core;
    host_tsv_init(&table);
    if (!host_tsv_load(path, &table, error, error_size))
        goto out;
    if (!host_tsv_header_equals(&table, metadata_fields,
            sizeof(metadata_fields) / sizeof(metadata_fields[0]))) {
        snprintf(error, error_size, "%s: unexpected TSV columns", path);
        goto out;
    }

    for (i = 0; i < table.row_count; ++i) {
        host_tsv_row_t *row = &table.rows[i];
        host_metadata_record_t *record;
        size_t title;
        char *raw_display;
        char *raw_core;
        char *raw_data[3];

        if (row->count > sizeof(metadata_fields) / sizeof(metadata_fields[0])) {
            snprintf(error, error_size, "%s:%lu: too many TSV fields", path,
                (unsigned long)row->line_number);
            goto out;
        }
        if (!append_record(metadata, &record)) {
            snprintf(error, error_size, "out of memory parsing %s", path);
            goto out;
        }
        if (!copy_trimmed(row->fields[0], &record->name)) {
            snprintf(error, error_size, "out of memory parsing %s", path);
            goto out;
        }
        if (*record->name == '\0') {
            snprintf(error, error_size, "%s:%lu: empty game name", path,
                (unsigned long)row->line_number);
            goto out;
        }
        if (!host_ascii_game_name(record->name) || strlen(record->name) >= GAME_METADATA_NAME_BYTES) {
            snprintf(error, error_size, "%s:%lu: invalid canonical game name '%s'", path,
                (unsigned long)row->line_number, record->name);
            goto out;
        }
        if (host_game_metadata_find(metadata, record->name) != record) {
            snprintf(error, error_size, "%s:%lu: duplicate game name %s", path,
                (unsigned long)row->line_number, record->name);
            goto out;
        }

        for (title = 0; title < 4; ++title) {
            if (!copy_trimmed(row->count > title + 1 ? row->fields[title + 1] : "",
                    &record->titles[title])) {
                snprintf(error, error_size, "out of memory parsing %s", path);
                goto out;
            }
            if (strlen(record->titles[title]) >= GAME_METADATA_TITLE_BYTES) {
                snprintf(error, error_size, "%s:%lu: %s title does not fit the 128-byte browser title buffer",
                    path, (unsigned long)row->line_number, record->name);
                goto out;
            }
            if (!host_utf8_validate((const uint8_t *)record->titles[title], strlen(record->titles[title]))) {
                snprintf(error, error_size, "%s:%lu: title must be valid UTF-8", path,
                    (unsigned long)row->line_number);
                goto out;
            }
        }
        if (core != HOST_CORE_NCDZ && *record->titles[0] == '\0') {
            snprintf(error, error_size, "%s:%lu: %s has no English display title", path,
                (unsigned long)row->line_number, record->name);
            goto out;
        }

        raw_display = row->count > 5 ? host_trim(row->fields[5]) : NULL;
        raw_core = row->count > 6 ? host_trim(row->fields[6]) : NULL;
        if (raw_display != NULL && *raw_display != '\0'
            && !parse_flag_tokens(raw_display, core, 1, &record->display_flags,
                record->name, error, error_size))
            goto out;
        if (raw_core != NULL && *raw_core != '\0'
            && !parse_flag_tokens(raw_core, core, 0, &record->core_flags,
                record->name, error, error_size))
            goto out;
        if (!copy_trimmed(row->count > 7 ? row->fields[7] : "", &record->aux_name)) {
            snprintf(error, error_size, "out of memory parsing %s", path);
            goto out;
        }
        if (*record->aux_name != '\0'
            && (!host_ascii_game_name(record->aux_name)
                || strlen(record->aux_name) >= GAME_METADATA_NAME_BYTES)) {
            snprintf(error, error_size, "%s:%lu: invalid auxiliary game name '%s'", path,
                (unsigned long)row->line_number, record->aux_name);
            goto out;
        }

        raw_data[0] = row->count > 8 ? host_trim(row->fields[8]) : NULL;
        raw_data[1] = row->count > 9 ? host_trim(row->fields[9]) : NULL;
        raw_data[2] = row->count > 10 ? host_trim(row->fields[10]) : NULL;
        for (title = 0; title < 3; ++title) {
            if (raw_data[title] != NULL && *raw_data[title] != '\0'
                && !host_parse_u32(raw_data[title], &record->data[title])) {
                snprintf(error, error_size, "%s: invalid data%lu value '%s'", record->name,
                    (unsigned long)title, raw_data[title]);
                goto out;
            }
        }
    }

    ok = 1;
out:
    host_tsv_free(&table);
    if (!ok)
        host_game_metadata_free(metadata);
    return ok;
}

static int validate_identity(const host_game_metadata_t *metadata, const host_rominfo_t *rominfo,
    char *error, size_t error_size)
{
    size_t i;
    if (rominfo == NULL) {
        snprintf(error, error_size, "%s: rominfo is required", host_game_core_name(metadata->core));
        return 0;
    }
    for (i = 0; i < rominfo->game_count; ++i) {
        if (host_game_metadata_find(metadata, rominfo->games[i].name) == NULL) {
            snprintf(error, error_size, "%s: identity divergence: missing from metadata: %s",
                host_game_core_name(metadata->core), rominfo->games[i].name);
            return 0;
        }
    }
    for (i = 0; i < metadata->count; ++i) {
        if (host_rominfo_find(rominfo, metadata->records[i].name) == NULL) {
            snprintf(error, error_size, "%s: identity divergence: not present in rominfo: %s",
                host_game_core_name(metadata->core), metadata->records[i].name);
            return 0;
        }
    }
    return 1;
}

int host_game_metadata_validate(const host_game_metadata_t *metadata,
    const host_rominfo_t *rominfo, char *error, size_t error_size)
{
    size_t i;

    if (metadata->core != HOST_CORE_NCDZ && !validate_identity(metadata, rominfo, error, error_size))
        return 0;
    for (i = 0; i < metadata->count; ++i) {
        const host_metadata_record_t *record = &metadata->records[i];
        if (metadata->core == HOST_CORE_CPS1) {
            if (record->core_flags != 0 || *record->aux_name != '\0'
                || record->data[0] != 0 || record->data[1] != 0 || record->data[2] != 0) {
                snprintf(error, error_size, "%s: CPS1 source has unexpected core-specific metadata",
                    record->name);
                return 0;
            }
        } else if (metadata->core == HOST_CORE_CPS2) {
            int has_key = record->data[0] != 0 || record->data[1] != 0 || record->data[2] != 0;
            int phoenix = (record->core_flags & CPS2_PHOENIX) != 0;
            if (has_key == phoenix) {
                snprintf(error, error_size,
                    "%s: CPS2 record must have exactly one of decryption key or phoenix flag",
                    record->name);
                return 0;
            }
            if ((record->core_flags & CPS2_CACHE_PARENT_OVERRIDE) != 0) {
                if (*record->aux_name == '\0' || host_game_metadata_find(metadata, record->aux_name) == NULL) {
                    snprintf(error, error_size, "%s: CPS2 cache parent override is missing or invalid",
                        record->name);
                    return 0;
                }
            } else if (*record->aux_name != '\0') {
                snprintf(error, error_size, "%s: CPS2 aux_name requires cache_parent_override", record->name);
                return 0;
            }
            if ((record->core_flags & CPS2_CACHE_INDEPENDENT) != 0
                && (record->core_flags & CPS2_CACHE_PARENT_OVERRIDE) != 0) {
                snprintf(error, error_size,
                    "%s: CPS2 cache metadata cannot be both independent and overridden", record->name);
                return 0;
            }
        } else if (metadata->core == HOST_CORE_MVS) {
            if (*record->aux_name != '\0' || record->data[0] != 0
                || record->data[1] != 0 || record->data[2] != 0) {
                snprintf(error, error_size, "%s: MVS source has unexpected numeric/auxiliary metadata",
                    record->name);
                return 0;
            }
        } else if (metadata->core == HOST_CORE_NCDZ) {
            size_t previous;
            if (record->display_flags != 0 || record->core_flags != 0 || *record->aux_name != '\0'
                || record->data[1] != 0 || record->data[2] != 0) {
                snprintf(error, error_size, "%s: NCDZ source has unexpected metadata fields", record->name);
                return 0;
            }
            if (record->data[0] == 0 || record->data[0] > 0xffffu) {
                snprintf(error, error_size, "%s: NCDZ NGH must be a non-zero uint16", record->name);
                return 0;
            }
            for (previous = 0; previous < i; ++previous) {
                if (metadata->records[previous].data[0] == record->data[0]) {
                    snprintf(error, error_size, "%s: NCDZ NGH 0x%04x duplicates %s", record->name,
                        record->data[0], metadata->records[previous].name);
                    return 0;
                }
            }
        }
    }
    return 1;
}

static int compare_record_ptrs(const void *left, const void *right)
{
    const host_metadata_record_t *const *a = (const host_metadata_record_t *const *)left;
    const host_metadata_record_t *const *b = (const host_metadata_record_t *const *)right;
    return strcmp((*a)->name, (*b)->name);
}

int host_game_metadata_sorted(const host_game_metadata_t *metadata,
    const host_metadata_record_t ***records)
{
    const host_metadata_record_t **sorted;
    size_t i;
    sorted = (const host_metadata_record_t **)malloc(metadata->count * sizeof(*sorted));
    if (sorted == NULL && metadata->count != 0)
        return 0;
    for (i = 0; i < metadata->count; ++i)
        sorted[i] = &metadata->records[i];
    qsort(sorted, metadata->count, sizeof(*sorted), compare_record_ptrs);
    *records = sorted;
    return 1;
}

int host_game_metadata_build_blob(const host_game_metadata_t *metadata,
    host_buffer_t *output, char *error, size_t error_size)
{
    const host_metadata_record_t **sorted = NULL;
    host_string_pool_t strings;
    host_buffer_t records;
    host_buffer_t body;
    size_t i;
    int strings_ready = 0;
    int ok = 0;

    host_buffer_free(output);
    host_buffer_init(&records);
    host_buffer_init(&body);
    if (!host_game_metadata_sorted(metadata, &sorted)
        || !host_string_pool_init(&strings)) {
        snprintf(error, error_size, "out of memory building game metadata");
        goto out;
    }
    strings_ready = 1;
    for (i = 0; i < metadata->count; ++i) {
        const host_metadata_record_t *record = sorted[i];
        uint8_t name[GAME_METADATA_NAME_BYTES] = {0};
        uint32_t title_offsets[4];
        uint32_t aux_offset;
        size_t title;

        memcpy(name, record->name, strlen(record->name) + 1);
        if (!host_buffer_append(&records, name, sizeof(name)))
            goto oom;
        for (title = 0; title < 4; ++title) {
            if (!host_string_pool_add(&strings, record->titles[title], &title_offsets[title]))
                goto oom;
            if (!host_append_le32(&records, title_offsets[title]))
                goto oom;
        }
        if (!host_string_pool_add(&strings, record->aux_name, &aux_offset)
            || !host_append_le32(&records, aux_offset)
            || !host_append_le32(&records, record->data[0])
            || !host_append_le32(&records, record->data[1])
            || !host_append_le32(&records, record->data[2])
            || !host_buffer_append_byte(&records, record->display_flags)
            || !host_buffer_append_byte(&records, record->core_flags)
            || !host_append_le16(&records, 0))
            goto oom;
    }
    if (records.size != metadata->count * GAME_METADATA_RECORD_SIZE) {
        snprintf(error, error_size, "internal game metadata record size mismatch");
        goto out;
    }
    if (!host_buffer_append(&body, records.data, records.size)
        || !host_buffer_append(&body, strings.data.data, strings.data.size)
        || !host_buffer_append(output, "NJGM", 4)
        || !host_append_le16(output, 1)
        || !host_append_le16(output, (uint16_t)metadata->core)
        || !host_append_le32(output, (uint32_t)metadata->count)
        || !host_append_le32(output, GAME_METADATA_RECORD_SIZE)
        || !host_append_le32(output, GAME_METADATA_HEADER_SIZE)
        || !host_append_le32(output, GAME_METADATA_HEADER_SIZE + (uint32_t)records.size)
        || !host_append_le32(output, (uint32_t)strings.data.size)
        || !host_append_le32(output, host_crc32(body.data, body.size))
        || !host_buffer_append(output, body.data, body.size))
        goto oom;
    ok = 1;
    goto out;
oom:
    snprintf(error, error_size, "out of memory building game metadata");
out:
    free(sorted);
    if (strings_ready)
        host_string_pool_free(&strings);
    host_buffer_free(&records);
    host_buffer_free(&body);
    if (!ok)
        host_buffer_free(output);
    return ok;
}

int host_game_metadata_build_gamelist(const host_game_metadata_t *metadata,
    host_buffer_t *output, char *error, size_t error_size)
{
    const host_metadata_record_t **sorted = NULL;
    size_t i;
    int ok = 0;

    host_buffer_free(output);
    if (metadata->core == HOST_CORE_NCDZ) {
        snprintf(error, error_size, "ncdz: generated gamelist is not supported");
        return 0;
    }
    if (!host_game_metadata_sorted(metadata, &sorted)) {
        snprintf(error, error_size, "out of memory building gamelist");
        return 0;
    }
    if (!host_buffer_append_string(output,
            "-------------------------------------------------------------------------------\n")
        || !host_buffer_append_format(output, "  NJEMU %s game list\n",
            metadata->core == HOST_CORE_CPS1 ? "CPS1"
            : metadata->core == HOST_CORE_CPS2 ? "CPS2" : "MVS")
        || !host_buffer_append_format(output,
            "  Generated from metadata/%s.tsv. Do not edit manually.\n",
            host_game_core_name(metadata->core))
        || !host_buffer_append_string(output,
            "-------------------------------------------------------------------------------\n\n"
            "ROM set         Game title\n"
            "-------------------------------------------------------------------------------\n")) {
        snprintf(error, error_size, "out of memory building gamelist");
        goto out;
    }
    for (i = 0; i < metadata->count; ++i) {
        const host_metadata_record_t *record = sorted[i];
        int first = 1;
        if (!host_buffer_append_format(output, "%-15s %s", record->name, record->titles[0]))
            goto oom;
        if (record->display_flags != 0) {
            if (!host_buffer_append_string(output, " ["))
                goto oom;
            if ((record->display_flags & DISPLAY_NOT_WORK) != 0) {
                if (!host_buffer_append_string(output, "not_work")) goto oom;
                first = 0;
            }
            if ((record->display_flags & DISPLAY_BOOTLEG) != 0) {
                if (!first && !host_buffer_append_string(output, ", ")) goto oom;
                if (!host_buffer_append_string(output, "bootleg")) goto oom;
                first = 0;
            }
            if ((record->display_flags & DISPLAY_HACK) != 0) {
                if (!first && !host_buffer_append_string(output, ", ")) goto oom;
                if (!host_buffer_append_string(output, "hack")) goto oom;
            }
            if (!host_buffer_append_byte(output, ']'))
                goto oom;
        }
        if (!host_buffer_append_byte(output, '\n'))
            goto oom;
    }
    ok = 1;
    goto out;
oom:
    snprintf(error, error_size, "out of memory building gamelist");
out:
    free(sorted);
    if (!ok)
        host_buffer_free(output);
    return ok;
}
