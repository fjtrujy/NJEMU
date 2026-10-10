#ifndef NJEMU_HOST_ELF_FOOTPRINT_H
#define NJEMU_HOST_ELF_FOOTPRINT_H

#include "file.h"

#include <stddef.h>
#include <stdint.h>

/* Allocated section sizes, not heap availability or page-aligned process RAM. */
typedef struct host_elf_footprint {
    uint64_t file_bytes;
    uint64_t text_bytes;
    uint64_t rodata_bytes;
    uint64_t data_bytes;
    uint64_t bss_bytes;
    uint64_t load_file_bytes;
    uint64_t load_memory_bytes;
    unsigned int elf_class;
    unsigned int byte_order;
} host_elf_footprint_t;

int host_elf_footprint_parse(const host_buffer_t *file, host_elf_footprint_t *result,
    char *error, size_t error_size);

#endif
