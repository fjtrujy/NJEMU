#include "commands.h"

#include "file.h"
#include "translation_source.h"
#include "translations.h"

#include <stdio.h>
#include <string.h>

static int join_path(char *output, size_t output_size, const char *directory, const char *name)
{
    size_t length = strlen(directory);
    const char *separator = length != 0 && (directory[length - 1] == '/' || directory[length - 1] == '\\') ? "" : "/";
    int written = snprintf(output, output_size, "%s%s%s", directory, separator, name);
    return written >= 0 && (size_t)written < output_size;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: njemu-tool romcnv-translations [--translations-dir DIR] [--output FILE]\n");
}

int command_romcnv_translations(int argc, char **argv)
{
    const char *translations_dir = "translations/romcnv";
    const char *output_path = NULL;
    host_translation_manifest_t manifest;
    host_translation_catalog_t english;
    host_translation_catalog_t zh_hans;
    host_buffer_t generated;
    char path[1024];
    char error[1024];
    int arg;
    int result = 1;

    host_translation_manifest_init(&manifest);
    host_translation_catalog_init(&english);
    host_translation_catalog_init(&zh_hans);
    host_buffer_init(&generated);

    for (arg = 1; arg < argc; ++arg) {
        if (strcmp(argv[arg], "--translations-dir") == 0 && arg + 1 < argc)
            translations_dir = argv[++arg];
        else if (strcmp(argv[arg], "--output") == 0 && arg + 1 < argc)
            output_path = argv[++arg];
        else {
            usage();
            result = 2;
            goto out;
        }
    }

    if (!join_path(path, sizeof(path), translations_dir, "messages.def")
        || !host_translation_load_manifest(path, "ROMCNV_TEXT_ID", &manifest,
            error, sizeof(error))) {
        fprintf(stderr, "ROMCNV translation validation failed: %s\n", error);
        goto out;
    }
    if (!join_path(path, sizeof(path), translations_dir, "en.lang")
        || !host_translation_load_catalog(path, &manifest, &english, error, sizeof(error))) {
        fprintf(stderr, "ROMCNV translation validation failed: %s\n", error);
        goto out;
    }
    if (!join_path(path, sizeof(path), translations_dir, "zh-Hans.lang")
        || !host_translation_load_catalog(path, &manifest, &zh_hans, error, sizeof(error))) {
        fprintf(stderr, "ROMCNV translation validation failed: %s\n", error);
        goto out;
    }
    if (!host_translation_validate_printf(&manifest, &english, &english, "en",
            error, sizeof(error))
        || !host_translation_validate_printf(&manifest, &english, &zh_hans, "zh-Hans",
            error, sizeof(error))) {
        fprintf(stderr, "ROMCNV translation validation failed: %s\n", error);
        goto out;
    }
    if (!host_romcnv_render_translation_include(&manifest, &english, &zh_hans, &generated)) {
        fprintf(stderr, "ROMCNV translation validation failed: out of memory\n");
        goto out;
    }
    if (output_path != NULL) {
        if (!host_make_parent_directories(output_path)
            || !host_write_file_if_different(output_path, generated.data, generated.size)) {
            fprintf(stderr, "ROMCNV translation validation failed: could not write %s\n",
                output_path);
            goto out;
        }
    }
    printf("validated %lu ROMCNV keys in 2 languages; en=%lu B, zh-Hans=%lu B\n",
        (unsigned long)manifest.count,
        (unsigned long)english.total_bytes,
        (unsigned long)zh_hans.total_bytes);
    result = 0;
out:
    host_buffer_free(&generated);
    host_translation_catalog_free(&zh_hans);
    host_translation_catalog_free(&english);
    host_translation_manifest_free(&manifest);
    return result;
}
