#include "common/cache_storage_driver.h"
#include "ps2/ps2_cache_storage.h"

#if defined(PS2_FAST_CACHE)
#include <cacheio.h>
#ifdef CACHE_IO_PROFILE
#include <cacheio-profile.h>
#endif
#include <errno.h>
#include <limits.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#endif

static bool cache_storage_available;

void ps2_cache_storage_set_available(bool available)
{
	cache_storage_available = available;
}

static int ps2_cache_storage_open(const char *path)
{
#if defined(PS2_FAST_CACHE)
	char absolute_path[PATH_MAX];
	if (!cache_storage_available)
		return -1;

	if (path == NULL)
		return -EINVAL;
	/* RPC bypasses libc open(), so resolve the EE working directory here,
	 * using the same normalization as the POSIX fallback. */
	if (__path_absolute(path, absolute_path, sizeof(absolute_path)) < 0)
		return -ENAMETOOLONG;
	{
		int handle = cacheioOpen(absolute_path);
#ifdef CACHE_IO_PROFILE
		cacheio_stats_t stats;
		if (handle >= 0 && cacheioGetStats(handle, &stats) < 0)
			printf("[cache-iop] profiling unavailable for %s\n", absolute_path);
#endif
		return handle;
	}
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

static void ps2_cache_storage_profile(int handle, const char *name)
{
#ifdef CACHE_IO_PROFILE
	cacheio_stats_t s;
	if (handle < 0 || cacheioGetStats(handle, &s) < 0) return;
	printf("[cache-iop] %s reads=%llu bytes=%llu fragments=%u device_calls=%llu "
		"sectors=%llu min_sectors=%u max_sectors=%u device_us=%llu "
		"defrag_us=%llu dma_us=%llu total_us=%llu errors=%u\n", name,
		(unsigned long long)s.reads, (unsigned long long)s.bytes,
		(unsigned int)s.fragments, (unsigned long long)s.device_calls,
		(unsigned long long)s.sectors, (unsigned int)s.min_sectors,
		(unsigned int)s.max_sectors, (unsigned long long)s.device_us,
		(unsigned long long)s.defrag_us, (unsigned long long)s.dma_us,
		(unsigned long long)s.total_us, (unsigned int)s.errors);
	printf("[cache-ee] %s reads=%llu lock_us=%llu flush_us=%llu rpc_us=%llu\n",
		name, (unsigned long long)s.ee_reads, (unsigned long long)s.ee_wait_us,
		(unsigned long long)s.ee_flush_us, (unsigned long long)s.ee_rpc_us);
#else
	(void)handle;
	(void)name;
#endif
}

const cache_storage_driver_t cache_storage_ps2 = {
	ps2_cache_storage_open,
	ps2_cache_storage_read_at,
	ps2_cache_storage_close,
	ps2_cache_storage_profile,
};
