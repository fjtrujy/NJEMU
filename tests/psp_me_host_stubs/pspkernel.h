#ifndef NJEMU_TEST_PSPKERNEL_H
#define NJEMU_TEST_PSPKERNEL_H

#include <stdint.h>

void sceKernelDcacheWritebackInvalidateRange(void *address, uint32_t size);
void sceKernelDcacheInvalidateRange(void *address, uint32_t size);
uint64_t sceKernelGetSystemTimeWide(void);

#endif /* NJEMU_TEST_PSPKERNEL_H */
