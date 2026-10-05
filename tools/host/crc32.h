#ifndef NJEMU_HOST_CRC32_H
#define NJEMU_HOST_CRC32_H

#include <stddef.h>
#include <stdint.h>

uint32_t host_crc32(const void *data, size_t size);

#endif
