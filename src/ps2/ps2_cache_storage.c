#include "common/cache_storage_driver.h"
#include "ps2/ps2_cache_storage.h"

#if defined(PS2_FAST_CACHE)
#include <cacheio.h>
#endif

static bool cache_storage_available;

void ps2_cache_storage_set_available(bool available)
{
	cache_storage_available = available;
}

static int ps2_cache_storage_open(const char *path)
{
#if defined(PS2_FAST_CACHE)
	if (!cache_storage_available)
		return -1;

	return cacheioOpen(path);
#else
	(void)path;
	return -1;
#endif
}

static int ps2_cache_storage_read_at(int handle, uint64_t offset, void *buffer, size_t size)
{
#if defined(PS2_FAST_CACHE)
	if (size > UINT32_MAX)
		return -1;
	return cacheioReadAt(handle, offset, buffer, (unsigned int)size);
#else
	(void)handle;
	(void)offset;
	(void)buffer;
	(void)size;
	return -1;
#endif
}

static void ps2_cache_storage_close(int handle)
{
#if defined(PS2_FAST_CACHE)
	if (handle >= 0)
		cacheioClose(handle);
#else
	(void)handle;
#endif
}

const cache_storage_driver_t cache_storage_ps2 = {
	ps2_cache_storage_open,
	ps2_cache_storage_read_at,
	ps2_cache_storage_close,
};
