/* Shared menu category icons, using the existing packed alpha glyph format. */
#ifndef COMMON_ICON_MENU_EXTRA_H
#define COMMON_ICON_MENU_EXTRA_H

#include <stdint.h>
#include "common/font_t.h"

int menu_icon_small_get(struct font_t *font, uint16_t code, int light);
int menu_icon_large_get(struct font_t *font, uint16_t code, int light);

#ifdef NJEMU_EXTERNAL_MENU_ICONS
int menu_icon_pack_load(const char *path);
void menu_icon_pack_unload(void);
#endif

#endif
