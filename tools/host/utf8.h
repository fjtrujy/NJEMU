#ifndef NJEMU_HOST_UTF8_H
#define NJEMU_HOST_UTF8_H

#include <stddef.h>
#include <stdint.h>

int host_utf8_next(const uint8_t **cursor, const uint8_t *end, uint32_t *codepoint);
int host_utf8_validate(const uint8_t *data, size_t size);
int host_utf8_encode(uint32_t codepoint, uint8_t bytes[4], size_t *size);

#endif
