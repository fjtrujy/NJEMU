#include "utf8.h"

int host_utf8_next(const uint8_t **cursor, const uint8_t *end, uint32_t *codepoint)
{
    const uint8_t *p = *cursor;
    uint32_t value;
    size_t length;
    size_t i;

    if (p >= end)
        return 0;
    if (p[0] < 0x80) {
        value = p[0];
        length = 1;
    } else if ((p[0] & 0xe0) == 0xc0) {
        value = p[0] & 0x1fu;
        length = 2;
        if (value < 2)
            return -1;
    } else if ((p[0] & 0xf0) == 0xe0) {
        value = p[0] & 0x0fu;
        length = 3;
    } else if ((p[0] & 0xf8) == 0xf0) {
        value = p[0] & 0x07u;
        length = 4;
        if (value > 4)
            return -1;
    } else {
        return -1;
    }

    if ((size_t)(end - p) < length)
        return -1;
    for (i = 1; i < length; ++i) {
        if ((p[i] & 0xc0) != 0x80)
            return -1;
        value = (value << 6) | (uint32_t)(p[i] & 0x3f);
    }

    if ((length == 2 && value < 0x80)
        || (length == 3 && value < 0x800)
        || (length == 4 && value < 0x10000)
        || (value >= 0xd800 && value <= 0xdfff)
        || value > 0x10ffff)
        return -1;

    *cursor = p + length;
    *codepoint = value;
    return 1;
}

int host_utf8_validate(const uint8_t *data, size_t size)
{
    const uint8_t *cursor = data;
    const uint8_t *end = data + size;
    uint32_t codepoint;

    while (cursor < end) {
        if (host_utf8_next(&cursor, end, &codepoint) < 0)
            return 0;
    }
    return 1;
}

int host_utf8_encode(uint32_t codepoint, uint8_t bytes[4], size_t *size)
{
    if (codepoint <= 0x7f) {
        bytes[0] = (uint8_t)codepoint;
        *size = 1;
    } else if (codepoint <= 0x7ff) {
        bytes[0] = (uint8_t)(0xc0 | (codepoint >> 6));
        bytes[1] = (uint8_t)(0x80 | (codepoint & 0x3f));
        *size = 2;
    } else if (codepoint <= 0xffff) {
        if (codepoint >= 0xd800 && codepoint <= 0xdfff)
            return 0;
        bytes[0] = (uint8_t)(0xe0 | (codepoint >> 12));
        bytes[1] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3f));
        bytes[2] = (uint8_t)(0x80 | (codepoint & 0x3f));
        *size = 3;
    } else if (codepoint <= 0x10ffff) {
        bytes[0] = (uint8_t)(0xf0 | (codepoint >> 18));
        bytes[1] = (uint8_t)(0x80 | ((codepoint >> 12) & 0x3f));
        bytes[2] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3f));
        bytes[3] = (uint8_t)(0x80 | (codepoint & 0x3f));
        *size = 4;
    } else {
        return 0;
    }
    return 1;
}
