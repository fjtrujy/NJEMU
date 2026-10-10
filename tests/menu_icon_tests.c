/* Validate that all additional menu icons have both normal and highlighted
 * glyphs in the layout consumed by the common 4-bpp font renderer. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "common/font_t.h"
#include "common/icon/menu_extra.h"
#include "common/ui_defs.h"

static int has_visible_pixel(const struct font_t *glyph)
{
    int bytes = (glyph->width * glyph->height + 1) / 2;
    for (int i = 0; i < bytes; i++) {
        if (glyph->data[i] != 0)
            return 1;
    }
    return 0;
}

int main(void)
{
    struct font_t glyph;
    assert(ICON_VIDEO == 17);
    assert(ICON_AUDIO == 18);
    assert(ICON_PERFORMANCE == 19);
    assert(ICON_TOOLS == 20);

    for (int code = ICON_VIDEO; code <= ICON_TOOLS; code++) {
        for (int light = 0; light <= 1; light++) {
            assert(menu_icon_small_get(&glyph, (uint16_t)code, light));
            assert(glyph.width == (light ? 32 : 24));
            assert(glyph.height == (light ? 26 : 18));
            assert(glyph.pitch == 32);
            assert(has_visible_pixel(&glyph));

            assert(menu_icon_large_get(&glyph, (uint16_t)code, light));
            assert(glyph.width == (light ? 40 : 32));
            assert(glyph.height == (light ? 32 : 24));
            assert(glyph.pitch == 32);
            assert(has_visible_pixel(&glyph));
        }
    }
    assert(!menu_icon_small_get(&glyph, ICON_CONFIG, 0));
    assert(!menu_icon_large_get(&glyph, ICON_COLORCFG, 1));
    assert(!menu_icon_large_get(&glyph, MAX_ICONS, 0));
    puts("Menu icon coverage: OK");
    return 0;
}
