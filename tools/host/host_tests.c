#include "crc32.h"
#include "dip_metadata.h"
#include "endian.h"
#include "file.h"
#include "game_database.h"
#include "game_metadata.h"
#include "rominfo.h"
#include "translation_source.h"
#include "translations.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "host test failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 0; \
    } \
} while (0)

static int path_join(char *output, size_t output_size, const char *a, const char *b)
{
    size_t length = strlen(a);
    const char *separator = length != 0 && (a[length - 1] == '/' || a[length - 1] == '\\') ? "" : "/";
    int written = snprintf(output, output_size, "%s%s%s", a, separator, b);
    return written >= 0 && (size_t)written < output_size;
}

static int write_text(const char *path, const char *text)
{
    return host_make_parent_directories(path) && host_write_file(path, text, strlen(text));
}

static int buffer_equal(const host_buffer_t *a, const host_buffer_t *b)
{
    return a->size == b->size && (a->size == 0 || memcmp(a->data, b->data, a->size) == 0);
}

static int buffer_contains(const host_buffer_t *buffer, const char *needle)
{
    size_t needle_size = strlen(needle);
    size_t i;
    if (needle_size == 0)
        return 1;
    if (needle_size > buffer->size)
        return 0;
    for (i = 0; i <= buffer->size - needle_size; ++i) {
        if (memcmp(buffer->data + i, needle, needle_size) == 0)
            return 1;
    }
    return 0;
}

static int load_core_metadata(const char *source_dir, const char *core_name,
    host_game_core_t core, int strict_rominfo, host_game_metadata_t *metadata,
    host_rominfo_t *rominfo, char *error, size_t error_size)
{
    char path[1024];
    char relative[256];

    snprintf(relative, sizeof(relative), "metadata/%s.tsv", core_name);
    if (!path_join(path, sizeof(path), source_dir, relative)
        || !host_game_metadata_load(path, core, metadata, error, error_size))
        return 0;
    if (core == HOST_CORE_NCDZ)
        return host_game_metadata_validate(metadata, NULL, error, error_size);
    snprintf(relative, sizeof(relative), "resources/%s/rominfo.%s", core_name, core_name);
    if (!path_join(path, sizeof(path), source_dir, relative))
        return 0;
    if (strict_rominfo) {
        if (!host_rominfo_load(path, rominfo, error, error_size))
            return 0;
    } else if (!host_rominfo_load_filenames(path, rominfo, error, error_size)) {
        return 0;
    }
    return host_game_metadata_validate(metadata, rominfo, error, error_size);
}

static int test_metadata(const char *source_dir, const char *scratch_dir)
{
    static const struct {
        const char *name;
        host_game_core_t core;
        size_t count;
    } cases[] = {
        {"cps1", HOST_CORE_CPS1, 137},
        {"cps2", HOST_CORE_CPS2, 286},
        {"mvs", HOST_CORE_MVS, 305},
        {"ncdz", HOST_CORE_NCDZ, 97},
    };
    char error[1024];
    size_t c;
    (void)scratch_dir;

    for (c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c) {
        host_game_metadata_t metadata;
        host_rominfo_t rominfo;
        host_buffer_t first;
        host_buffer_t second;
        const host_metadata_record_t *record;
        uint32_t expected_crc;

        host_game_metadata_init(&metadata);
        host_rominfo_init(&rominfo);
        host_buffer_init(&first);
        host_buffer_init(&second);
        CHECK(load_core_metadata(source_dir, cases[c].name, cases[c].core, 0,
            &metadata, &rominfo, error, sizeof(error)));
        CHECK(metadata.count == cases[c].count);
        CHECK(host_game_metadata_build_blob(&metadata, &first, error, sizeof(error)));
        CHECK(host_game_metadata_build_blob(&metadata, &second, error, sizeof(error)));
        CHECK(buffer_equal(&first, &second));
        CHECK(first.size >= 32);
        CHECK(memcmp(first.data, "NJGM", 4) == 0);
        CHECK(host_read_le16(first.data + 4) == 1);
        CHECK(host_read_le16(first.data + 6) == (uint16_t)cases[c].core);
        CHECK(host_read_le32(first.data + 8) == cases[c].count);
        CHECK(host_read_le32(first.data + 12) == 52);
        CHECK(host_read_le32(first.data + 16) == 32);
        CHECK((size_t)host_read_le32(first.data + 20)
            + (size_t)host_read_le32(first.data + 24) == first.size);
        expected_crc = host_crc32(first.data + 32, first.size - 32);
        CHECK(host_read_le32(first.data + 28) == expected_crc);

        if (cases[c].core != HOST_CORE_NCDZ) {
            host_buffer_t list_a;
            host_buffer_t list_b;
            host_buffer_init(&list_a);
            host_buffer_init(&list_b);
            CHECK(host_game_metadata_build_gamelist(&metadata, &list_a, error, sizeof(error)));
            CHECK(host_game_metadata_build_gamelist(&metadata, &list_b, error, sizeof(error)));
            CHECK(buffer_equal(&list_a, &list_b));
            CHECK(buffer_contains(&list_a, "Generated from metadata/"));
            if (cases[c].core == HOST_CORE_CPS1) {
                CHECK(buffer_contains(&list_a, "sf2m13"));
                CHECK(buffer_contains(&list_a, "[bootleg]"));
            } else if (cases[c].core == HOST_CORE_MVS) {
                CHECK(buffer_contains(&list_a, "kof2001d"));
                CHECK(buffer_contains(&list_a, "[not_work"));
            }
            host_buffer_free(&list_b);
            host_buffer_free(&list_a);
        } else {
            host_buffer_t list;
            host_buffer_init(&list);
            CHECK(!host_game_metadata_build_gamelist(&metadata, &list, error, sizeof(error)));
            CHECK(strstr(error, "not supported") != NULL);
            host_buffer_free(&list);
        }

        if (cases[c].core == HOST_CORE_CPS2) {
            record = host_game_metadata_find(&metadata, "ssf2");
            CHECK(record != NULL);
            CHECK(record->data[0] == 0x23456789u && record->data[1] == 0xabcdef01u
                && record->data[2] == 0x00400000u);
            record = host_game_metadata_find(&metadata, "ddtodd");
            CHECK(record != NULL && (record->core_flags & 0x01u) != 0 && record->data[0] == 0);
            record = host_game_metadata_find(&metadata, "ssf2ta");
            CHECK(record != NULL && (record->core_flags & 0x02u) != 0
                && strcmp(record->aux_name, "ssf2t") == 0);
            record = host_game_metadata_find(&metadata, "mpangj");
            CHECK(record != NULL && (record->core_flags & 0x04u) != 0);
            record = host_game_metadata_find(&metadata, "jyangoku");
            CHECK(record != NULL && record->data[2] == 0);
            CHECK(host_game_metadata_find(&metadata, "gigaman2") == NULL);
        } else if (cases[c].core == HOST_CORE_MVS) {
            static const char *covered[] = {"kof2001d", "kof2k1hd", "kof2kd", "roboarma", "samsho2k2"};
            static const char *stale[] = {"fatfursa", "kf2k2ur", "kof96pm", "kof97c", "kof97prc", "kof97xt", "kof98a", "kof98evo"};
            size_t i;
            for (i = 0; i < sizeof(covered) / sizeof(covered[0]); ++i) {
                record = host_game_metadata_find(&metadata, covered[i]);
                CHECK(record != NULL && record->titles[0][0] != '\0');
            }
            for (i = 0; i < sizeof(stale) / sizeof(stale[0]); ++i)
                CHECK(host_game_metadata_find(&metadata, stale[i]) == NULL);
            record = host_game_metadata_find(&metadata, "kof96ae");
            CHECK(record != NULL && record->core_flags == 0x07u);
            record = host_game_metadata_find(&metadata, "kof97ps");
            CHECK(record != NULL && record->core_flags == 0x01u);
            record = host_game_metadata_find(&metadata, "matrimbl");
            CHECK(record != NULL && record->core_flags == 0x04u);
            record = host_game_metadata_find(&metadata, "mslug");
            CHECK(record != NULL && record->core_flags == 0);
        } else if (cases[c].core == HOST_CORE_NCDZ) {
            record = host_game_metadata_find(&metadata, "lastbld2");
            CHECK(record != NULL && record->data[0] == 0x0243u);
            record = host_game_metadata_find(&metadata, "fatfury3");
            CHECK(record != NULL && record->data[0] == 0x069cu);
        }

        if (cases[c].core == HOST_CORE_CPS1) {
            size_t saved_count = metadata.count;
            metadata.count--;
            CHECK(!host_game_metadata_validate(&metadata, &rominfo, error, sizeof(error)));
            CHECK(strstr(error, "identity divergence") != NULL);
            metadata.count = saved_count;
        }

        host_buffer_free(&second);
        host_buffer_free(&first);
        host_rominfo_free(&rominfo);
        host_game_metadata_free(&metadata);
    }
    return 1;
}

static int test_rominfo(const char *source_dir, const char *scratch_dir)
{
    host_rominfo_t rominfo;
    char source_path[1024];
    char test_path[1024];
    char error[1024];
    const host_game_record_t *game;
    const host_region_record_t *region = NULL;
    size_t regions = 0;
    size_t roms = 0;
    size_t i;
    const char *duplicate =
        "FILENAME( a, cps2, 0, 0, 0, 0 )\nEND\n"
        "FILENAME( a, cps2, 0, 0, 0, 0 )\nEND\n";
    const char *cycle =
        "FILENAME( a, b, 0, 0, 0, 0 )\nEND\n"
        "FILENAME( b, a, 0, 0, 0, 0 )\nEND\n";
    const char *unresolved = "FILENAME( a, missing, 0, 0, 0, 0 )\nEND\n";
    const char *malformed =
        "FILENAME( a, cps2, 0, 0, 0, 0 )\n"
        "REGION( 0x100, CPU1, 0 )\n"
        "ROM( 0, foo.bin, 0, 0x10 )\nEND\n";

    host_rominfo_init(&rominfo);
    CHECK(path_join(source_path, sizeof(source_path), source_dir, "resources/cps2/rominfo.cps2"));
    CHECK(host_rominfo_load(source_path, &rominfo, error, sizeof(error)));
    CHECK(host_rominfo_validate_parents(&rominfo, "cps2", error, sizeof(error)));
    CHECK(rominfo.game_count == 286);
    for (i = 0; i < rominfo.game_count; ++i) {
        size_t j;
        regions += rominfo.games[i].region_count;
        for (j = 0; j < rominfo.games[i].region_count; ++j)
            roms += rominfo.games[i].regions[j].rom_count;
    }
    CHECK(regions == 1387 && roms == 5382);
    game = host_rominfo_find(&rominfo, "ssf2");
    CHECK(game != NULL && strcmp(game->parent, "cps2") == 0
        && game->machine == 0 && game->input == 1 && game->init == 0 && game->rotation == 0);
    game = host_rominfo_find(&rominfo, "ssf2ta");
    CHECK(game != NULL && strcmp(game->parent, "ssf2t") == 0);
    game = host_rominfo_find(&rominfo, "1944");
    CHECK(game != NULL);
    for (i = 0; i < game->region_count; ++i) {
        if (strcmp(game->regions[i].name, "CPU1") == 0)
            CHECK(game->regions[i].size == 0x180000u);
        if (strcmp(game->regions[i].name, "USER1") == 0)
            CHECK(game->regions[i].size == 0x080000u);
        if (strcmp(game->regions[i].name, "CPU2") == 0) {
            region = &game->regions[i];
            CHECK(region->rom_count == 2);
            CHECK(strcmp(region->roms[0].name, "nff.01") == 0 && region->roms[0].load_type == 0);
            CHECK(region->roms[1].load_type == 1 && region->roms[1].name[0] == '\0');
            CHECK(region->roms[1].offset == 0x10000u && region->roms[1].length == 0x18000u
                && region->roms[1].crc == 0);
        }
        if (strcmp(game->regions[i].name, "GFX1") == 0) {
            region = &game->regions[i];
            CHECK(region->rom_count > 0 && region->roms[0].is_romx);
            CHECK(strcmp(region->roms[0].name, "nff.13m") == 0);
            CHECK(region->roms[0].group == 2 && region->roms[0].skip == 6
                && region->roms[0].crc == 0xc9fca741u);
        }
    }
    host_rominfo_free(&rominfo);

    CHECK(path_join(test_path, sizeof(test_path), scratch_dir, "duplicate.rominfo"));
    CHECK(write_text(test_path, duplicate));
    CHECK(!host_rominfo_load(test_path, &rominfo, error, sizeof(error)));
    CHECK(strstr(error, "duplicate FILENAME") != NULL);

    CHECK(path_join(test_path, sizeof(test_path), scratch_dir, "cycle.rominfo"));
    CHECK(write_text(test_path, cycle));
    CHECK(host_rominfo_load(test_path, &rominfo, error, sizeof(error)));
    CHECK(!host_rominfo_validate_parents(&rominfo, "cps2", error, sizeof(error)));
    CHECK(strstr(error, "parent cycle") != NULL);
    host_rominfo_free(&rominfo);

    CHECK(path_join(test_path, sizeof(test_path), scratch_dir, "unresolved.rominfo"));
    CHECK(write_text(test_path, unresolved));
    CHECK(host_rominfo_load(test_path, &rominfo, error, sizeof(error)));
    CHECK(!host_rominfo_validate_parents(&rominfo, "cps2", error, sizeof(error)));
    CHECK(strstr(error, "unresolved parent") != NULL);
    host_rominfo_free(&rominfo);

    CHECK(path_join(test_path, sizeof(test_path), scratch_dir, "malformed.rominfo"));
    CHECK(write_text(test_path, malformed));
    CHECK(!host_rominfo_load(test_path, &rominfo, error, sizeof(error)));
    CHECK(strstr(error, "ROM requires 5 fields") != NULL);
    host_rominfo_free(&rominfo);
    return 1;
}

static int test_game_database(const char *source_dir, const char *scratch_dir)
{
    host_game_metadata_t metadata;
    host_rominfo_t rominfo;
    host_buffer_t first;
    host_buffer_t second;
    host_game_database_stats_t stats;
    char error[1024];
    char rominfo_path[1024];
    host_buffer_t rominfo_bytes;
    const host_game_record_t *const_game;
    host_game_record_t *game;
    size_t saved_metadata_count;
    host_region_record_t *saved_regions;
    size_t saved_region_count;
    host_region_record_t duplicate_regions[2];
    size_t saved_rom_count;
    (void)scratch_dir;

    host_game_metadata_init(&metadata);
    host_rominfo_init(&rominfo);
    host_buffer_init(&first);
    host_buffer_init(&second);
    host_buffer_init(&rominfo_bytes);
    CHECK(load_core_metadata(source_dir, "cps2", HOST_CORE_CPS2, 1,
        &metadata, &rominfo, error, sizeof(error)));
    CHECK(host_game_database_build(&metadata, &rominfo, &first, &stats, error, sizeof(error)));
    CHECK(stats.games == 286 && stats.regions == 1387 && stats.roms == 5382);
    CHECK(host_game_database_build(&metadata, &rominfo, &second, &stats, error, sizeof(error)));
    CHECK(buffer_equal(&first, &second));
    CHECK(first.size == 215482u);
    CHECK(memcmp(first.data, "NJGD", 4) == 0);
    CHECK(host_read_le16(first.data + 4) == 1 && host_read_le16(first.data + 6) == HOST_CORE_CPS2);
    CHECK(host_read_le16(first.data + 8) == 64 && host_read_le16(first.data + 10) == 40);
    CHECK(host_read_le16(first.data + 12) == 16 && host_read_le16(first.data + 14) == 20);
    CHECK(host_read_le16(first.data + 16) == 16);
    CHECK(host_read_le32(first.data + 20) == 286 && host_read_le32(first.data + 24) == 1387
        && host_read_le32(first.data + 28) == 5382);
    CHECK(host_read_le32(first.data + 60) == first.size);
    CHECK(host_crc32(first.data + 64, first.size - 64) == host_read_le32(first.data + 56));
    CHECK(path_join(rominfo_path, sizeof(rominfo_path), source_dir, "resources/cps2/rominfo.cps2"));
    CHECK(host_read_file(rominfo_path, &rominfo_bytes));
    CHECK(first.size < rominfo_bytes.size);

    saved_metadata_count = metadata.count;
    metadata.count--;
    CHECK(!host_game_database_build(&metadata, &rominfo, &second, &stats, error, sizeof(error)));
    CHECK(strstr(error, "identity divergence") != NULL);
    metadata.count = saved_metadata_count;

    const_game = host_rominfo_find(&rominfo, metadata.records[0].name);
    CHECK(const_game != NULL && const_game->region_count > 0);
    game = (host_game_record_t *)const_game;
    saved_regions = game->regions;
    saved_region_count = game->region_count;
    duplicate_regions[0] = saved_regions[0];
    duplicate_regions[1] = saved_regions[0];
    game->regions = duplicate_regions;
    game->region_count = 2;
    CHECK(!host_game_database_build(&metadata, &rominfo, &second, &stats, error, sizeof(error)));
    CHECK(strstr(error, "duplicate CPS2 region") != NULL);
    game->regions = saved_regions;
    game->region_count = saved_region_count;

    for (saved_region_count = 0; saved_region_count < game->region_count; ++saved_region_count) {
        if (strcmp(game->regions[saved_region_count].name, "CPU1") == 0)
            break;
    }
    CHECK(saved_region_count < game->region_count);
    saved_rom_count = game->regions[saved_region_count].rom_count;
    game->regions[saved_region_count].rom_count = 9;
    CHECK(!host_game_database_build(&metadata, &rominfo, &second, &stats, error, sizeof(error)));
    CHECK(strstr(error, "runtime limit") != NULL);
    game->regions[saved_region_count].rom_count = saved_rom_count;

    host_buffer_free(&rominfo_bytes);
    host_buffer_free(&second);
    host_buffer_free(&first);
    host_rominfo_free(&rominfo);
    host_game_metadata_free(&metadata);
    return 1;
}

static int test_dip(const char *source_dir, const char *scratch_dir)
{
    static const struct {
        const char *relative;
        size_t profiles;
        size_t rows;
        size_t bytes;
    } cases[] = {
        {"metadata/cps1_dips.json", 33, 1844, 60783},
        {"metadata/mvs_dips.json", 4, 152, 4524},
    };
    const char *valid =
        "{\"version\":1,\"languages\":[\"en\",\"ja\",\"zh-Hans\",\"zh-Hant\"],\"profiles\":{\"default\":{"
        "\"en\":[{\"label\":\"Mode\",\"enable\":1,\"mask\":3,\"value_max\":1,\"values\":[\"Off\",\"On\"]},{\"label\":\"\\u0000\",\"enable\":0,\"mask\":0,\"value_max\":0,\"values\":[]}],"
        "\"ja\":[{\"label\":\"Mode\",\"enable\":1,\"mask\":3,\"value_max\":1,\"values\":[\"Off\",\"On\"]},{\"label\":\"\\u0000\",\"enable\":0,\"mask\":0,\"value_max\":0,\"values\":[]}],"
        "\"zh-Hans\":[{\"label\":\"Mode\",\"enable\":1,\"mask\":3,\"value_max\":1,\"values\":[\"Off\",\"On\"]},{\"label\":\"\\u0000\",\"enable\":0,\"mask\":0,\"value_max\":0,\"values\":[]}],"
        "\"zh-Hant\":[{\"label\":\"Mode\",\"enable\":1,\"mask\":3,\"value_max\":1,\"values\":[\"Off\",\"On\"]},{\"label\":\"\\u0000\",\"enable\":0,\"mask\":0,\"value_max\":0,\"values\":[]}]}}}";
    const char *drift =
        "{\"version\":1,\"languages\":[\"en\",\"ja\",\"zh-Hans\",\"zh-Hant\"],\"profiles\":{\"default\":{"
        "\"en\":[{\"label\":\"Mode\",\"enable\":1,\"mask\":3,\"value_max\":0,\"values\":[\"Off\"]},{\"label\":\"\\u0000\",\"enable\":0,\"mask\":0,\"value_max\":0,\"values\":[]}],"
        "\"ja\":[{\"label\":\"Mode\",\"enable\":1,\"mask\":2,\"value_max\":0,\"values\":[\"Off\"]},{\"label\":\"\\u0000\",\"enable\":0,\"mask\":0,\"value_max\":0,\"values\":[]}],"
        "\"zh-Hans\":[{\"label\":\"Mode\",\"enable\":1,\"mask\":3,\"value_max\":0,\"values\":[\"Off\"]},{\"label\":\"\\u0000\",\"enable\":0,\"mask\":0,\"value_max\":0,\"values\":[]}],"
        "\"zh-Hant\":[{\"label\":\"Mode\",\"enable\":1,\"mask\":3,\"value_max\":0,\"values\":[\"Off\"]},{\"label\":\"\\u0000\",\"enable\":0,\"mask\":0,\"value_max\":0,\"values\":[]}]}}}";
    const char *bad_range =
        "{\"version\":1,\"languages\":[\"en\",\"ja\",\"zh-Hans\",\"zh-Hant\"],\"profiles\":{\"default\":{"
        "\"en\":[{\"label\":\"Mode\",\"enable\":1,\"mask\":3,\"value_max\":0,\"values\":[\"Off\",\"On\"]},{\"label\":\"\\u0000\",\"enable\":0,\"mask\":0,\"value_max\":0,\"values\":[]}],"
        "\"ja\":[{\"label\":\"Mode\",\"enable\":1,\"mask\":3,\"value_max\":1,\"values\":[\"Off\",\"On\"]},{\"label\":\"\\u0000\",\"enable\":0,\"mask\":0,\"value_max\":0,\"values\":[]}],"
        "\"zh-Hans\":[{\"label\":\"Mode\",\"enable\":1,\"mask\":3,\"value_max\":1,\"values\":[\"Off\",\"On\"]},{\"label\":\"\\u0000\",\"enable\":0,\"mask\":0,\"value_max\":0,\"values\":[]}],"
        "\"zh-Hant\":[{\"label\":\"Mode\",\"enable\":1,\"mask\":3,\"value_max\":1,\"values\":[\"Off\",\"On\"]},{\"label\":\"\\u0000\",\"enable\":0,\"mask\":0,\"value_max\":0,\"values\":[]}]}}}";
    char path[1024];
    char error[1024];
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        host_buffer_t first;
        host_buffer_t second;
        host_dip_metadata_stats_t stats;
        host_buffer_init(&first);
        host_buffer_init(&second);
        CHECK(path_join(path, sizeof(path), source_dir, cases[i].relative));
        CHECK(host_dip_metadata_build(path, &first, &stats, error, sizeof(error)));
        CHECK(stats.profiles == cases[i].profiles && stats.localized_rows == cases[i].rows);
        CHECK(first.size == cases[i].bytes && memcmp(first.data, "NJDP", 4) == 0);
        CHECK(host_dip_metadata_build(path, &second, &stats, error, sizeof(error)));
        CHECK(buffer_equal(&first, &second));
        host_buffer_free(&second);
        host_buffer_free(&first);
    }

    CHECK(path_join(path, sizeof(path), scratch_dir, "valid-dip.json"));
    CHECK(write_text(path, valid));
    {
        host_buffer_t blob;
        host_dip_metadata_stats_t stats;
        host_buffer_init(&blob);
        CHECK(host_dip_metadata_build(path, &blob, &stats, error, sizeof(error)));
        CHECK(stats.profiles == 1 && stats.localized_rows == 8);
        host_buffer_free(&blob);
    }
    CHECK(path_join(path, sizeof(path), scratch_dir, "drift-dip.json"));
    CHECK(write_text(path, drift));
    {
        host_buffer_t blob;
        host_dip_metadata_stats_t stats;
        host_buffer_init(&blob);
        CHECK(!host_dip_metadata_build(path, &blob, &stats, error, sizeof(error)));
        CHECK(strstr(error, "structural") != NULL || strstr(error, "differ") != NULL);
        host_buffer_free(&blob);
    }
    CHECK(path_join(path, sizeof(path), scratch_dir, "range-dip.json"));
    CHECK(write_text(path, bad_range));
    {
        host_buffer_t blob;
        host_dip_metadata_stats_t stats;
        host_buffer_init(&blob);
        CHECK(!host_dip_metadata_build(path, &blob, &stats, error, sizeof(error)));
        CHECK(strstr(error, "value_max") != NULL);
        host_buffer_free(&blob);
    }
    return 1;
}

static int test_translation_source_fail(const char *scratch_dir, const char *catalog_text,
    const char *expected_error)
{
    host_translation_manifest_t manifest;
    host_translation_catalog_t catalog;
    char manifest_path[1024];
    char catalog_path[1024];
    char error[1024];
    int loaded;
    host_translation_manifest_init(&manifest);
    host_translation_catalog_init(&catalog);
    CHECK(path_join(manifest_path, sizeof(manifest_path), scratch_dir, "messages.def"));
    CHECK(path_join(catalog_path, sizeof(catalog_path), scratch_dir, "bad.lang"));
    CHECK(write_text(manifest_path, "UI_TEXT_ID(A, 0)\nUI_TEXT_ID(B, 1)\n"));
    CHECK(write_text(catalog_path, catalog_text));
    CHECK(host_translation_load_manifest(manifest_path, "UI_TEXT_ID", &manifest, error, sizeof(error)));
    loaded = host_translation_load_catalog(catalog_path, &manifest, &catalog, error, sizeof(error));
    CHECK(!loaded);
    CHECK(strstr(error, expected_error) != NULL);
    host_translation_catalog_free(&catalog);
    host_translation_manifest_free(&manifest);
    return 1;
}

static int find_unicode_entry(const host_unicode_glyph_t *entries, size_t count, uint32_t codepoint)
{
    size_t i;
    for (i = 0; i < count; ++i) {
        if (entries[i].codepoint == codepoint)
            return 1;
    }
    return 0;
}

static int test_translations(const char *source_dir, const char *scratch_dir)
{
    static const char *language_files[] = {"en.lang", "ja.lang", "es.lang", "zh-Hans.lang", "zh-Hant.lang"};
    host_translation_manifest_t manifest;
    host_translation_catalog_t catalogs[5];
    host_unicode_glyph_t *glyphs = NULL;
    size_t glyph_count = 0;
    host_buffer_t first;
    host_buffer_t second;
    char path[1024];
    char error[1024];
    size_t i;

    host_translation_manifest_init(&manifest);
    for (i = 0; i < 5; ++i)
        host_translation_catalog_init(&catalogs[i]);
    host_buffer_init(&first);
    host_buffer_init(&second);
    CHECK(path_join(path, sizeof(path), source_dir, "translations/messages.def"));
    CHECK(host_translation_load_manifest(path, "UI_TEXT_ID", &manifest, error, sizeof(error)));
    CHECK(manifest.count == 409);
    CHECK(host_translation_schema_hash(&manifest) == 0x81443cabu);
    for (i = 0; i < 5; ++i) {
        char relative[256];
        snprintf(relative, sizeof(relative), "translations/%s", language_files[i]);
        CHECK(path_join(path, sizeof(path), source_dir, relative));
        CHECK(host_translation_load_catalog(path, &manifest, &catalogs[i], error, sizeof(error)));
        CHECK(host_translation_validate_printf(&manifest, &catalogs[0], &catalogs[i],
            language_files[i], error, sizeof(error)));
        CHECK(host_translation_build_pack((unsigned)i, &manifest, &catalogs[i], &first,
            error, sizeof(error)));
        CHECK(host_translation_build_pack((unsigned)i, &manifest, &catalogs[i], &second,
            error, sizeof(error)));
        CHECK(buffer_equal(&first, &second));
        CHECK(host_translation_validate_pack(&first, (unsigned)i, &manifest, &catalogs[i],
            error, sizeof(error)));
        host_buffer_free(&first);
        host_buffer_free(&second);
        host_buffer_init(&first);
        host_buffer_init(&second);
    }
    CHECK(host_translation_collect_unicode(catalogs, 5, NULL, 0, &glyphs, &glyph_count,
        error, sizeof(error)));
    CHECK(find_unicode_entry(glyphs, glyph_count, 0x3057u));
    CHECK(find_unicode_entry(glyphs, glyph_count, 0x8bf7u));
    CHECK(!find_unicode_entry(glyphs, glyph_count, 0x00b7u));
    free(glyphs);
    glyphs = NULL;

    CHECK(test_translation_source_fail(scratch_dir, "A=A\n", "missing"));
    CHECK(test_translation_source_fail(scratch_dir, "A=A\nB=B\nEXTRA=x\n", "unknown key"));
    CHECK(test_translation_source_fail(scratch_dir, "A=A\nA=again\nB=B\n", "duplicate key"));
    CHECK(test_translation_source_fail(scratch_dir, "B=B\nA=A\n", "order"));
    CHECK(test_translation_source_fail(scratch_dir, "A=bad\\q\nB=B\n", "unsupported escape"));

    {
        host_translation_manifest_t small_manifest;
        host_translation_catalog_t english;
        host_translation_catalog_t candidate;
        char manifest_path[1024];
        char en_path[1024];
        char candidate_path[1024];
        host_buffer_t pack;
        host_buffer_t original;
        uint8_t invalid_utf8[] = {'A', '=', 'b', 'a', 'd', 0xff, '\n', 'B', '=', 'B', '\n'};
        uint8_t *large_value;
        size_t header_size = 20;

        host_translation_manifest_init(&small_manifest);
        host_translation_catalog_init(&english);
        host_translation_catalog_init(&candidate);
        host_buffer_init(&pack);
        host_buffer_init(&original);
        CHECK(path_join(manifest_path, sizeof(manifest_path), scratch_dir, "small.def"));
        CHECK(path_join(en_path, sizeof(en_path), scratch_dir, "small-en.lang"));
        CHECK(path_join(candidate_path, sizeof(candidate_path), scratch_dir, "small-other.lang"));
        CHECK(write_text(manifest_path, "UI_TEXT_ID(A, 0)\nUI_TEXT_ID(B, 1)\n"));
        CHECK(write_text(en_path, "A=value=%s\nB=<NULL>\n"));
        CHECK(host_translation_load_manifest(manifest_path, "UI_TEXT_ID", &small_manifest, error, sizeof(error)));
        CHECK(host_translation_load_catalog(en_path, &small_manifest, &english, error, sizeof(error)));
        CHECK(write_text(candidate_path, "A=value=%d\nB=<NULL>\n"));
        CHECK(host_translation_load_catalog(candidate_path, &small_manifest, &candidate, error, sizeof(error)));
        CHECK(!host_translation_validate_printf(&small_manifest, &english, &candidate, "test", error, sizeof(error)));
        CHECK(strstr(error, "does not match") != NULL);
        host_translation_catalog_free(&candidate);
        CHECK(host_write_file(candidate_path, invalid_utf8, sizeof(invalid_utf8)));
        CHECK(!host_translation_load_catalog(candidate_path, &small_manifest, &candidate, error, sizeof(error)));
        CHECK(strstr(error, "valid UTF-8") != NULL);

        CHECK(write_text(candidate_path, "A=<CIRCLE> OK\nB=<NULL>\n"));
        CHECK(host_translation_load_catalog(candidate_path, &small_manifest, &candidate, error, sizeof(error)));
        CHECK(candidate.values[0].size >= 3 && candidate.values[0].data[0] == 0xee
            && candidate.values[0].data[1] == 0x80 && candidate.values[0].data[2] == 0x84);
        host_translation_catalog_free(&candidate);

        CHECK(write_text(candidate_path, "A=emoji 😀\nB=<NULL>\n"));
        CHECK(host_translation_load_catalog(candidate_path, &small_manifest, &candidate, error, sizeof(error)));
        CHECK(!host_translation_collect_unicode(&candidate, 1, NULL, 0, &glyphs, &glyph_count,
            error, sizeof(error)));
        CHECK(strstr(error, "font is missing") != NULL);
        host_translation_catalog_free(&candidate);

        CHECK(write_text(candidate_path, "A=Español · acción\nB=<NULL>\n"));
        CHECK(host_translation_load_catalog(candidate_path, &small_manifest, &candidate, error, sizeof(error)));
        CHECK(host_translation_collect_unicode(&candidate, 1, NULL, 0, &glyphs, &glyph_count,
            error, sizeof(error)));
        CHECK(glyph_count == 0);
        free(glyphs);
        glyphs = NULL;
        host_translation_catalog_free(&candidate);

        CHECK(host_translation_build_pack(0, &small_manifest, &english, &pack, error, sizeof(error)));
        CHECK(host_buffer_append(&original, pack.data, pack.size));
#define RESTORE_PACK() do { \
            host_buffer_free(&pack); host_buffer_init(&pack); \
            CHECK(host_buffer_append(&pack, original.data, original.size)); \
        } while (0)
        pack.data[0] = 'B';
        CHECK(!host_translation_validate_pack(&pack, 0, &small_manifest, &english, error, sizeof(error)));
        RESTORE_PACK();
        pack.data[4] = 3;
        CHECK(!host_translation_validate_pack(&pack, 0, &small_manifest, &english, error, sizeof(error)));
        RESTORE_PACK();
        pack.data[6] = 1;
        CHECK(!host_translation_validate_pack(&pack, 0, &small_manifest, &english, error, sizeof(error)));
        RESTORE_PACK();
        pack.data[8] = 1;
        CHECK(!host_translation_validate_pack(&pack, 0, &small_manifest, &english, error, sizeof(error)));
        RESTORE_PACK();
        pack.data[10] = 1;
        CHECK(!host_translation_validate_pack(&pack, 0, &small_manifest, &english, error, sizeof(error)));
        RESTORE_PACK();
        pack.data[16] ^= 1;
        CHECK(!host_translation_validate_pack(&pack, 0, &small_manifest, &english, error, sizeof(error)));
        RESTORE_PACK();
        pack.data[header_size] = 0xff;
        pack.data[header_size + 1] = 0x7f;
        CHECK(!host_translation_validate_pack(&pack, 0, &small_manifest, &english, error, sizeof(error)));
        RESTORE_PACK();
        pack.size--;
        CHECK(!host_translation_validate_pack(&pack, 0, &small_manifest, &english, error, sizeof(error)));
        RESTORE_PACK();
        if (pack.size > header_size + 4) {
            pack.data[header_size + 4] = 0xff;
            CHECK(!host_translation_validate_pack(&pack, 0, &small_manifest, &english, error, sizeof(error)));
        }
#undef RESTORE_PACK

        large_value = (uint8_t *)malloc(65534u);
        CHECK(large_value != NULL);
        memset(large_value, 'x', 65534u);
        free(english.values[0].data);
        english.values[0].data = large_value;
        english.values[0].size = 65534u;
        CHECK(!host_translation_build_pack(0, &small_manifest, &english, &pack, error, sizeof(error)));
        CHECK(strstr(error, "allows at most") != NULL || strstr(error, "limit") != NULL);

        host_buffer_free(&original);
        host_buffer_free(&pack);
        host_translation_catalog_free(&candidate);
        host_translation_catalog_free(&english);
        host_translation_manifest_free(&small_manifest);
    }

    {
        host_translation_manifest_t rom_manifest;
        host_translation_catalog_t rom_en;
        host_translation_catalog_t rom_zh;
        host_buffer_t rendered_a;
        host_buffer_t rendered_b;
        host_translation_manifest_init(&rom_manifest);
        host_translation_catalog_init(&rom_en);
        host_translation_catalog_init(&rom_zh);
        host_buffer_init(&rendered_a);
        host_buffer_init(&rendered_b);
        CHECK(path_join(path, sizeof(path), source_dir, "translations/romcnv/messages.def"));
        CHECK(host_translation_load_manifest(path, "ROMCNV_TEXT_ID", &rom_manifest, error, sizeof(error)));
        CHECK(rom_manifest.count == 45);
        CHECK(path_join(path, sizeof(path), source_dir, "translations/romcnv/en.lang"));
        CHECK(host_translation_load_catalog(path, &rom_manifest, &rom_en, error, sizeof(error)));
        CHECK(path_join(path, sizeof(path), source_dir, "translations/romcnv/zh-Hans.lang"));
        CHECK(host_translation_load_catalog(path, &rom_manifest, &rom_zh, error, sizeof(error)));
        CHECK(host_translation_validate_printf(&rom_manifest, &rom_en, &rom_zh, "zh-Hans", error, sizeof(error)));
        CHECK(host_romcnv_render_translation_include(&rom_manifest, &rom_en, &rom_zh, &rendered_a));
        CHECK(host_romcnv_render_translation_include(&rom_manifest, &rom_en, &rom_zh, &rendered_b));
        CHECK(buffer_equal(&rendered_a, &rendered_b));
        CHECK(buffer_contains(&rendered_a, "romcnv_catalog_en"));
        host_buffer_free(&rendered_b);
        host_buffer_free(&rendered_a);
        host_translation_catalog_free(&rom_zh);
        host_translation_catalog_free(&rom_en);
        host_translation_manifest_free(&rom_manifest);
    }

    host_buffer_free(&second);
    host_buffer_free(&first);
    for (i = 0; i < 5; ++i)
        host_translation_catalog_free(&catalogs[i]);
    host_translation_manifest_free(&manifest);
    return 1;
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s --source-dir DIR --scratch-dir DIR --suite SUITE\n", program);
}

int main(int argc, char **argv)
{
    const char *source_dir = NULL;
    const char *scratch_dir = NULL;
    const char *suite = NULL;
    int i;
    int ok = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--source-dir") == 0 && i + 1 < argc)
            source_dir = argv[++i];
        else if (strcmp(argv[i], "--scratch-dir") == 0 && i + 1 < argc)
            scratch_dir = argv[++i];
        else if (strcmp(argv[i], "--suite") == 0 && i + 1 < argc)
            suite = argv[++i];
        else {
            usage(argv[0]);
            return 2;
        }
    }
    if (source_dir == NULL || scratch_dir == NULL || suite == NULL) {
        usage(argv[0]);
        return 2;
    }
    if (!host_make_directories(scratch_dir)) {
        fprintf(stderr, "could not create scratch directory: %s\n", scratch_dir);
        return 1;
    }

    if (strcmp(suite, "metadata") == 0)
        ok = test_metadata(source_dir, scratch_dir);
    else if (strcmp(suite, "rominfo") == 0)
        ok = test_rominfo(source_dir, scratch_dir);
    else if (strcmp(suite, "game-database") == 0)
        ok = test_game_database(source_dir, scratch_dir);
    else if (strcmp(suite, "dip") == 0)
        ok = test_dip(source_dir, scratch_dir);
    else if (strcmp(suite, "translations") == 0)
        ok = test_translations(source_dir, scratch_dir);
    else {
        fprintf(stderr, "unknown host test suite: %s\n", suite);
        return 2;
    }
    if (!ok)
        return 1;
    printf("PASS: host-tool %s suite\n", suite);
    return 0;
}
