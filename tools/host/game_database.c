#include "game_database.h"

#include "crc32.h"
#include "endian.h"
#include "string_pool.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATABASE_HEADER_SIZE 64u
#define DATABASE_GAME_RECORD_SIZE 40u
#define DATABASE_REGION_RECORD_SIZE 16u
#define DATABASE_ROM_RECORD_SIZE 20u
#define DATABASE_CPS2_RECORD_SIZE 16u
#define DATABASE_PARENT_NONE 0xffffu
#define DATABASE_ROM_FLAG_ROMX 0x01u

static int region_info(const char *name, uint8_t *id, size_t *limit)
{
    if (strcmp(name, "CPU1") == 0) {
        *id = 1; *limit = 8;
    } else if (strcmp(name, "CPU2") == 0) {
        *id = 2; *limit = 3;
    } else if (strcmp(name, "GFX1") == 0) {
        *id = 3; *limit = 32;
    } else if (strcmp(name, "SOUND1") == 0) {
        *id = 4; *limit = 8;
    } else if (strcmp(name, "USER1") == 0) {
        *id = 5; *limit = 0;
    } else {
        return 0;
    }
    return 1;
}

static int ascii_string(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    for (; *p != '\0'; ++p) {
        if (*p >= 0x80)
            return 0;
    }
    return 1;
}

static int sorted_index(const host_metadata_record_t *const *records, size_t count,
    const char *name, uint16_t *index)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (strcmp(records[i]->name, name) == 0) {
            if (i > 0xffffu)
                return 0;
            *index = (uint16_t)i;
            return 1;
        }
    }
    return 0;
}

static int validate_v1(const host_game_metadata_t *metadata,
    const host_rominfo_t *rominfo, const host_metadata_record_t *const *sorted,
    const host_game_record_t **topology, host_game_database_stats_t *stats,
    char *error, size_t error_size)
{
    size_t game_index;

    if (metadata->count == 0) {
        snprintf(error, error_size, "CPS2 database has no games");
        return 0;
    }
    if (metadata->count >= DATABASE_PARENT_NONE) {
        snprintf(error, error_size, "CPS2 game count exceeds uint16 parent/index capacity");
        return 0;
    }
    stats->games = metadata->count;
    stats->regions = 0;
    stats->roms = 0;

    for (game_index = 0; game_index < metadata->count; ++game_index) {
        const host_metadata_record_t *record = sorted[game_index];
        const host_game_record_t *game = host_rominfo_find(rominfo, record->name);
        size_t region_index;

        if (game == NULL) {
            snprintf(error, error_size, "CPS2 identity divergence: missing topology: %s", record->name);
            return 0;
        }
        topology[game_index] = game;
        if (game->machine > 0xffffu || game->input > 0xffffu
            || game->init > 0xffffu || game->rotation > 0xffffu) {
            snprintf(error, error_size, "%s: selector exceeds format V1 uint16 range", record->name);
            return 0;
        }
        if (game->region_count > 0xffu) {
            snprintf(error, error_size, "%s: region count exceeds format V1 range", record->name);
            return 0;
        }
        stats->regions += game->region_count;
        if (stats->regions > 0xffffffffu) {
            snprintf(error, error_size, "CPS2 topology exceeds uint32 section count capacity");
            return 0;
        }

        for (region_index = 0; region_index < game->region_count; ++region_index) {
            const host_region_record_t *region = &game->regions[region_index];
            uint8_t region_id;
            size_t limit;
            size_t previous;
            size_t rom_index;

            if (!region_info(region->name, &region_id, &limit)) {
                snprintf(error, error_size, "%s: unsupported CPS2 region %s",
                    record->name, region->name);
                return 0;
            }
            for (previous = 0; previous < region_index; ++previous) {
                if (strcmp(game->regions[previous].name, region->name) == 0) {
                    snprintf(error, error_size, "%s: duplicate CPS2 region %s",
                        record->name, region->name);
                    return 0;
                }
            }
            if (region->flags > 0xffffu) {
                snprintf(error, error_size, "%s: %s flags exceed format V1 range",
                    record->name, region->name);
                return 0;
            }
            if (region->rom_count > 0xffu || region->rom_count > limit) {
                snprintf(error, error_size,
                    "%s: %s has %lu ROM records; runtime limit is %lu",
                    record->name, region->name, (unsigned long)region->rom_count,
                    (unsigned long)limit);
                return 0;
            }
            stats->roms += region->rom_count;
            if (stats->roms > 0xffffffffu) {
                snprintf(error, error_size, "CPS2 topology exceeds uint32 section count capacity");
                return 0;
            }
            for (rom_index = 0; rom_index < region->rom_count; ++rom_index) {
                const host_rom_record_t *rom = &region->roms[rom_index];
                if (rom->load_type > 0xffu || rom->group > 0xffu || rom->skip > 0xffu) {
                    snprintf(error, error_size, "%s: ROM selector exceeds format V1 range",
                        record->name);
                    return 0;
                }
                if (!ascii_string(rom->name) || strlen(rom->name) >= 32u) {
                    snprintf(error, error_size,
                        "%s: ROM filename '%s' exceeds 31 ASCII characters",
                        record->name, rom->name);
                    return 0;
                }
            }
        }
    }
    return 1;
}

int host_game_database_build(const host_game_metadata_t *metadata,
    const host_rominfo_t *rominfo, host_buffer_t *output,
    host_game_database_stats_t *stats, char *error, size_t error_size)
{
    const host_metadata_record_t **sorted = NULL;
    const host_game_record_t **topology = NULL;
    host_string_pool_t strings;
    host_buffer_t games;
    host_buffer_t regions;
    host_buffer_t roms;
    host_buffer_t cores;
    host_buffer_t body;
    size_t game_index;
    uint32_t region_index = 0;
    uint32_t rom_index = 0;
    int strings_ready = 0;
    int ok = 0;

    host_buffer_free(output);
    host_buffer_init(&games);
    host_buffer_init(&regions);
    host_buffer_init(&roms);
    host_buffer_init(&cores);
    host_buffer_init(&body);

    if (metadata->core != HOST_CORE_CPS2) {
        snprintf(error, error_size, "game database V1 only supports CPS2");
        goto out;
    }
    if (!host_game_metadata_sorted(metadata, &sorted)) {
        snprintf(error, error_size, "out of memory sorting CPS2 games");
        goto out;
    }
    topology = (const host_game_record_t **)malloc(metadata->count * sizeof(*topology));
    if (topology == NULL) {
        snprintf(error, error_size, "out of memory building CPS2 database");
        goto out;
    }
    if (!validate_v1(metadata, rominfo, sorted, topology, stats, error, error_size))
        goto out;
    if (!host_string_pool_init(&strings)) {
        snprintf(error, error_size, "out of memory building CPS2 database");
        goto out;
    }
    strings_ready = 1;

    for (game_index = 0; game_index < metadata->count; ++game_index) {
        const host_metadata_record_t *record = sorted[game_index];
        const host_game_record_t *game = topology[game_index];
        uint16_t parent_index = DATABASE_PARENT_NONE;
        uint32_t name_offset;
        uint32_t title_offsets[4];
        uint32_t aux_offset;
        uint32_t first_region = region_index;
        size_t title;
        size_t region;

        if (strcmp(game->parent, "cps2") != 0
            && !sorted_index(sorted, metadata->count, game->parent, &parent_index)) {
            snprintf(error, error_size, "%s: unresolved parent %s", record->name, game->parent);
            goto out;
        }
        if (!host_string_pool_add(&strings, record->name, &name_offset))
            goto oom;
        for (title = 0; title < 4; ++title) {
            if (!host_string_pool_add(&strings, record->titles[title], &title_offsets[title]))
                goto oom;
        }
        if (!host_string_pool_add(&strings, record->aux_name, &aux_offset))
            goto oom;

        for (region = 0; region < game->region_count; ++region) {
            const host_region_record_t *source_region = &game->regions[region];
            uint8_t region_id;
            size_t limit;
            uint32_t first_rom = rom_index;
            size_t rom;

            if (!region_info(source_region->name, &region_id, &limit)) {
                snprintf(error, error_size, "%s: unsupported CPS2 region %s",
                    record->name, source_region->name);
                goto out;
            }
            (void)limit;
            for (rom = 0; rom < source_region->rom_count; ++rom) {
                const host_rom_record_t *source_rom = &source_region->roms[rom];
                uint32_t rom_name_offset;
                uint8_t flags = source_rom->is_romx ? DATABASE_ROM_FLAG_ROMX : 0;
                if (!host_string_pool_add(&strings, source_rom->name, &rom_name_offset)
                    || !host_append_le32(&roms, rom_name_offset)
                    || !host_append_le32(&roms, source_rom->offset)
                    || !host_append_le32(&roms, source_rom->length)
                    || !host_append_le32(&roms, source_rom->crc)
                    || !host_buffer_append_byte(&roms, (uint8_t)source_rom->load_type)
                    || !host_buffer_append_byte(&roms, (uint8_t)source_rom->group)
                    || !host_buffer_append_byte(&roms, (uint8_t)source_rom->skip)
                    || !host_buffer_append_byte(&roms, flags))
                    goto oom;
                ++rom_index;
            }
            if (!host_append_le16(&regions, (uint16_t)game_index)
                || !host_buffer_append_byte(&regions, region_id)
                || !host_buffer_append_byte(&regions, (uint8_t)source_region->rom_count)
                || !host_append_le32(&regions, source_region->size)
                || !host_append_le32(&regions, first_rom)
                || !host_append_le16(&regions, (uint16_t)source_region->flags)
                || !host_append_le16(&regions, 0))
                goto oom;
            ++region_index;
        }

        if (!host_append_le32(&cores, record->data[0])
            || !host_append_le32(&cores, record->data[1])
            || !host_append_le32(&cores, record->data[2])
            || !host_append_le32(&cores, aux_offset)
            || !host_append_le32(&games, name_offset)
            || !host_append_le16(&games, parent_index)
            || !host_buffer_append_byte(&games, (uint8_t)game->region_count)
            || !host_buffer_append_byte(&games, record->display_flags)
            || !host_append_le16(&games, (uint16_t)game->machine)
            || !host_append_le16(&games, (uint16_t)game->input)
            || !host_append_le16(&games, (uint16_t)game->init)
            || !host_append_le16(&games, (uint16_t)game->rotation)
            || !host_append_le32(&games, first_region))
            goto oom;
        for (title = 0; title < 4; ++title) {
            if (!host_append_le32(&games, title_offsets[title]))
                goto oom;
        }
        if (!host_buffer_append_byte(&games, record->core_flags)
            || !host_buffer_append_byte(&games, 0)
            || !host_append_le16(&games, (uint16_t)game_index))
            goto oom;
    }

    if (games.size != metadata->count * DATABASE_GAME_RECORD_SIZE
        || regions.size != stats->regions * DATABASE_REGION_RECORD_SIZE
        || roms.size != stats->roms * DATABASE_ROM_RECORD_SIZE
        || cores.size != metadata->count * DATABASE_CPS2_RECORD_SIZE) {
        snprintf(error, error_size, "internal CPS2 database record size mismatch");
        goto out;
    }

    if (!host_buffer_append(&body, games.data, games.size)
        || !host_buffer_append(&body, regions.data, regions.size)
        || !host_buffer_append(&body, roms.data, roms.size)
        || !host_buffer_append(&body, cores.data, cores.size)
        || !host_buffer_append(&body, strings.data.data, strings.data.size))
        goto oom;
    if (body.size > 0xffffffffu - DATABASE_HEADER_SIZE) {
        snprintf(error, error_size, "database exceeds uint32 file size capacity");
        goto out;
    }

    {
        uint32_t games_offset = DATABASE_HEADER_SIZE;
        uint32_t regions_offset = games_offset + (uint32_t)games.size;
        uint32_t roms_offset = regions_offset + (uint32_t)regions.size;
        uint32_t core_offset = roms_offset + (uint32_t)roms.size;
        uint32_t strings_offset = core_offset + (uint32_t)cores.size;
        uint32_t file_size = DATABASE_HEADER_SIZE + (uint32_t)body.size;

        if (!host_buffer_append(output, "NJGD", 4)
            || !host_append_le16(output, 1)
            || !host_append_le16(output, HOST_CORE_CPS2)
            || !host_append_le16(output, DATABASE_HEADER_SIZE)
            || !host_append_le16(output, DATABASE_GAME_RECORD_SIZE)
            || !host_append_le16(output, DATABASE_REGION_RECORD_SIZE)
            || !host_append_le16(output, DATABASE_ROM_RECORD_SIZE)
            || !host_append_le16(output, DATABASE_CPS2_RECORD_SIZE)
            || !host_append_le16(output, 0)
            || !host_append_le32(output, (uint32_t)metadata->count)
            || !host_append_le32(output, region_index)
            || !host_append_le32(output, rom_index)
            || !host_append_le32(output, games_offset)
            || !host_append_le32(output, regions_offset)
            || !host_append_le32(output, roms_offset)
            || !host_append_le32(output, core_offset)
            || !host_append_le32(output, strings_offset)
            || !host_append_le32(output, (uint32_t)strings.data.size)
            || !host_append_le32(output, host_crc32(body.data, body.size))
            || !host_append_le32(output, file_size)
            || !host_buffer_append(output, body.data, body.size))
            goto oom;
    }
    ok = 1;
    goto out;
oom:
    snprintf(error, error_size, "out of memory building CPS2 database");
out:
    if (strings_ready)
        host_string_pool_free(&strings);
    host_buffer_free(&games);
    host_buffer_free(&regions);
    host_buffer_free(&roms);
    host_buffer_free(&cores);
    host_buffer_free(&body);
    free(topology);
    free(sorted);
    if (!ok)
        host_buffer_free(output);
    return ok;
}
