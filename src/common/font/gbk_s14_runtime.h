#ifndef COMMON_FONT_GBK_S14_RUNTIME_H
#define COMMON_FONT_GBK_S14_RUNTIME_H

#include <stdint.h>

#include "common/font_t.h"

int gbk_s14_font_init(const char *base_dir);
void gbk_s14_font_shutdown(void);

int gbk_s14p_get_gryph(struct font_t *font, uint16_t code);
int gbk_s14p_get_pitch(uint16_t code);

#ifdef COMMAND_LIST
int gbk_s14_get_gryph(struct font_t *font, uint16_t code);
#endif

#endif /* COMMON_FONT_GBK_S14_RUNTIME_H */
