#include "crc32.h"

uint32_t host_crc32(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xffffffffu;
    size_t i;

    for (i = 0; i < size; ++i) {
        unsigned bit;
        crc ^= bytes[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return crc ^ 0xffffffffu;
}
