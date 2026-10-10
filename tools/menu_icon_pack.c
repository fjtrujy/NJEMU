/* Host-only pack generator for the experimental external icon build. */
#include <stdint.h>
#include <stdio.h>
#include "common/ui_defs.h"
#include "common/icon/menu_extra.h"

static int write_icons(FILE *file, int large, int light)
{
    int bytes = large ? (light ? 640 : 384) : (light ? 416 : 216);
    for (int icon = ICON_VIDEO; icon <= ICON_TOOLS; ++icon) {
        struct font_t font;
        int ok = large ? menu_icon_large_get(&font, icon, light) :
                         menu_icon_small_get(&font, icon, light);
        if (!ok || fwrite(font.data, 1, (size_t)bytes, file) != (size_t)bytes)
            return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    FILE *file;
    int ok;
    if (argc != 2)
        return 2;
    file = fopen(argv[1], "wb");
    if (!file)
        return 1;
    ok = write_icons(file, 0, 0) && write_icons(file, 0, 1) &&
         write_icons(file, 1, 0) && write_icons(file, 1, 1);
    if (fclose(file) != 0)
        ok = 0;
    return ok ? 0 : 1;
}
