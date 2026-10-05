#include "commands.h"

#include "file.h"
#include "translation_source.h"
#include "translations.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *language_names[] = {"en", "ja", "es", "zh-Hans", "zh-Hant"};
static const char *language_files[] = {"en.lang", "ja.lang", "es.lang", "zh-Hans.lang", "zh-Hant.lang"};

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
        "usage: njemu-tool translations [--translations-dir DIR] [--build] "
        "[--output-dir DIR] [--unicode-map-output FILE] [--unicode-source FILE ...]\n");
}

int command_translations(int argc, char **argv)
{
    const char *translations_dir = "translations";
    const char *output_dir = "build/translations/lang";
    const char *unicode_output = NULL;
    const char **unicode_sources = NULL;
    size_t unicode_source_count = 0;
    int build = 0;
    host_translation_manifest_t manifest;
    host_translation_catalog_t catalogs[5];
    host_unicode_glyph_t *glyphs = NULL;
    size_t glyph_count = 0;
    size_t pack_sizes[5] = {0, 0, 0, 0, 0};
    char path[1024];
    char error[1024];
    int arg;
    size_t i;
    int result = 1;

    host_translation_manifest_init(&manifest);
    for (i = 0; i < 5; ++i)
        host_translation_catalog_init(&catalogs[i]);

    for (arg = 1; arg < argc; ++arg) {
        if (strcmp(argv[arg], "--translations-dir") == 0 && arg + 1 < argc)
            translations_dir = argv[++arg];
        else if (strcmp(argv[arg], "--build") == 0)
            build = 1;
        else if (strcmp(argv[arg], "--output-dir") == 0 && arg + 1 < argc)
            output_dir = argv[++arg];
        else if (strcmp(argv[arg], "--unicode-map-output") == 0 && arg + 1 < argc)
            unicode_output = argv[++arg];
        else if (strcmp(argv[arg], "--unicode-source") == 0 && arg + 1 < argc) {
            const char **next = (const char **)realloc(unicode_sources,
                (unicode_source_count + 1) * sizeof(*unicode_sources));
            if (next == NULL) {
                fprintf(stderr, "translation validation failed: out of memory\n");
                goto out;
            }
            unicode_sources = next;
            unicode_sources[unicode_source_count++] = argv[++arg];
        } else {
            usage();
            result = 2;
            goto out;
        }
    }

    if (!join_path(path, sizeof(path), translations_dir, "messages.def")
        || !host_translation_load_manifest(path, "UI_TEXT_ID", &manifest, error, sizeof(error))) {
        fprintf(stderr, "translation validation failed: %s\n", error);
        goto out;
    }
    for (i = 0; i < 5; ++i) {
        if (!join_path(path, sizeof(path), translations_dir, language_files[i])) {
            fprintf(stderr, "translation validation failed: path is too long\n");
            goto out;
        }
        if (!host_translation_load_catalog(path, &manifest, &catalogs[i], error, sizeof(error))) {
            fprintf(stderr, "translation validation failed: %s\n", error);
            goto out;
        }
        if (!host_translation_validate_printf(&manifest, &catalogs[0], &catalogs[i],
                language_names[i], error, sizeof(error))) {
            fprintf(stderr, "translation validation failed: %s\n", error);
            goto out;
        }
    }

    if (!host_translation_collect_unicode(catalogs, 5, unicode_sources, unicode_source_count,
            &glyphs, &glyph_count, error, sizeof(error))) {
        fprintf(stderr, "translation validation failed: %s\n", error);
        goto out;
    }

    if (build) {
        if (!host_make_directories(output_dir)) {
            fprintf(stderr, "translation validation failed: could not create %s\n", output_dir);
            goto out;
        }
        for (i = 0; i < 5; ++i) {
            host_buffer_t pack;
            char filename[64];
            host_buffer_init(&pack);
            snprintf(filename, sizeof(filename), "%s.lng", language_names[i]);
            if (!join_path(path, sizeof(path), output_dir, filename)
                || !host_translation_build_pack((unsigned)i, &manifest, &catalogs[i],
                    &pack, error, sizeof(error))
                || !host_translation_validate_pack(&pack, (unsigned)i, &manifest, &catalogs[i],
                    error, sizeof(error))) {
                fprintf(stderr, "translation validation failed: %s\n", error);
                host_buffer_free(&pack);
                goto out;
            }
            pack_sizes[i] = pack.size;
            if (!host_write_file_if_different(path, pack.data, pack.size)) {
                fprintf(stderr, "translation validation failed: could not write %s\n", path);
                host_buffer_free(&pack);
                goto out;
            }
            host_buffer_free(&pack);
        }
    }

    if (unicode_output != NULL) {
        host_buffer_t source;
        host_buffer_init(&source);
        if (!host_translation_render_unicode_source(glyphs, glyph_count, &source)
            || !host_make_parent_directories(unicode_output)
            || !host_write_file_if_different(unicode_output, source.data, source.size)) {
            fprintf(stderr, "translation validation failed: could not write %s\n", unicode_output);
            host_buffer_free(&source);
            goto out;
        }
        host_buffer_free(&source);
    }

    printf("validated %lu keys in 5 languages; en=%lu B, ja=%lu B, es=%lu B, zh-Hans=%lu B, zh-Hant=%lu B\n",
        (unsigned long)manifest.count,
        (unsigned long)catalogs[0].total_bytes,
        (unsigned long)catalogs[1].total_bytes,
        (unsigned long)catalogs[2].total_bytes,
        (unsigned long)catalogs[3].total_bytes,
        (unsigned long)catalogs[4].total_bytes);
    if (build) {
        printf("generated .lng V2 packs in %s: en=%lu B, ja=%lu B, es=%lu B, zh-Hans=%lu B, zh-Hant=%lu B\n",
            output_dir,
            (unsigned long)pack_sizes[0], (unsigned long)pack_sizes[1],
            (unsigned long)pack_sizes[2], (unsigned long)pack_sizes[3],
            (unsigned long)pack_sizes[4]);
    }
    if (unicode_output != NULL) {
        printf("generated Unicode glyph lookup with %lu entries at %s\n",
            (unsigned long)glyph_count, unicode_output);
    }
    result = 0;
out:
    free(glyphs);
    free(unicode_sources);
    for (i = 0; i < 5; ++i)
        host_translation_catalog_free(&catalogs[i]);
    host_translation_manifest_free(&manifest);
    return result;
}
