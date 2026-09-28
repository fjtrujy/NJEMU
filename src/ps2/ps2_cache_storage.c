#include "common/cache_storage_driver.h"
#include "ps2/ps2_cache_storage.h"

#include <cacheio.h>

static bool cache_storage_available;

void ps2_cache_storage_set_available(bool available)
{
	cache_storage_available = available;
}

static int ps2_cache_storage_open(const char *path)
{
	if (!cache_storage_available)
		return -1;

	return cacheioOpen(path);
}

static int ps2_cache_storage_read_at(int handle, uint64_t offset, void *buffer, size_t size)
{
	if (size > UINT32_MAX)
		return -1;
	return cacheioReadAt(handle, offset, buffer, (unsigned int)size);
}

static void ps2_cache_storage_close(int handle)
{
	if (handle >= 0)
		cacheioClose(handle);
}

const cache_storage_driver_t cache_storage_ps2 = {
	ps2_cache_storage_open,
	ps2_cache_storage_read_at,
	ps2_cache_storage_close,
};
