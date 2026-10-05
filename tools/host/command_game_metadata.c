#include "commands.h"

#include "file.h"
#include "game_metadata.h"
#include "rominfo.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    fprintf(stderr,
        "usage: njemu-tool game-metadata --core CORE --source FILE [--rominfo FILE] "
        "[--output FILE] [--gamelist-output FILE] [--validate-only]\n");
}

int command_game_metadata(int argc, char **argv)
{
    const char *core_name = NULL;
    const char *source = NULL;
    const char *rominfo_path = NULL;
    const char *output_path = NULL;
    const char *gamelist_path = NULL;
    int validate_only = 0;
    host_game_core_t core;
    host_game_metadata_t metadata;
    host_rominfo_t rominfo;
    host_buffer_t output;
    host_buffer_t gamelist;
    char error[512];
    int have_rominfo = 0;
    int arg;
    int result = 1;

    for (arg = 1; arg < argc; ++arg) {
        if (strcmp(argv[arg], "--core") == 0 && arg + 1 < argc)
            core_name = argv[++arg];
        else if (strcmp(argv[arg], "--source") == 0 && arg + 1 < argc)
            source = argv[++arg];
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
    if (core_name == NULL || source == NULL || !host_game_core_from_name(core_name, &core)) {
        usage();
        return 2;
    }
    if (validate_only && (output_path != NULL || gamelist_path != NULL)) {
        fprintf(stderr, "game_metadata: --validate-only cannot be combined with output options\n");
        return 1;
    }
    if (!validate_only && output_path == NULL) {
        fprintf(stderr, "game_metadata: --output is required unless --validate-only is used\n");
        return 1;
    }
    if (core != HOST_CORE_NCDZ && rominfo_path == NULL) {
        fprintf(stderr, "game_metadata: %s: --rominfo is required\n", core_name);
        return 1;
    }

    host_game_metadata_init(&metadata);
    host_rominfo_init(&rominfo);
    host_buffer_init(&output);
    host_buffer_init(&gamelist);
    if (!host_game_metadata_load(source, core, &metadata, error, sizeof(error))) {
        fprintf(stderr, "game_metadata: %s\n", error);
        goto out;
    }
    if (rominfo_path != NULL) {
        if (!host_rominfo_load_filenames(rominfo_path, &rominfo, error, sizeof(error))) {
            fprintf(stderr, "game_metadata: %s\n", error);
            goto out;
        }
        have_rominfo = 1;
    }
    if (!host_game_metadata_validate(&metadata, have_rominfo ? &rominfo : NULL,
            error, sizeof(error))) {
        fprintf(stderr, "game_metadata: %s\n", error);
        goto out;
    }
    if (!host_game_metadata_build_blob(&metadata, &output, error, sizeof(error))) {
        fprintf(stderr, "game_metadata: %s\n", error);
        goto out;
    }
    if (!validate_only) {
        if (!host_write_file(output_path, output.data, output.size)) {
            fprintf(stderr, "game_metadata: could not write %s\n", output_path);
            goto out;
        }
        printf("generated %s: core=%s records=%lu record_size=52 bytes=%lu\n",
            output_path, core_name, (unsigned long)metadata.count, (unsigned long)output.size);
        if (gamelist_path != NULL) {
            if (!host_game_metadata_build_gamelist(&metadata, &gamelist, error, sizeof(error))) {
                fprintf(stderr, "game_metadata: %s\n", error);
                goto out;
            }
            if (!host_write_file(gamelist_path, gamelist.data, gamelist.size)) {
                fprintf(stderr, "game_metadata: could not write %s\n", gamelist_path);
                goto out;
            }
            printf("generated %s: core=%s records=%lu bytes=%lu\n",
                gamelist_path, core_name, (unsigned long)metadata.count,
                (unsigned long)gamelist.size);
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
