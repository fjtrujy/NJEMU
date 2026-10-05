#ifndef NJEMU_HOST_TEXT_H
#define NJEMU_HOST_TEXT_H

#include <stddef.h>
#include <stdint.h>

char *host_trim(char *text);
int host_parse_u32(const char *text, uint32_t *value);
int host_parse_size(const char *text, size_t *value);
int host_ascii_identifier(const char *text);
int host_ascii_game_name(const char *text);
char *host_strdup(const char *text);
char *host_strndup(const char *text, size_t size);

#endif
