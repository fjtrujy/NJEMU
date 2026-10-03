#ifndef NJEMU_TEST_ME_CORE_MAPPER_H
#define NJEMU_TEST_ME_CORE_MAPPER_H

#include <stdint.h>

void meCoreDcacheWritebackRange(void *address, uint32_t size);
void meCoreDcacheInvalidateRange(void *address, uint32_t size);

#endif /* NJEMU_TEST_ME_CORE_MAPPER_H */
