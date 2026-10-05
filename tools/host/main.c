#include "commands.h"

#include <stdio.h>
#include <string.h>

typedef int (*command_fn_t)(int argc, char **argv);

typedef struct command_entry {
    const char *name;
    command_fn_t function;
    const char *summary;
} command_entry_t;

static const command_entry_t commands[] = {
    {"dip-metadata", command_dip_metadata, "validate/build localized DIP metadata"},
    {"font", command_font, "validate/build the external GBK font asset"},
    {"game-database", command_game_database, "validate/build the unified CPS2 game database"},
    {"game-metadata", command_game_metadata, "validate/build runtime game metadata"},
    {"rominfo-validate", command_rominfo_validate, "validate textual rominfo topology"},
    {"validate-cps2-cache", command_validate_cps2_cache, "validate CPS2 converter cache layouts"},
    {"compare-frames", command_compare_frames, "compare Desktop PPM frame dumps"},
};

static void print_usage(const char *program)
{
    size_t i;

    fprintf(stderr, "usage: %s <command> [options]\n\ncommands:\n", program);
    for (i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i)
        fprintf(stderr, "  %-20s %s\n", commands[i].name, commands[i].summary);
}

int main(int argc, char **argv)
{
    size_t i;

    if (argc < 2) {
        print_usage(argv[0]);
        return 2;
    }
    for (i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        if (strcmp(argv[1], commands[i].name) == 0)
            return commands[i].function(argc - 1, argv + 1);
    }
    fprintf(stderr, "%s: unknown command %s\n", argv[0], argv[1]);
    print_usage(argv[0]);
    return 2;
}
