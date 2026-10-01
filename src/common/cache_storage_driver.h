/******************************************************************************
 *
 * cache_storage_driver.h
 *
 * Optional platform accelerator for large read-only cache files.
 *
 ******************************************************************************/

#ifndef CACHE_STORAGE_DRIVER_H
#define CACHE_STORAGE_DRIVER_H

#include <stddef.h>
#include <stdint.h>

typedef struct cache_storage_driver
{
	int (*open)(const char *path);
	int (*readAt)(int handle, uint64_t offset, void *buffer, size_t size);
	void (*close)(int handle);
	void (*profile)(int handle, const char *name); /* Optional diagnostic snapshot. */
} cache_storage_driver_t;

extern const cache_storage_driver_t *const cache_storage_driver;

#endif /* CACHE_STORAGE_DRIVER_H */
