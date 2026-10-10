/* Experimental category icon pack. No icon pixels are linked into this file. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "common/ui_defs.h"
#include "common/icon/menu_extra.h"

enum {
    ICON_SMALL_BYTES = 4 * 216,
    ICON_SMALL_LIGHT_BYTES = 4 * 416,
    ICON_LARGE_BYTES = 4 * 384,
    ICON_LARGE_LIGHT_BYTES = 4 * 640,
    ICON_PACK_BYTES = ICON_SMALL_BYTES + ICON_SMALL_LIGHT_BYTES +
                      ICON_LARGE_BYTES + ICON_LARGE_LIGHT_BYTES
};

static uint8_t *icon_pack;

int menu_icon_pack_load(const char *path)
{
    FILE *stream;
    uint8_t *loaded;
    int trailing;

    if (icon_pack)
        return 1;
    stream = fopen(path, "rb");
    if (!stream) {
        fprintf(stderr, "Cannot load category icons: %s\n", path);
        return 0;
    }
    loaded = malloc(ICON_PACK_BYTES);
    if (!loaded) {
        fclose(stream);
        return 0;
    }
    trailing = (fread(loaded, 1, ICON_PACK_BYTES, stream) == ICON_PACK_BYTES) ?
        fgetc(stream) : -2;
    fclose(stream);
    if (trailing != EOF) {
        fprintf(stderr, "Invalid category icon pack: %s\n", path);
        free(loaded);
        return 0;
    }
    icon_pack = loaded;
    return 1;
}

void menu_icon_pack_unload(void)
{
    free(icon_pack);
    icon_pack = NULL;
}

static int icon_index(uint16_t code)
{
    return code >= ICON_VIDEO && code <= ICON_TOOLS ?
        (int)(code - ICON_VIDEO) : -1;
}

int menu_icon_small_get(struct font_t *font, uint16_t code, int light)
{
    int index = icon_index(code);
    if (!icon_pack || index < 0)
        return 0;
    font->data = icon_pack + (light ? ICON_SMALL_BYTES + index * 416 : index * 216);
    font->width = light ? 32 : 24;
    font->height = light ? 26 : 18;
    font->pitch = 32;
    font->skipx = 0;
    font->skipy = 0;
    return 1;
}

int menu_icon_large_get(struct font_t *font, uint16_t code, int light)
{
    int index = icon_index(code);
    if (!icon_pack || index < 0)
        return 0;
    font->data = icon_pack + ICON_SMALL_BYTES + ICON_SMALL_LIGHT_BYTES +
        (light ? ICON_LARGE_BYTES + index * 640 : index * 384);
    font->width = light ? 40 : 32;
    font->height = light ? 32 : 24;
    font->pitch = 32;
    font->skipx = 0;
    font->skipy = 0;
    return 1;
}
