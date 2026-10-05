#include "commands.h"

#include "file.h"
#include "game_database.h"
#include "game_metadata.h"
#include "rominfo.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    fprintf(stderr,
        "usage: njemu-tool game-database --core cps2 --metadata FILE --rominfo FILE "
        "[--output FILE] [--gamelist-output FILE] [--validate-only]\n");
}

int command_game_database(int argc, char **argv)
{
    const char *core_name = NULL;
    const char *metadata_path = NULL;
    const char *rominfo_path = NULL;
    const char *output_path = NULL;
    const char *gamelist_path = NULL;
    int validate_only = 0;
    host_game_metadata_t metadata;
    host_rominfo_t rominfo;
    host_game_database_stats_t stats;
    host_buffer_t output;
    host_buffer_t gamelist;
    char error[512];
    int arg;
    int result = 1;

    for (arg = 1; arg < argc; ++arg) {
        if (strcmp(argv[arg], "--core") == 0 && arg + 1 < argc)
            core_name = argv[++arg];
        else if (strcmp(argv[arg], "--metadata") == 0 && arg + 1 < argc)
            metadata_path = argv[++arg];
        else if (strcmp(argv[arg], "--rominfo") == 0 && arg + 1 < argc)
            rominfo_path = argv[++arg];
        else if (strcmp(argv[arg], "--output") == 0 && arg + 1 < argc)
            output_path = argv[++arg];
        else if (strcmp(argv[arg], "--gamelist-output") == 0 && arg + 1 < argc)
            gamelist_path = argv[++arg];
        else if (strcmp(argv[arg], "--validate-only") == 0)
            validate_only = 1;
        else {
            usage();
            return 2;
        }
    }
    if (core_name == NULL || strcmp(core_name, "cps2") != 0
        || metadata_path == NULL || rominfo_path == NULL) {
        usage();
        return 2;
    }
    if (validate_only && (output_path != NULL || gamelist_path != NULL)) {
        fprintf(stderr, "game_database: --validate-only cannot be combined with output options\n");
        return 1;
    }
    if (!validate_only && output_path == NULL) {
        fprintf(stderr, "game_database: --output is required unless --validate-only is used\n");
        return 1;
    }

    host_game_metadata_init(&metadata);
    host_rominfo_init(&rominfo);
    host_buffer_init(&output);
    host_buffer_init(&gamelist);
    if (!host_game_metadata_load(metadata_path, HOST_CORE_CPS2, &metadata, error, sizeof(error))) {
        fprintf(stderr, "game_database: %s\n", error);
        goto out;
    }
    if (!host_rominfo_load(rominfo_path, &rominfo, error, sizeof(error))) {
        fprintf(stderr, "game_database: %s\n", error);
        goto out;
    }
    if (!host_rominfo_validate_parents(&rominfo, "cps2", error, sizeof(error))) {
        fprintf(stderr, "game_database: %s\n", error);
        goto out;
    }
    if (!host_game_metadata_validate(&metadata, &rominfo, error, sizeof(error))) {
        fprintf(stderr, "game_database: %s\n", error);
        goto out;
    }
    if (!host_game_database_build(&metadata, &rominfo, &output, &stats, error, sizeof(error))) {
        fprintf(stderr, "game_database: %s\n", error);
        goto out;
    }

    if (!validate_only) {
        if (!host_write_file(output_path, output.data, output.size)) {
            fprintf(stderr, "game_database: could not write %s\n", output_path);
            goto out;
        }
        printf("generated %s: core=cps2 games=%lu regions=%lu roms=%lu bytes=%lu\n",
            output_path, (unsigned long)stats.games, (unsigned long)stats.regions,
            (unsigned long)stats.roms, (unsigned long)output.size);
        if (gamelist_path != NULL) {
            if (!host_game_metadata_build_gamelist(&metadata, &gamelist, error, sizeof(error))) {
                fprintf(stderr, "game_database: %s\n", error);
                goto out;
            }
            if (!host_write_file(gamelist_path, gamelist.data, gamelist.size)) {
                fprintf(stderr, "game_database: could not write %s\n", gamelist_path);
                goto out;
            }
            printf("generated %s: core=cps2 games=%lu bytes=%lu\n",
                gamelist_path, (unsigned long)stats.games, (unsigned long)gamelist.size);
        }
    }
    result = 0;
out:
    host_buffer_free(&gamelist);
    host_buffer_free(&output);
    host_rominfo_free(&rominfo);
    host_game_metadata_free(&metadata);
    return result;
}
