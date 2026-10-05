#ifndef NJEMU_HOST_TRANSLATIONS_H
#define NJEMU_HOST_TRANSLATIONS_H

#include "file.h"
#include "translation_source.h"

#include <stddef.h>
#include <stdint.h>

typedef struct host_unicode_glyph {
    uint32_t codepoint;
    uint16_t glyph;
} host_unicode_glyph_t;

uint32_t host_translation_schema_hash(const host_translation_manifest_t *manifest);
int host_translation_build_pack(unsigned language_id,
    const host_translation_manifest_t *manifest,
    const host_translation_catalog_t *catalog,
    host_buffer_t *output, char *error, size_t error_size);
int host_translation_validate_pack(const host_buffer_t *pack, unsigned language_id,
    const host_translation_manifest_t *manifest,
    const host_translation_catalog_t *catalog,
    char *error, size_t error_size);
int host_translation_collect_unicode(
    const host_translation_catalog_t *catalogs, size_t catalog_count,
    const char *const *extra_sources, size_t extra_source_count,
    host_unicode_glyph_t **entries, size_t *entry_count,
    char *error, size_t error_size);
int host_translation_render_unicode_source(const host_unicode_glyph_t *entries,
    size_t entry_count, host_buffer_t *output);
int host_romcnv_render_translation_include(
    const host_translation_manifest_t *manifest,
    const host_translation_catalog_t *english,
    const host_translation_catalog_t *zh_hans,
    host_buffer_t *output);

#endif
