#include "elf_footprint.h"

#include <stdio.h>
#include <string.h>

enum {
    ELF32_CLASS = 1,
    ELF64_CLASS = 2,
    ELF_LITTLE_ENDIAN = 1,
    ELF_BIG_ENDIAN = 2,
    ELF_SECTION_NOBITS = 8,
    ELF_SECTION_WRITE = 1,
    ELF_SECTION_ALLOC = 2,
    ELF_SECTION_EXEC = 4
};

enum {
    ELF_PROGRAM_LOAD = 1
};

static uint16_t read_u16(const uint8_t *ptr, unsigned int order)
{
    if (order == ELF_LITTLE_ENDIAN)
        return (uint16_t)((uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8));
    return (uint16_t)(((uint16_t)ptr[0] << 8) | (uint16_t)ptr[1]);
}

static uint32_t read_u32(const uint8_t *ptr, unsigned int order)
{
    uint32_t first = read_u16(ptr, order);
    uint32_t second = read_u16(ptr + 2, order);
    return order == ELF_LITTLE_ENDIAN ? first | (second << 16) : (first << 16) | second;
}

static uint64_t read_u64(const uint8_t *ptr, unsigned int order)
{
    uint64_t first = read_u32(ptr, order);
    uint64_t second = read_u32(ptr + 4, order);
    return order == ELF_LITTLE_ENDIAN ? first | (second << 32) : (first << 32) | second;
}

static int in_range(uint64_t offset, uint64_t length, size_t file_size)
{
    return offset <= (uint64_t)file_size && length <= (uint64_t)file_size - offset;
}

static int invalid(char *error, size_t error_size, const char *reason)
{
    if (error_size != 0)
        snprintf(error, error_size, "%s", reason);
    return 0;
}

int host_elf_footprint_parse(const host_buffer_t *file, host_elf_footprint_t *result,
    char *error, size_t error_size)
{
    const uint8_t *bytes;
    unsigned int elf_class;
    unsigned int order;
    uint64_t sections_offset;
    uint64_t section_count;
    uint64_t section_stride;
    uint64_t min_stride;
    uint64_t programs_offset;
    uint64_t program_stride;
    uint64_t program_count;
    uint64_t min_program_stride;
    uint64_t index;
    host_elf_footprint_t parsed;

    if (file == NULL || result == NULL || file->data == NULL || file->size < 16)
        return invalid(error, error_size, "missing or truncated ELF header");
    bytes = file->data;
    if (memcmp(bytes, "\177ELF", 4) != 0)
        return invalid(error, error_size, "not an ELF file");
    elf_class = bytes[4];
    order = bytes[5];
    if ((elf_class != ELF32_CLASS && elf_class != ELF64_CLASS) ||
        (order != ELF_LITTLE_ENDIAN && order != ELF_BIG_ENDIAN) || bytes[6] != 1)
        return invalid(error, error_size, "unsupported ELF class, byte order, or version");
    if (file->size < (elf_class == ELF32_CLASS ? 52u : 64u))
        return invalid(error, error_size, "truncated ELF header");

    if (elf_class == ELF32_CLASS) {
        programs_offset = read_u32(bytes + 28, order);
        program_stride = read_u16(bytes + 42, order);
        program_count = read_u16(bytes + 44, order);
        min_program_stride = 32;
        sections_offset = read_u32(bytes + 32, order);
        section_stride = read_u16(bytes + 46, order);
        section_count = read_u16(bytes + 48, order);
        min_stride = 40;
    } else {
        programs_offset = read_u64(bytes + 32, order);
        program_stride = read_u16(bytes + 54, order);
        program_count = read_u16(bytes + 56, order);
        min_program_stride = 56;
        sections_offset = read_u64(bytes + 40, order);
        section_stride = read_u16(bytes + 58, order);
        section_count = read_u16(bytes + 60, order);
        min_stride = 64;
    }
    if (section_count == 0 || section_stride < min_stride ||
        section_count > UINT64_MAX / section_stride ||
        !in_range(sections_offset, section_count * section_stride, file->size))
        return invalid(error, error_size, "missing or invalid ELF section table");
    if (program_count != 0 &&
        (program_stride < min_program_stride ||
         program_count > UINT64_MAX / program_stride ||
         !in_range(programs_offset, program_count * program_stride, file->size)))
        return invalid(error, error_size, "invalid ELF program header table");

    memset(&parsed, 0, sizeof(parsed));
    parsed.elf_class = elf_class;
    parsed.byte_order = order;
    parsed.file_bytes = file->size;

    for (index = 0; index < program_count; ++index) {
        const uint8_t *program = bytes + (size_t)(programs_offset + index * program_stride);
        uint64_t type = read_u32(program, order);
        uint64_t offset;
        uint64_t file_length;
        uint64_t memory_length;
        if (type != ELF_PROGRAM_LOAD)
            continue;
        if (elf_class == ELF32_CLASS) {
            offset = read_u32(program + 4, order);
            file_length = read_u32(program + 16, order);
            memory_length = read_u32(program + 20, order);
        } else {
            offset = read_u64(program + 8, order);
            file_length = read_u64(program + 32, order);
            memory_length = read_u64(program + 40, order);
        }
        if (file_length > memory_length || !in_range(offset, file_length, file->size) ||
            parsed.load_file_bytes > UINT64_MAX - file_length ||
            parsed.load_memory_bytes > UINT64_MAX - memory_length)
            return invalid(error, error_size, "invalid ELF PT_LOAD segment");
        parsed.load_file_bytes += file_length;
        parsed.load_memory_bytes += memory_length;
    }

    for (index = 0; index < section_count; ++index) {
        const uint8_t *section = bytes + (size_t)(sections_offset + index * section_stride);
        uint64_t type = read_u32(section + 4, order);
        uint64_t flags;
        uint64_t offset;
        uint64_t size;
        uint64_t *category;

        if (elf_class == ELF32_CLASS) {
            flags = read_u32(section + 8, order);
            offset = read_u32(section + 16, order);
            size = read_u32(section + 20, order);
        } else {
            flags = read_u64(section + 8, order);
            offset = read_u64(section + 24, order);
            size = read_u64(section + 32, order);
        }
        if (type != ELF_SECTION_NOBITS && !in_range(offset, size, file->size))
            return invalid(error, error_size, "ELF section contents extend beyond file");
        if (!(flags & ELF_SECTION_ALLOC))
            continue;
        if (type == ELF_SECTION_NOBITS)
            category = &parsed.bss_bytes;
        else if (flags & ELF_SECTION_WRITE)
            category = &parsed.data_bytes;
        else if (flags & ELF_SECTION_EXEC)
            category = &parsed.text_bytes;
        else
            category = &parsed.rodata_bytes;

        if (*category > UINT64_MAX - size)
            return invalid(error, error_size, "ELF allocated size overflow");
        *category += size;
    }
    *result = parsed;
    if (error_size != 0)
        error[0] = '\0';
    return 1;
}
