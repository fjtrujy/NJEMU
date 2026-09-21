#ifndef COMMON_UI_UTF8_H
#define COMMON_UI_UTF8_H

#include <stddef.h>
#include <stdint.h>

size_t ui_utf8_decode(const unsigned char *text, uint32_t *codepoint);
int ui_utf8_validate_n(const char *text, size_t length);

#endif /* COMMON_UI_UTF8_H */
