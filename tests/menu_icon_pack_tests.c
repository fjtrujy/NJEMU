/* Host A/B integrity test for the experimental external category icon pack. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "common/ui_defs.h"
#include "common/icon/menu_extra.h"

int embedded_small_get(struct font_t *font, uint16_t code, int light);
int embedded_large_get(struct font_t *font, uint16_t code, int light);

int main(int argc, char **argv)
{
    if (argc != 2)
        return 2;
    assert(!menu_icon_small_get(&(struct font_t){0}, ICON_VIDEO, 0));
    assert(menu_icon_pack_load(argv[1]));
    for (int icon = ICON_VIDEO; icon <= ICON_TOOLS; ++icon) {
        for (int light = 0; light <= 1; ++light) {
            for (int large = 0; large <= 1; ++large) {
                struct font_t embedded, external;
                int length = large ? (light ? 640 : 384) : (light ? 416 : 216);
                int a = large ? embedded_large_get(&embedded, icon, light) :
                                embedded_small_get(&embedded, icon, light);
                int b = large ? menu_icon_large_get(&external, icon, light) :
                                menu_icon_small_get(&external, icon, light);
                assert(a && b);
                assert(embedded.width == external.width);
                assert(embedded.height == external.height);
                assert(embedded.pitch == external.pitch);
                assert(memcmp(embedded.data, external.data, (size_t)length) == 0);
            }
        }
    }
    menu_icon_pack_unload();
    assert(!menu_icon_large_get(&(struct font_t){0}, ICON_AUDIO, 0));
    puts("external icon pack matches all 16 embedded glyph variants");
    return 0;
}
