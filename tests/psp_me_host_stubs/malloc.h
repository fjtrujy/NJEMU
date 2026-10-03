#ifndef NJEMU_TEST_MALLOC_H
#define NJEMU_TEST_MALLOC_H

#include <stddef.h>
#include <stdlib.h>

static inline void *memalign(size_t alignment, size_t size)
{
	void *pointer = NULL;
	if (posix_memalign(&pointer, alignment, size) != 0)
		return NULL;
	return pointer;
}

#endif /* NJEMU_TEST_MALLOC_H */
