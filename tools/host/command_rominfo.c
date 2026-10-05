#include "commands.h"

#include "rominfo.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    fprintf(stderr,
        "usage: njemu-tool rominfo-validate --source FILE [--root-parent NAME]\n");
}

int command_rominfo_validate(int argc, char **argv)
{
    const char *source = NULL;
    const char *root_parent = NULL;
    host_rominfo_t rominfo;
    char error[512];
    size_t regions = 0;
    size_t roms = 0;
    size_t i;
    int arg;

    for (arg = 1; arg < argc; ++arg) {
        if (strcmp(argv[arg], "--source") == 0 && arg + 1 < argc)
            source = argv[++arg];
        else if (strcmp(argv[arg], "--root-parent") == 0 && arg + 1 < argc)
            root_parent = argv[++arg];
        else {
            usage();
            return 2;
        }
    }
    if (source == NULL) {
        usage();
        return 2;
    }

    host_rominfo_init(&rominfo);
    if (!host_rominfo_load(source, &rominfo, error, sizeof(error))) {
        fprintf(stderr, "rominfo: %s\n", error);
        host_rominfo_free(&rominfo);
        return 1;
    }
    if (root_parent != NULL
        && !host_rominfo_validate_parents(&rominfo, root_parent, error, sizeof(error))) {
        fprintf(stderr, "rominfo: %s\n", error);
        host_rominfo_free(&rominfo);
        return 1;
    }
    for (i = 0; i < rominfo.game_count; ++i) {
        size_t region;
        regions += rominfo.games[i].region_count;
        for (region = 0; region < rominfo.games[i].region_count; ++region)
            roms += rominfo.games[i].regions[region].rom_count;
    }
    printf("validated rominfo: games=%lu regions=%lu roms=%lu\n",
        (unsigned long)rominfo.game_count, (unsigned long)regions, (unsigned long)roms);
    host_rominfo_free(&rominfo);
    return 0;
}
