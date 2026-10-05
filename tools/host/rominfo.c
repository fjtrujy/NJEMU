#include "rominfo.h"

#include "file.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void free_game(host_game_record_t *game)
{
    size_t i;

    free(game->name);
    free(game->parent);
    for (i = 0; i < game->region_count; ++i) {
        size_t j;
        host_region_record_t *region = &game->regions[i];
        free(region->name);
        for (j = 0; j < region->rom_count; ++j)
            free(region->roms[j].name);
        free(region->roms);
    }
    free(game->regions);
    memset(game, 0, sizeof(*game));
}

void host_rominfo_init(host_rominfo_t *rominfo)
{
    rominfo->games = NULL;
    rominfo->game_count = 0;
}

void host_rominfo_free(host_rominfo_t *rominfo)
{
    size_t i;
    for (i = 0; i < rominfo->game_count; ++i)
        free_game(&rominfo->games[i]);
    free(rominfo->games);
    host_rominfo_init(rominfo);
}

const host_game_record_t *host_rominfo_find(const host_rominfo_t *rominfo, const char *name)
{
    size_t i;
    for (i = 0; i < rominfo->game_count; ++i) {
        if (strcmp(rominfo->games[i].name, name) == 0)
            return &rominfo->games[i];
    }
    return NULL;
}

static host_game_record_t *append_game(host_rominfo_t *rominfo)
{
    host_game_record_t *games = (host_game_record_t *)realloc(
        rominfo->games, (rominfo->game_count + 1) * sizeof(*rominfo->games));
    host_game_record_t *game;

    if (games == NULL)
        return NULL;
    rominfo->games = games;
    game = &rominfo->games[rominfo->game_count++];
    memset(game, 0, sizeof(*game));
    return game;
}

static host_region_record_t *append_region(host_game_record_t *game)
{
    host_region_record_t *regions = (host_region_record_t *)realloc(
        game->regions, (game->region_count + 1) * sizeof(*game->regions));
    host_region_record_t *region;

    if (regions == NULL)
        return NULL;
    game->regions = regions;
    region = &game->regions[game->region_count++];
    memset(region, 0, sizeof(*region));
    return region;
}

static host_rom_record_t *append_rom(host_region_record_t *region)
{
    host_rom_record_t *roms = (host_rom_record_t *)realloc(
        region->roms, (region->rom_count + 1) * sizeof(*region->roms));
    host_rom_record_t *rom;

    if (roms == NULL)
        return NULL;
    region->roms = roms;
    rom = &region->roms[region->rom_count++];
    memset(rom, 0, sizeof(*rom));
    return rom;
}

static int split_fields(char *body, char **fields, size_t capacity, size_t *count)
{
    char *cursor = body;
    size_t used = 0;

    for (;;) {
        char *comma;
        if (used >= capacity)
            return 0;
        comma = strchr(cursor, ',');
        if (comma != NULL)
            *comma = '\0';
        fields[used] = host_trim(cursor);
        if (*fields[used] == '\0')
            return 0;
        ++used;
        if (comma == NULL)
            break;
        cursor = comma + 1;
    }
    *count = used;
    return 1;
}

static int parse_macro(char *line, char **macro, char **body)
{
    char *comment = strstr(line, "//");
    char *open;
    char *close;
    char *cursor;

    if (comment != NULL)
        *comment = '\0';
    line = host_trim(line);
    open = strchr(line, '(');
    close = strrchr(line, ')');
    if (open == NULL || close == NULL || close < open)
        return 0;
    *open = '\0';
    *close = '\0';
    *macro = host_trim(line);
    for (cursor = *macro; *cursor != '\0'; ++cursor) {
        if (*cursor < 'A' || *cursor > 'Z')
            return 0;
    }
    cursor = host_trim(close + 1);
    if (*cursor != '\0')
        return 0;
    *body = open + 1;
    return **macro != '\0';
}

static int parse_u32_field(const char *path, size_t line_number, const char *text,
    const char *field, uint32_t *value, char *error, size_t error_size)
{
    if (host_parse_u32(text, value))
        return 1;
    snprintf(error, error_size, "%s:%lu: invalid %s value '%s'", path,
        (unsigned long)line_number, field, text);
    return 0;
}

int host_rominfo_load_filenames(const char *path, host_rominfo_t *rominfo,
    char *error, size_t error_size)
{
    host_buffer_t file;
    char *cursor;
    int ok = 0;

    host_rominfo_free(rominfo);
    host_buffer_init(&file);
    if (!host_read_file(path, &file)) {
        snprintf(error, error_size, "could not read %s", path);
        return 0;
    }
    cursor = (char *)file.data;
    while (*cursor != '\0') {
        char *line = cursor;
        char *end = strchr(cursor, '\n');
        char *open;
        char *first_comma;
        char *second_comma;
        char *name;
        char *parent;
        host_game_record_t *game;

        if (end != NULL) {
            *end = '\0';
            cursor = end + 1;
        } else {
            cursor += strlen(cursor);
        }
        end = line + strlen(line);
        if (end > line && end[-1] == '\r')
            end[-1] = '\0';
        line = host_trim(line);
        if (strncmp(line, "FILENAME", 8) != 0)
            continue;
        open = line + 8;
        while (*open == ' ' || *open == '\t')
            ++open;
        if (*open != '(')
            continue;
        ++open;
        first_comma = strchr(open, ',');
        if (first_comma == NULL)
            continue;
        *first_comma = '\0';
        second_comma = strchr(first_comma + 1, ',');
        if (second_comma == NULL)
            continue;
        *second_comma = '\0';
        name = host_trim(open);
        parent = host_trim(first_comma + 1);
        if (*name == '\0' || *parent == '\0')
            continue;
        if (host_rominfo_find(rominfo, name) != NULL) {
            snprintf(error, error_size, "%s: duplicate FILENAME record %s", path, name);
            goto out;
        }
        game = append_game(rominfo);
        if (game == NULL) {
            snprintf(error, error_size, "out of memory parsing %s", path);
            goto out;
        }
        game->name = host_strdup(name);
        game->parent = host_strdup(parent);
        if (game->name == NULL || game->parent == NULL) {
            snprintf(error, error_size, "out of memory parsing %s", path);
            goto out;
        }
    }
    if (rominfo->game_count == 0) {
        snprintf(error, error_size, "%s: no FILENAME records found", path);
        goto out;
    }
    ok = 1;
out:
    host_buffer_free(&file);
    if (!ok)
        host_rominfo_free(rominfo);
    return ok;
}

int host_rominfo_load(const char *path, host_rominfo_t *rominfo, char *error, size_t error_size)
{
    host_buffer_t file;
    char *cursor;
    size_t line_number = 0;
    host_game_record_t *current_game = NULL;
    host_region_record_t *current_region = NULL;
    int ok = 0;

    host_rominfo_free(rominfo);
    host_buffer_init(&file);
    if (!host_read_file(path, &file)) {
        snprintf(error, error_size, "could not read %s", path);
        return 0;
    }

    cursor = (char *)file.data;
    while (*cursor != '\0') {
        char *line = cursor;
        char *end = strchr(cursor, '\n');
        char *trimmed;
        char *macro;
        char *body;
        char *fields[8];
        size_t field_count = 0;

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
        trimmed = host_trim(line);
        if (*trimmed == '\0' || (trimmed[0] == '/' && trimmed[1] == '/'))
            continue;
        if (strcmp(trimmed, "END") == 0) {
            if (current_game == NULL) {
                snprintf(error, error_size, "%s:%lu: END outside FILENAME record",
                    path, (unsigned long)line_number);
                goto out;
            }
            current_game = NULL;
            current_region = NULL;
            continue;
        }
        if (!parse_macro(trimmed, &macro, &body)) {
            snprintf(error, error_size, "%s:%lu: unrecognized rominfo syntax",
                path, (unsigned long)line_number);
            goto out;
        }
        if (!split_fields(body, fields, sizeof(fields) / sizeof(fields[0]), &field_count)) {
            snprintf(error, error_size, "%s:%lu: empty or excessive macro field",
                path, (unsigned long)line_number);
            goto out;
        }

        if (strcmp(macro, "FILENAME") == 0) {
            host_game_record_t *game;
            if (current_game != NULL) {
                snprintf(error, error_size, "%s:%lu: FILENAME before previous END",
                    path, (unsigned long)line_number);
                goto out;
            }
            if (field_count != 6) {
                snprintf(error, error_size, "%s:%lu: FILENAME requires 6 fields",
                    path, (unsigned long)line_number);
                goto out;
            }
            if (host_rominfo_find(rominfo, fields[0]) != NULL) {
                snprintf(error, error_size, "%s:%lu: duplicate FILENAME record %s",
                    path, (unsigned long)line_number, fields[0]);
                goto out;
            }
            game = append_game(rominfo);
            if (game == NULL) {
                snprintf(error, error_size, "out of memory parsing %s", path);
                goto out;
            }
            game->name = host_strdup(fields[0]);
            game->parent = host_strdup(fields[1]);
            if (game->name == NULL || game->parent == NULL
                || !parse_u32_field(path, line_number, fields[2], "machine", &game->machine, error, error_size)
                || !parse_u32_field(path, line_number, fields[3], "input", &game->input, error, error_size)
                || !parse_u32_field(path, line_number, fields[4], "init", &game->init, error, error_size)
                || !parse_u32_field(path, line_number, fields[5], "rotation", &game->rotation, error, error_size))
                goto out;
            current_game = game;
            current_region = NULL;
            continue;
        }

        if (current_game == NULL) {
            snprintf(error, error_size, "%s:%lu: %s outside FILENAME record",
                path, (unsigned long)line_number, macro);
            goto out;
        }
        if (strcmp(macro, "REGION") == 0) {
            host_region_record_t *region;
            if (field_count != 3) {
                snprintf(error, error_size, "%s:%lu: REGION requires 3 fields",
                    path, (unsigned long)line_number);
                goto out;
            }
            region = append_region(current_game);
            if (region == NULL) {
                snprintf(error, error_size, "out of memory parsing %s", path);
                goto out;
            }
            region->name = host_strdup(fields[1]);
            if (region->name == NULL
                || !parse_u32_field(path, line_number, fields[0], "region size", &region->size, error, error_size)
                || !parse_u32_field(path, line_number, fields[2], "region flags", &region->flags, error, error_size))
                goto out;
            current_region = region;
            continue;
        }

        if (strcmp(macro, "ROM") == 0 || strcmp(macro, "ROMX") == 0) {
            int is_romx = strcmp(macro, "ROMX") == 0;
            uint32_t load_type;
            int continuation;
            size_t expected;
            size_t field = 1;
            host_rom_record_t *rom;

            if (current_region == NULL) {
                snprintf(error, error_size, "%s:%lu: %s outside REGION record",
                    path, (unsigned long)line_number, macro);
                goto out;
            }
            if (!parse_u32_field(path, line_number, fields[0], "ROM load type", &load_type,
                    error, error_size))
                goto out;
            continuation = load_type == 1;
            expected = is_romx ? (continuation ? 6u : 7u) : (continuation ? 4u : 5u);
            if (field_count != expected) {
                snprintf(error, error_size, "%s:%lu: %s requires %lu fields for load type %u",
                    path, (unsigned long)line_number, macro, (unsigned long)expected, load_type);
                goto out;
            }
            rom = append_rom(current_region);
            if (rom == NULL) {
                snprintf(error, error_size, "out of memory parsing %s", path);
                goto out;
            }
            rom->load_type = load_type;
            rom->is_romx = is_romx;
            if (continuation) {
                rom->name = host_strdup("");
            } else {
                rom->name = host_strdup(fields[field++]);
            }
            if (rom->name == NULL
                || !parse_u32_field(path, line_number, fields[field++], "ROM offset", &rom->offset, error, error_size)
                || !parse_u32_field(path, line_number, fields[field++], "ROM length", &rom->length, error, error_size)
                || !parse_u32_field(path, line_number, fields[field++], "ROM CRC", &rom->crc, error, error_size))
                goto out;
            if (is_romx) {
                if (!parse_u32_field(path, line_number, fields[field++], "ROMX group", &rom->group, error, error_size)
                    || !parse_u32_field(path, line_number, fields[field++], "ROMX skip", &rom->skip, error, error_size))
                    goto out;
            }
            continue;
        }

        snprintf(error, error_size, "%s:%lu: unsupported macro %s",
            path, (unsigned long)line_number, macro);
        goto out;
    }

    if (current_game != NULL) {
        snprintf(error, error_size, "%s:%lu: unterminated FILENAME record",
            path, (unsigned long)(line_number != 0 ? line_number : 1));
        goto out;
    }
    if (rominfo->game_count == 0) {
        snprintf(error, error_size, "%s: no FILENAME records found", path);
        goto out;
    }
    ok = 1;
out:
    host_buffer_free(&file);
    if (!ok)
        host_rominfo_free(rominfo);
    return ok;
}

static int game_index(const host_rominfo_t *rominfo, const char *name)
{
    size_t i;
    for (i = 0; i < rominfo->game_count; ++i) {
        if (strcmp(rominfo->games[i].name, name) == 0)
            return (int)i;
    }
    return -1;
}

static int visit_parent(const host_rominfo_t *rominfo, size_t index, const char *root_parent,
    uint8_t *state, char *error, size_t error_size)
{
    const host_game_record_t *game = &rominfo->games[index];
    int parent;

    if (state[index] == 2)
        return 1;
    if (state[index] == 1) {
        snprintf(error, error_size, "parent cycle detected at %s", game->name);
        return 0;
    }
    state[index] = 1;
    if (strcmp(game->parent, root_parent) != 0) {
        parent = game_index(rominfo, game->parent);
        if (parent < 0) {
            snprintf(error, error_size, "%s: unresolved parent %s", game->name, game->parent);
            return 0;
        }
        if (!visit_parent(rominfo, (size_t)parent, root_parent, state, error, error_size))
            return 0;
    }
    state[index] = 2;
    return 1;
}

int host_rominfo_validate_parents(const host_rominfo_t *rominfo, const char *root_parent,
    char *error, size_t error_size)
{
    uint8_t *state;
    size_t i;
    int ok = 1;

    state = (uint8_t *)calloc(rominfo->game_count, 1);
    if (state == NULL) {
        snprintf(error, error_size, "out of memory validating parent graph");
        return 0;
    }
    for (i = 0; i < rominfo->game_count; ++i) {
        if (!visit_parent(rominfo, i, root_parent, state, error, error_size)) {
            ok = 0;
            break;
        }
    }
    free(state);
    return ok;
}
