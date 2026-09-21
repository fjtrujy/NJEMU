#ifndef COMMON_UI_UNICODE_GLYPH_H
#define COMMON_UI_UNICODE_GLYPH_H

#include <stdint.h>

typedef struct ui_unicode_glyph_entry
{
	uint16_t codepoint;
	uint16_t glyph;
} ui_unicode_glyph_entry_t;

int ui_unicode_glyph_lookup(uint32_t codepoint, uint16_t *glyph);

#endif /* COMMON_UI_UNICODE_GLYPH_H */
