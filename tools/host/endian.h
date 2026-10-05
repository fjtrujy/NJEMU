#ifndef NJEMU_HOST_ENDIAN_H
#define NJEMU_HOST_ENDIAN_H

#include <stdint.h>

#include "file.h"

int host_append_le16(host_buffer_t *buffer, uint16_t value);
int host_append_le32(host_buffer_t *buffer, uint32_t value);
uint16_t host_read_le16(const uint8_t *data);
uint32_t host_read_le32(const uint8_t *data);

#endif
