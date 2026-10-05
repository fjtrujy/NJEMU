#include "commands.h"

#include "dip_metadata.h"
#include "file.h"

#include <stdio.h>
#include <string.h>

static void usage(void)
{
    fprintf(stderr,
        "usage: njemu-tool dip-metadata --source FILE [--output FILE] [--validate-only]\n");
}

int command_dip_metadata(int argc, char **argv)
{
    const char *source = NULL;
    const char *output_path = NULL;
    int validate_only = 0;
    host_buffer_t output;
    host_dip_metadata_stats_t stats;
    char error[512];
    int arg;
    int result = 1;

    for (arg = 1; arg < argc; ++arg) {
        if (strcmp(argv[arg], "--source") == 0 && arg + 1 < argc)
            source = argv[++arg];
        else if (strcmp(argv[arg], "--output") == 0 && arg + 1 < argc)
            output_path = argv[++arg];
        else if (strcmp(argv[arg], "--validate-only") == 0)
            validate_only = 1;
        else {
            usage();
            return 2;
        }
    }
    if (source == NULL) {
        usage();
        return 2;
    }
    if (validate_only && output_path != NULL) {
        fprintf(stderr, "DIP metadata validation failed: --validate-only cannot be combined with --output\n");
        return 1;
    }
    if (!validate_only && output_path == NULL) {
        fprintf(stderr, "DIP metadata validation failed: --output is required unless --validate-only is used\n");
        return 1;
    }

    host_buffer_init(&output);
    if (!host_dip_metadata_build(source, &output, &stats, error, sizeof(error))) {
        fprintf(stderr, "DIP metadata validation failed: %s\n", error);
        goto out;
    }
    printf("validated DIP metadata: profiles=%lu localized_rows=%lu runtime_bytes=%lu\n",
        (unsigned long)stats.profiles, (unsigned long)stats.localized_rows,
        (unsigned long)output.size);
    if (!validate_only) {
        if (!host_write_file_if_different(output_path, output.data, output.size)) {
            fprintf(stderr, "DIP metadata validation failed: could not write %s\n", output_path);
            goto out;
        }
        printf("generated %s\n", output_path);
    }
    result = 0;
out:
    host_buffer_free(&output);
    return result;
}
