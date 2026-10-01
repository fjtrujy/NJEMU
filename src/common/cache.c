/******************************************************************************

	cache.c

	Memory Cache Interface Functions

******************************************************************************/

#include <limits.h>
#include <sys/param.h>
#include "emucfg.h"
#include "common/cache.h"
#include "common/cache_storage_driver.h"
#include "common/emulator_options.h"
#include "common/emulator_runtime.h"
#include "common/input_driver.h"
#include "common/runtime_paths.h"
#include "common/path_utils.h"
#include "common/ticker_driver.h"
#include "common/ui_text_driver.h"
#include "common/ui.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common/memory_sizes.h"
#include "common/zip_archive.h"

#if (EMU_SYSTEM == CPS2)
#include "cps2/memintrf.h"
#include "cps2/vidhrdw.h"
#elif (EMU_SYSTEM == MVS)
#include "mvs/memintrf.h"
#include "mvs/processed_assets.h"
#include "mvs/vidhrdw.h"
#endif

#if USE_CACHE
static inline void cache_read_legacy(int fd, void *buffer, size_t size)
{
	ssize_t bytes_read = read(fd, buffer, size);
	(void)bytes_read;
}

#define BLOCK_MASK			0xffff
#define BLOCK_SHIFT			16			// 16
#define BLOCK_NOT_CACHED	0xffff
#define BLOCK_EMPTY			0xffffffff
#define MIN_RUNTIME_CACHE_BLOCKS ((2u * 1024u * 1024u) / CACHE_BLOCK_SIZE)

#if (EMU_SYSTEM == MVS)
#define MAX_PCM_BLOCKS		0x140		// 0x100 PCM for 3xx
#endif


/******************************************************************************
	Global Structures/Variables
******************************************************************************/

uint32_t (*read_cache)(uint32_t offset);
void (*update_cache)(uint32_t offset);
#if (EMU_SYSTEM == CPS2)
uint32_t block_offset[MAX_CACHE_BLOCKS];
uint8_t  *block_empty = (uint8_t *)block_offset;
#endif


/******************************************************************************
	Local Structures/Variables
******************************************************************************/

typedef struct cache_s
{
	int idx;
	int block;
	uint32_t frame;
	struct cache_s *prev;
	struct cache_s *next;
} cache_t;


static cache_t *cache_data;
static cache_t *head;
static cache_t *tail;

static int num_cache;
static uint16_t ALIGN16_DATA blocks[MAX_CACHE_BLOCKS];
/* Cache slots and on-disk blocks remain 64 KiB. Runtime demand reads may fill
 * them in 16/32 KiB parts; one bit tracks each 16 KiB quarter. */
static uint8_t gfx_valid_parts[MAX_CACHE_BLOCKS];
#define CACHE_READ_QUARTER_SIZE (CACHE_BLOCK_SIZE / 4)

static size_t cache_default_read_size(void)
{
#if defined(PS2) && (EMU_SYSTEM == MVS)
	/* Measured on real PS2/MX4SIO: 16 KiB materially reduces demand-read wait. */
	return 16u * 1024u;
#else
	/* Preserve established behavior until each platform/core is measured. */
	return CACHE_BLOCK_SIZE;
#endif
}

size_t cache_resolved_read_size(void)
{
	switch (option_cache_read_size)
	{
	case CACHE_READ_SIZE_16K: return 16u * 1024u;
	case CACHE_READ_SIZE_32K: return 32u * 1024u;
	case CACHE_READ_SIZE_64K: return CACHE_BLOCK_SIZE;
	case CACHE_READ_SIZE_AUTO:
	default: return cache_default_read_size();
	}
}
static int32_t cache_fd;
static zip_archive_t cache_zip_archive;
static zip_entry_t cache_zip_entry;

int cache_type;
static char spr_cache_name[PATH_MAX];

static void cache_list_init(cache_t *data, int count, cache_t **list_head, cache_t **list_tail)
{
	int i;

	*list_head = NULL;
	*list_tail = NULL;
	if (data == NULL || count <= 0)
		return;

	memset(data, 0, sizeof(*data) * (size_t)count);
	for (i = 0; i < count; i++)
	{
		data[i].idx = i;
		data[i].block = -1;
		data[i].prev = i > 0 ? &data[i - 1] : NULL;
		data[i].next = i + 1 < count ? &data[i + 1] : NULL;
	}

	*list_head = &data[0];
	*list_tail = &data[count - 1];
}

static void cache_rotate_head_to_tail(cache_t **list_head, cache_t **list_tail)
{
	cache_t *p = *list_head;

	if (p == NULL || p == *list_tail)
		return;

	*list_head = p->next;
	(*list_head)->prev = NULL;
	p->prev = *list_tail;
	p->next = NULL;
	(*list_tail)->next = p;
	*list_tail = p;
}

#if (EMU_SYSTEM == MVS)
/* PCM cache infrastructure is always compiled; pcm_cache_enable is the
 * runtime gate selected by the game-specific memory plan. */
int pcm_cache_enable;

static cache_t *pcm_data;
static cache_t *pcm_head;
static cache_t *pcm_tail;
static int num_pcm_cache;

static uint16_t ALIGN16_DATA pcm_blocks[MAX_PCM_BLOCKS];
static uint8_t pcm_valid_parts[MAX_PCM_BLOCKS];
static int32_t pcm_fd;
static int64_t cache_file_pos;
static int64_t pcm_file_pos;
static int cache_storage_handle = -1;
static int pcm_storage_handle = -1;
#ifdef CACHE_IO_VALIDATE_ACCELERATED
static uint8_t ALIGN16_DATA cache_validation_buffers[2][CACHE_BLOCK_SIZE];
#endif

static int cachefile_open_resolved(int type, char *resolved_path, size_t resolved_size)
{
	int32_t fd = -1;
	char path[PATH_MAX];

	switch (type)
	{
	case CACHE_INFO:
		if (use_parent_crom && use_parent_srom && use_parent_vrom)
		{
			if (!path_format(path, sizeof(path), "%s/%s_cache/cache_info", cache_dir, parent_name)) break;
			fd = open(path, O_RDONLY, 0777);
		}
		else
		{
			if (!path_format(path, sizeof(path), "%s/%s_cache/cache_info", cache_dir, game_name)) break;
			fd = open(path, O_RDONLY, 0777);
		}
		break;

	case CACHE_CROM:
		if (use_parent_crom)
		{
			if (!path_format(path, sizeof(path), "%s/%s_cache/crom", cache_dir, parent_name)) break;
			fd = open(path, O_RDONLY, 0777);
		}
		if (fd < 0)
		{
			if (!path_format(path, sizeof(path), "%s/%s_cache/crom", cache_dir, game_name)) break;
			fd = open(path, O_RDONLY, 0777);
		}
		break;

	case CACHE_SROM:
		if (use_parent_srom)
		{
			if (!path_format(path, sizeof(path), "%s/%s_cache/srom", cache_dir, parent_name)) break;
			fd = open(path, O_RDONLY, 0777);
		}
		if (fd < 0)
		{
			if (!path_format(path, sizeof(path), "%s/%s_cache/srom", cache_dir, game_name)) break;
			fd = open(path, O_RDONLY, 0777);
		}
		break;

	case CACHE_VROM:
		if (use_parent_vrom)
		{
			if (!path_format(path, sizeof(path), "%s/%s_cache/vrom", cache_dir, parent_name)) break;
			fd = open(path, O_RDONLY, 0777);
		}
		if (fd < 0)
		{
			if (!path_format(path, sizeof(path), "%s/%s_cache/vrom", cache_dir, game_name)) break;
			fd = open(path, O_RDONLY, 0777);
		}
		break;
	}

	if (fd >= 0 && resolved_path != NULL && resolved_size > 0)
		snprintf(resolved_path, resolved_size, "%s", path);

	return fd;
}

int cachefile_open(int type)
{
	return cachefile_open_resolved(type, NULL, 0);
}

static int cache_storage_open_optional(const char *path, const char *name)
{
	int handle;

	if (cache_storage_driver == NULL || cache_storage_driver->open == NULL ||
		cache_storage_driver->readAt == NULL || cache_storage_driver->close == NULL) {
#ifdef CACHE_IO_PROFILE
		printf("[cache-io] %s extent reader unavailable; using POSIX\n", name);
#endif
		return -1;
	}

	handle = cache_storage_driver->open(path);
	if (handle >= 0)
		printf("[cache-io] %s extent reader enabled\n", name);
#ifdef CACHE_IO_PROFILE
	else
		printf("[cache-io] %s extent open failed (%d); using POSIX\n", name, handle);
#endif
	return handle;
}

static void cache_storage_close_optional(int *handle)
{
	if (handle == NULL || *handle < 0)
		return;

	if (cache_storage_driver != NULL && cache_storage_driver->close != NULL)
		cache_storage_driver->close(*handle);
	*handle = -1;
}

size_t cachefile_zip_read(int type, const char *name, void *buf, size_t size)
{
	zip_archive_t archive = {0};
	zip_entry_t entry = {0};
	int use_parent = 0;
	char path[PATH_MAX];
	size_t bytes = 0;

	switch (type)
	{
	case CACHE_CROM: use_parent = use_parent_crom; break;
	case CACHE_SROM: use_parent = use_parent_srom; break;
	case CACHE_VROM: use_parent = use_parent_vrom; break;
	default: break;
	}

	if (use_parent && parent_name[0])
	{
		if (!path_format(path, sizeof(path), "%s/%s_cache.zip", cache_dir, parent_name)) return 0;
		if (zip_archive_open(&archive, path))
		{
			if (zip_entry_open(&archive, name, &entry))
			{
				bytes = zip_entry_read(&entry, buf, size);
				if (!zip_entry_close(&entry))
					bytes = 0;
				zip_archive_close(&archive);
				return bytes;
			}
			zip_archive_close(&archive);
		}
	}

	if (!path_format(path, sizeof(path), "%s/%s_cache.zip", cache_dir, game_name)) return 0;
	if (!zip_archive_open(&archive, path))
		return 0;

	if (zip_entry_open(&archive, name, &entry))
	{
		bytes = zip_entry_read(&entry, buf, size);
		if (!zip_entry_close(&entry))
			bytes = 0;
	}
	zip_archive_close(&archive);
	return bytes;
}

#ifdef CACHE_IO_PROFILE
typedef struct cache_io_profile_s
{
	uint64_t preload_reads;
	uint64_t preload_seeks;
	uint64_t preload_seek_skips;
	uint64_t preload_accelerated_reads;
	uint64_t preload_bytes;
	uint64_t preload_time_us;
	uint64_t hits;
	uint64_t misses;
	uint64_t sequential_misses;
	uint64_t seeks;
	uint64_t seek_skips;
	uint64_t accelerated_reads;
	uint64_t accelerated_fallbacks;
	uint64_t bytes_read;
	uint64_t miss_time_us;
	uint64_t max_miss_time_us;
	uint64_t timed_reads;
	uint64_t posix_reads;
	uint64_t errors;
	uint64_t reloads;
	uint64_t validation_checks, validation_errors;
	uint64_t latency[5]; /* <1, <4, <16, <64, >=64 ms */
	uint8_t seen[MAX_CACHE_BLOCKS * 4 / 8];
	int32_t last_miss_block;
} cache_io_profile_t;

static cache_io_profile_t crom_io_profile;
static cache_io_profile_t pcm_io_profile;
static uint64_t cache_io_last_report_us;

static uint64_t cache_io_now_us(void)
{
	if (ticker_data && ticker_driver && ticker_driver->currentUs)
		return ticker_driver->currentUs(ticker_data);
	return 0;
}

static void cache_io_profile_reset(cache_io_profile_t *profile)
{
	memset(profile, 0, sizeof(*profile));
	profile->last_miss_block = -1;
	cache_io_last_report_us = cache_io_now_us();
}

static void cache_io_profile_miss(cache_io_profile_t *profile, int block)
{
	profile->misses++;
	if (block >= 0 && block < (int)(sizeof(profile->seen) * 8) &&
		(profile->seen[block >> 3] & (1u << (block & 7))))
		profile->reloads++;
	if (profile->last_miss_block >= 0 && block == profile->last_miss_block + 1)
		profile->sequential_misses++;
	profile->last_miss_block = block;
}

static void cache_io_profile_time(cache_io_profile_t *profile, uint64_t start)
{
	uint64_t elapsed;

	if (!start)
		return;

	elapsed = cache_io_now_us() - start;
	profile->timed_reads++;
	profile->latency[elapsed < 1000 ? 0 : elapsed < 4000 ? 1 :
		elapsed < 16000 ? 2 : elapsed < 64000 ? 3 : 4]++;
	profile->miss_time_us += elapsed;
	if (elapsed > profile->max_miss_time_us)
		profile->max_miss_time_us = elapsed;
}

static void cache_io_profile_print(const char *name, const cache_io_profile_t *profile)
{
	uint64_t accesses = profile->hits + profile->misses;
	uint64_t avg_us = profile->timed_reads ? profile->miss_time_us / profile->timed_reads : 0;
	uint64_t rate_x100 = accesses ? (profile->hits * 10000) / accesses : 0;

	printf("[cache-io] %s preload_reads=%llu preload_seeks=%llu "
		"preload_seek_skips=%llu preload_accelerated_reads=%llu "
		"preload_bytes=%llu preload_time_us=%llu "
		"hits=%llu misses=%llu hit_rate=%llu.%02llu%% "
		"sequential_misses=%llu seeks=%llu seek_skips=%llu "
		"accelerated_reads=%llu accelerated_fallbacks=%llu bytes=%llu "
		"avg_miss_us=%llu max_miss_us=%llu "
		"wait_us=%llu timed_reads=%llu posix_reads=%llu errors=%llu reloads=%llu "
		"latency_lt1_4_16_64_ge64_ms=%llu,%llu,%llu,%llu,%llu validation_checks=%llu validation_errors=%llu\n",
		name,
		(unsigned long long)profile->preload_reads,
		(unsigned long long)profile->preload_seeks,
		(unsigned long long)profile->preload_seek_skips,
		(unsigned long long)profile->preload_accelerated_reads,
		(unsigned long long)profile->preload_bytes,
		(unsigned long long)profile->preload_time_us,
		(unsigned long long)profile->hits,
		(unsigned long long)profile->misses,
		(unsigned long long)(rate_x100 / 100),
		(unsigned long long)(rate_x100 % 100),
		(unsigned long long)profile->sequential_misses,
		(unsigned long long)profile->seeks,
		(unsigned long long)profile->seek_skips,
		(unsigned long long)profile->accelerated_reads,
		(unsigned long long)profile->accelerated_fallbacks,
		(unsigned long long)profile->bytes_read,
		(unsigned long long)avg_us,
		(unsigned long long)profile->max_miss_time_us,
		(unsigned long long)profile->miss_time_us,
		(unsigned long long)profile->timed_reads,
		(unsigned long long)profile->posix_reads,
		(unsigned long long)profile->errors,
		(unsigned long long)profile->reloads,
		(unsigned long long)profile->latency[0],
		(unsigned long long)profile->latency[1],
		(unsigned long long)profile->latency[2],
		(unsigned long long)profile->latency[3],
		(unsigned long long)profile->latency[4],
		(unsigned long long)profile->validation_checks,
		(unsigned long long)profile->validation_errors);
}

static void cache_io_profile_snapshot(void)
{
	uint64_t now = cache_io_now_us();
	/* Report after a completed read, at most once per five seconds. The read
	 * timer excludes printf; cumulative counters also print at shutdown. */
	if (!now || now - cache_io_last_report_us < 5000000ULL)
		return;
	/* Reserve this interval before printf/RPC can yield to the PCM thread. */
	cache_io_last_report_us = now;
	printf("[cache-io] snapshot_us=%llu block_bytes=%u cumulative=1\n",
		(unsigned long long)now, (unsigned int)CACHE_BLOCK_SIZE);
	cache_io_profile_print("crom", &crom_io_profile);
	cache_io_profile_print("pcm", &pcm_io_profile);
	if (cache_storage_driver && cache_storage_driver->profile) {
		cache_storage_driver->profile(cache_storage_handle, "crom");
		cache_storage_driver->profile(pcm_storage_handle, "pcm");
	}
	cache_io_last_report_us = cache_io_now_us();
}

#endif

static int mvs_cache_read_range(int fd, int *storage_handle, int64_t *known_pos,
	uint16_t block, uint8_t *dst, const char *name, unsigned int within, unsigned int read_size
#ifdef CACHE_IO_PROFILE
	, cache_io_profile_t *profile, int runtime_miss
#endif
)
{
	const int64_t offset = ((int64_t)block << BLOCK_SHIFT) + within;
	ssize_t bytes;
#ifdef CACHE_IO_VALIDATE_ACCELERATED
	/* Graphics and PCM can validate concurrently on different EE threads. */
	uint8_t *cache_validation_buffer = cache_validation_buffers[
		storage_handle == &pcm_storage_handle ? 1 : 0];
#endif
#ifdef CACHE_IO_PROFILE
	uint64_t start = cache_io_now_us();
	/* Include preloaded blocks so their first runtime reload is visible. */
	if (profile == &crom_io_profile) {
		unsigned int part;
		for (part = within / CACHE_READ_QUARTER_SIZE;
			part < (within + read_size) / CACHE_READ_QUARTER_SIZE; part++) {
			unsigned int key = block * 4u + part;
			if (key < sizeof(profile->seen) * 8)
				profile->seen[key >> 3] |= (uint8_t)(1u << (key & 7));
		}
	} else
	if (profile == &pcm_io_profile) {
		unsigned int part;
		for (part = within / CACHE_READ_QUARTER_SIZE;
			part < (within + read_size) / CACHE_READ_QUARTER_SIZE; part++) {
			unsigned int key = block * 4u + part;
			if (key < sizeof(profile->seen) * 8)
				profile->seen[key >> 3] |= (uint8_t)(1u << (key & 7));
		}
	} else
	if (block < MAX_CACHE_BLOCKS)
		profile->seen[block >> 3] |= (uint8_t)(1u << (block & 7));
#endif

	if (storage_handle != NULL && *storage_handle >= 0 &&
		cache_storage_driver != NULL && cache_storage_driver->readAt != NULL)
	{
		bytes = cache_storage_driver->readAt(*storage_handle, (uint64_t)offset,
			dst, read_size);
		if (bytes >= 0 && (size_t)bytes == read_size)
		{
#ifdef CACHE_IO_VALIDATE_ACCELERATED
			ssize_t reference_bytes;
			size_t mismatch;

			memcpy(cache_validation_buffer, dst, read_size);
			if (lseek(fd, offset, SEEK_SET) < 0)
			{
				printf("[cache-io-validate] %s block=%u offset=%lld reference seek failed\n",
					name, (unsigned int)block, (long long)offset);
				*known_pos = -1;
				return 0;
			}
			reference_bytes = read(fd, dst, read_size);
			if (reference_bytes < 0 || (size_t)reference_bytes != read_size)
			{
				printf("[cache-io-validate] %s block=%u offset=%lld reference read=%d\n",
					name, (unsigned int)block, (long long)offset, (int)reference_bytes);
				*known_pos = -1;
				return 0;
			}
			*known_pos = offset + read_size;

			for (mismatch = 0; mismatch < read_size; ++mismatch)
			{
				if (cache_validation_buffer[mismatch] != dst[mismatch])
					break;
			}
#ifdef CACHE_IO_PROFILE
			profile->validation_checks++;
			if (mismatch != read_size) profile->validation_errors++;
#endif
			if (mismatch != read_size)
			{
				printf("[cache-io-validate] MISMATCH %s block=%u offset=%lld byte=%u accelerated=%02x posix=%02x\n",
					name, (unsigned int)block, (long long)offset,
					(unsigned int)mismatch, cache_validation_buffer[mismatch], dst[mismatch]);
			}
#endif
#ifdef CACHE_IO_PROFILE
			if (runtime_miss)
			{
				profile->accelerated_reads++;
				profile->bytes_read += read_size;
				cache_io_profile_time(profile, start);
				cache_io_profile_snapshot();
			}
			else
			{
				uint64_t elapsed = start ? cache_io_now_us() - start : 0;
				profile->preload_reads++;
				profile->preload_accelerated_reads++;
				profile->preload_bytes += read_size;
				profile->preload_time_us += elapsed;
			}
#endif
			return 1;
		}

#ifdef CACHE_IO_PROFILE
		profile->accelerated_fallbacks++;
#endif
		printf("[cache-io] %s extent read failed (%d); disabling accelerator\n",
			name, (int)bytes);
		cache_storage_close_optional(storage_handle);
	}

	if (
#ifdef CACHE_IO_FORCE_SEEK
		1
#else
		*known_pos != offset
#endif
	)
	{
		if (lseek(fd, offset, SEEK_SET) < 0)
		{
			*known_pos = -1;
#ifdef CACHE_IO_PROFILE
			if (runtime_miss) {
				profile->errors++;
				cache_io_profile_time(profile, start);
				cache_io_profile_snapshot();
			}
#endif
			return 0;
		}
#ifdef CACHE_IO_PROFILE
		if (runtime_miss)
			profile->seeks++;
		else
			profile->preload_seeks++;
#endif
	}
#ifdef CACHE_IO_PROFILE
	else if (runtime_miss)
	{
		profile->seek_skips++;
	}
	else
	{
		profile->preload_seek_skips++;
	}
#endif

	bytes = read(fd, dst, read_size);
	if (bytes >= 0 && (size_t)bytes == read_size)
		*known_pos = offset + read_size;
	else
		*known_pos = -1;

#ifdef CACHE_IO_PROFILE
	if (runtime_miss)
	{
		profile->posix_reads++;
		if (bytes < 0 || (size_t)bytes != read_size) profile->errors++;
		if (bytes > 0)
			profile->bytes_read += (uint64_t)bytes;
		cache_io_profile_time(profile, start);
		cache_io_profile_snapshot();
	}
	else
	{
		uint64_t elapsed = start ? cache_io_now_us() - start : 0;
		profile->preload_reads++;
		if (bytes > 0)
			profile->preload_bytes += (uint64_t)bytes;
		profile->preload_time_us += elapsed;
	}
#endif

	return bytes >= 0 && (size_t)bytes == read_size;
}

static int mvs_cache_read_block(int fd, int *storage_handle, int64_t *known_pos,
	uint16_t block, uint8_t *dst, const char *name
#ifdef CACHE_IO_PROFILE
	, cache_io_profile_t *profile, int runtime_miss
#endif
)
{
	return mvs_cache_read_range(fd, storage_handle, known_pos, block, dst, name,
		0, CACHE_BLOCK_SIZE
#ifdef CACHE_IO_PROFILE
		, profile, runtime_miss
#endif
	);
}

#endif


/******************************************************************************
	Local Functions
******************************************************************************/

#if (EMU_SYSTEM == MVS)


/*------------------------------------------------------
	Read PCM Cache
------------------------------------------------------*/

uint8_t *pcm_cache_read(uint16_t new_part)
{
	const unsigned int requested_quarter = new_part & 3u;
	const uint16_t new_block = new_part >> 2;
	const size_t read_size = cache_resolved_read_size();
	const unsigned int quarters_per_read = (unsigned int)read_size / CACHE_READ_QUARTER_SIZE;
	const unsigned int first_quarter = (requested_quarter / quarters_per_read) * quarters_per_read;
	const unsigned int within = first_quarter * CACHE_READ_QUARTER_SIZE;
	const uint8_t valid_mask = (uint8_t)(((1u << quarters_per_read) - 1u) << first_quarter);
	uint32_t idx = pcm_blocks[new_block];
	cache_t *p;

	if (idx == BLOCK_NOT_CACHED)
	{
		p = pcm_head;
		if (p->block >= 0)
			pcm_blocks[p->block] = BLOCK_NOT_CACHED;
		p->block = new_block;
		pcm_blocks[new_block] = p->idx;
		pcm_valid_parts[p->idx] = 0;
	}
	else
		p = &pcm_data[idx];

	if ((pcm_valid_parts[p->idx] & valid_mask) != valid_mask)
	{
#ifdef CACHE_IO_PROFILE
		cache_io_profile_miss(&pcm_io_profile,
			new_block * (CACHE_BLOCK_SIZE / (unsigned int)read_size) +
			first_quarter / quarters_per_read);
#endif
		uint8_t *dst = &memory_region_sound1[(p->idx << BLOCK_SHIFT) + within];
		if (mvs_cache_read_range(pcm_fd, &pcm_storage_handle, &pcm_file_pos,
			new_block, dst, "pcm", within, read_size
#ifdef CACHE_IO_PROFILE
			, &pcm_io_profile, 1
#endif
		))
			pcm_valid_parts[p->idx] |= valid_mask;
		else
			memset(dst, 0, read_size);
	}
#ifdef CACHE_IO_PROFILE
	else
		pcm_io_profile.hits++;
#endif

	if (p->frame != frames_displayed)
	{
		p->frame = frames_displayed;
		if (p->next)
		{
			if (p->prev)
			{
				p->prev->next = p->next;
				p->next->prev = p->prev;
			}
			else
			{
				pcm_head = p->next;
				pcm_head->prev = NULL;
			}
			p->prev = pcm_tail;
			p->next = NULL;
			pcm_tail->next = p;
			pcm_tail = p;
		}
	}

	return &memory_region_sound1[(p->idx << BLOCK_SHIFT) +
		requested_quarter * CACHE_READ_QUARTER_SIZE];
}

#endif

/*------------------------------------------------------
	Open Data File in ZIP Cache File
------------------------------------------------------*/

static int zip_cache_open(int number)
{
	static const char cnv_table[16] =
	{
		'0','1','2','3','4','5','6','7',
		'8','9','a','b','c','d','e','f'
	};
	char fname[4];

	fname[0] = cnv_table[(number >> 8) & 0x0f];
	fname[1] = cnv_table[(number >> 4) & 0x0f];
	fname[2] = cnv_table[ number       & 0x0f];
	fname[3] = '\0';

	zip_entry_close(&cache_zip_entry);
	return zip_entry_open(&cache_zip_archive, fname, &cache_zip_entry);
}


/*------------------------------------------------------
	Read Data File from ZIP Cache File
------------------------------------------------------*/

static int zip_cache_load(int offs)
{
	size_t bytes = zip_entry_read(&cache_zip_entry,
		&GFX_MEMORY[offs << 16], CACHE_BLOCK_SIZE);
	int close_ok = zip_entry_close(&cache_zip_entry);

	return bytes == CACHE_BLOCK_SIZE && close_ok;
}


/*------------------------------------------------------
	Open Data File in Folder Cache
------------------------------------------------------*/

static int folder_cache_open(int number)
{
	static const char cnv_table[16] =
	{
		'0','1','2','3','4','5','6','7',
		'8','9','a','b','c','d','e','f'
	};
	char fname[PATH_MAX];

	if (!path_format(fname, sizeof(fname), "%s/%c%c%c", spr_cache_name,
		cnv_table[(number >> 8) & 0x0f],
		cnv_table[(number >> 4) & 0x0f],
		cnv_table[ number       & 0x0f]))
		return 0;

	cache_fd = open(fname, O_RDONLY, 0777);

	return cache_fd != -1;
}


/*------------------------------------------------------
	Read Data File from Folder Cache
------------------------------------------------------*/

#define folder_cache_load(offs)									\
	cache_read_legacy(cache_fd, &GFX_MEMORY[offs << 16], CACHE_BLOCK_SIZE);	\
	close(cache_fd);											\
	cache_fd = -1;

/*------------------------------------------------------
	Fill Cache with Data

	Since 3 types of data are mixed, we should divide the area
	and read each with an appropriate size, but we're just
	reading from the beginning to simplify.
------------------------------------------------------*/

static int fill_cache(void)
{
	int i, block;
	cache_t *p;

	i = 0;
	block = 0;

#if (EMU_SYSTEM == MVS)
	if (cache_type == CACHE_RAWFILE)
	{
		while (i < num_cache)
		{
			p = head;
			if (!mvs_cache_read_block((int32_t)cache_fd, &cache_storage_handle,
				&cache_file_pos, block, &GFX_MEMORY[p->idx << BLOCK_SHIFT], "crom"
#ifdef CACHE_IO_PROFILE
				, &crom_io_profile, 0
#endif
			))
				return 0;

			p->block = block;
			blocks[block] = p->idx;
				gfx_valid_parts[p->idx] = 0x0f;

				cache_rotate_head_to_tail(&head, &tail);
			i++;

			if (++block >= MAX_CACHE_BLOCKS)
				break;
		}
	}
	else
	{
		while (i < num_cache)
		{
			int ok;

			p = head;
			p->block = block;
			blocks[block] = p->idx;

			if (cache_type == CACHE_ZIPFILE)
				ok = zip_cache_open(p->block);
			else
				ok = folder_cache_open(p->block);

			if (!ok)
			{
				msg_printf(TEXT(COULD_NOT_OPEN_SPRITE_BLOCK_x), p->block);
				return 0;
			}

			if (cache_type == CACHE_ZIPFILE) {
				if (!zip_cache_load(p->idx))
					return 0;
			} else {
				folder_cache_load(p->idx)
			}

				cache_rotate_head_to_tail(&head, &tail);
			i++;

			if (++block >= MAX_CACHE_BLOCKS)
				break;
		}
	}
	if (pcm_cache_enable)
	{
		i = 0;
		block = 0;

			while (i < num_pcm_cache)
			{
				p = pcm_head;
				if (!mvs_cache_read_block(pcm_fd, &pcm_storage_handle, &pcm_file_pos,
					block, &memory_region_sound1[p->idx << BLOCK_SHIFT], "pcm"
#ifdef CACHE_IO_PROFILE
					, &pcm_io_profile, 0
#endif
				))
					return 0;

				p->block = block;
				pcm_blocks[block] = p->idx;
				pcm_valid_parts[p->idx] = 0x0f;

				cache_rotate_head_to_tail(&pcm_head, &pcm_tail);
			i++;
			block++;
		}
	}
#else
	if (cache_type == CACHE_RAWFILE)
	{
		while (i < num_cache)
		{
			if (block_offset[block] != BLOCK_EMPTY)
			{
				p = head;
				p->block = block;
				blocks[block] = p->idx;
				gfx_valid_parts[p->idx] = 0x0f;

				lseek(cache_fd, block_offset[block], SEEK_SET);
				cache_read_legacy(cache_fd, &GFX_MEMORY[p->idx << BLOCK_SHIFT], CACHE_BLOCK_SIZE);

				cache_rotate_head_to_tail(&head, &tail);
				i++;
			}

			if (++block >= MAX_CACHE_BLOCKS)
				break;
		}
	}
	else
	{
		while (i < num_cache)
		{
			if (!block_empty[block])
			{
				int ok;

				p = head;
				p->block = block;
				blocks[block] = p->idx;

				if (cache_type == CACHE_ZIPFILE)
					ok = zip_cache_open(p->block);
				else
					ok = folder_cache_open(p->block);

				if (!ok)
				{
					msg_printf(TEXT(COULD_NOT_OPEN_SPRITE_BLOCK_x), p->block);
					return 0;
				}

				if (cache_type == CACHE_ZIPFILE) {
					if (!zip_cache_load(p->idx))
						return 0;
				} else {
					folder_cache_load(p->idx)
				}

				cache_rotate_head_to_tail(&head, &tail);
				i++;
			}

			if (++block >= MAX_CACHE_BLOCKS)
				break;
		}
	}
#endif

	return 1;
}

/*------------------------------------------------------
	Address Conversion Only

	When all data is stored in memory with empty areas removed
------------------------------------------------------*/

#if (EMU_SYSTEM == CPS2)
static uint32_t read_cache_direct(uint32_t offset)
{
	return offset;
}

static uint32_t read_cache_static(uint32_t offset)
{
	int idx = blocks[offset >> BLOCK_SHIFT];

	return ((idx << BLOCK_SHIFT) | (offset & BLOCK_MASK));
}
#endif


/*------------------------------------------------------
	Use Uncompressed Cache

	Read data from uncompressed cache file
------------------------------------------------------*/

static uint32_t read_cache_rawfile(uint32_t offset)
{
	int16_t new_block = offset >> BLOCK_SHIFT;
	uint32_t idx = blocks[new_block];
	cache_t *p;
	size_t read_size = cache_resolved_read_size();
	unsigned int part = (offset & BLOCK_MASK) / (unsigned int)read_size;
	unsigned int within = part * (unsigned int)read_size;
	unsigned int first_quarter = within / CACHE_READ_QUARTER_SIZE;
	unsigned int quarter_count = (unsigned int)read_size / CACHE_READ_QUARTER_SIZE;
	uint8_t valid_mask = (uint8_t)(((1u << quarter_count) - 1u) << first_quarter);

	if (idx == BLOCK_NOT_CACHED)
	{
		p = head;
		if (p->block >= 0)
			blocks[p->block] = BLOCK_NOT_CACHED;
		p->block = new_block;
		blocks[new_block] = p->idx;
		gfx_valid_parts[p->idx] = 0;
	}
	else
		p = &cache_data[idx];

	if ((gfx_valid_parts[p->idx] & valid_mask) != valid_mask)
	{
		uint8_t *dst = &GFX_MEMORY[(p->idx << BLOCK_SHIFT) + within];
#if (EMU_SYSTEM == MVS)
#ifdef CACHE_IO_PROFILE
		cache_io_profile_miss(&crom_io_profile,
			new_block * (CACHE_BLOCK_SIZE / (unsigned int)read_size) + part);
#endif
		if (mvs_cache_read_range(cache_fd, &cache_storage_handle, &cache_file_pos,
			new_block, dst, "crom", within, read_size
#ifdef CACHE_IO_PROFILE
			, &crom_io_profile, 1
#endif
		))
			gfx_valid_parts[p->idx] |= valid_mask;
		else
			memset(dst, 0, read_size);
#else
		if (lseek((int32_t)cache_fd, (off_t)block_offset[new_block] + within, SEEK_SET) >= 0)
		{
			ssize_t bytes = read((int32_t)cache_fd, dst, read_size);
			if (bytes == (ssize_t)read_size)
				gfx_valid_parts[p->idx] |= valid_mask;
			else
				memset(dst, 0, read_size);
		}
		else
			memset(dst, 0, read_size);
#endif
	}
#ifdef CACHE_IO_PROFILE
	else
		crom_io_profile.hits++;
#endif

	if (p->next)
	{
		if (p->prev)
		{
			p->prev->next = p->next;
			p->next->prev = p->prev;
		}
		else
		{
			head = p->next;
			head->prev = NULL;
		}
		p->prev = tail;
		p->next = NULL;
		tail->next = p;
		tail = p;
	}

	return ((uint32_t)p->idx << BLOCK_SHIFT) | (offset & BLOCK_MASK);
}

/*------------------------------------------------------
	Use ZIP Compressed Cache

	Read data from ZIP compressed cache file
------------------------------------------------------*/

static uint32_t read_cache_zipfile(uint32_t offset)
{
	int16_t new_block = offset >> BLOCK_SHIFT;
	uint32_t idx = blocks[new_block];
	cache_t *p;

	if (idx == BLOCK_NOT_CACHED)
	{
		if (!zip_cache_open(new_block))
			return 0;

		p = head;
		blocks[p->block] = BLOCK_NOT_CACHED;

		p->block = new_block;
		blocks[new_block] = p->idx;

		if (!zip_cache_load(p->idx))
			return 0;
	}
	else p = &cache_data[idx];

	if (p->next)
	{
		if (p->prev)
		{
			p->prev->next = p->next;
			p->next->prev = p->prev;
		}
		else
		{
			head = p->next;
			head->prev = NULL;
		}

		p->prev = tail;
		p->next = NULL;

		tail->next = p;
		tail = p;
	}

	return ((tail->idx << BLOCK_SHIFT) | (offset & BLOCK_MASK));
}


/*------------------------------------------------------
	Use Folder Cache

	Read data from individual block files in folder
------------------------------------------------------*/

static uint32_t read_cache_folder(uint32_t offset)
{
	int16_t new_block = offset >> BLOCK_SHIFT;
	uint32_t idx = blocks[new_block];
	cache_t *p;

	if (idx == BLOCK_NOT_CACHED)
	{
		if (!folder_cache_open(new_block))
			return 0;

		p = head;
		blocks[p->block] = BLOCK_NOT_CACHED;

		p->block = new_block;
		blocks[new_block] = p->idx;

		folder_cache_load(p->idx);
	}
	else p = &cache_data[idx];

	if (p->next)
	{
		if (p->prev)
		{
			p->prev->next = p->next;
			p->next->prev = p->prev;
		}
		else
		{
			head = p->next;
			head->prev = NULL;
		}

		p->prev = tail;
		p->next = NULL;

		tail->next = p;
		tail = p;
	}

	return ((tail->idx << BLOCK_SHIFT) | (offset & BLOCK_MASK));
}


/*------------------------------------------------------
	Update Cache Data

	Moves specified data to the end of cache.
	Not needed when cache is not managed.
------------------------------------------------------*/

static inline void update_cache_dynamic(uint32_t offset)
{
	int16_t new_block = offset >> BLOCK_SHIFT;
	int idx = blocks[new_block];

	if (idx != BLOCK_NOT_CACHED)
	{
		cache_t *p = &cache_data[idx];

		if (p->next)
		{
			if (p->prev)
			{
				p->prev->next = p->next;
				p->next->prev = p->prev;
			}
			else
			{
				head = p->next;
				head->prev = NULL;
			}

			p->prev = tail;
			p->next = NULL;

			tail->next = p;
			tail = p;
		}
	}
}


/******************************************************************************
	Cache Interface Functions
******************************************************************************/

/*------------------------------------------------------
	Initialize Cache
------------------------------------------------------*/

void cache_init(void)
{
	int i;

	zip_entry_close(&cache_zip_entry);
	zip_archive_close(&cache_zip_archive);

	if (cache_data)
	{
		free(cache_data);
		cache_data = NULL;
	}
	head = NULL;
	tail = NULL;
	num_cache = 0;
	cache_fd = -1;

#if (EMU_SYSTEM == MVS)
	cache_storage_close_optional(&cache_storage_handle);
	cache_storage_close_optional(&pcm_storage_handle);
	cache_type = CACHE_NOTFOUND;
	read_cache = NULL;
#else
	cache_type = CACHE_NOTFOUND;
	read_cache = read_cache_direct;
#endif
	update_cache = NULL;

	for (i = 0; i < MAX_CACHE_BLOCKS; i++)
		blocks[i] = BLOCK_NOT_CACHED;
	memset(gfx_valid_parts, 0, sizeof(gfx_valid_parts));
	printf("[cache] demand_read_bytes=%u slot_bytes=%u\n",
		(unsigned int)cache_resolved_read_size(), (unsigned int)CACHE_BLOCK_SIZE);

#if (EMU_SYSTEM == MVS)
	if (pcm_data)
	{
		free(pcm_data);
		pcm_data = NULL;
	}
	pcm_head = NULL;
	pcm_tail = NULL;
	num_pcm_cache = 0;
	memset(pcm_valid_parts, 0, sizeof(pcm_valid_parts));
	pcm_cache_enable = 0;
	pcm_fd = -1;
	cache_file_pos = -1;
	pcm_file_pos = -1;

#ifdef CACHE_IO_PROFILE
	cache_io_profile_reset(&crom_io_profile);
	cache_io_profile_reset(&pcm_io_profile);
#endif

	for (i = 0; i < MAX_PCM_BLOCKS; i++)
		pcm_blocks[i] = BLOCK_NOT_CACHED;
#endif
}


/*------------------------------------------------------
	Start Cache Processing
------------------------------------------------------*/

int cache_start(const memory_plan_t *plan, void *preallocated_gfx, void *preallocated_pcm)
{
	int i, found;
	int requested_cache_blocks;
	int minimum_cache_blocks;
	int source_cache_blocks;
	uint32_t size = 0;
	char version_str[8] = {0};
#if (EMU_SYSTEM == MVS)
	int32_t fd;
	int requested_pcm_blocks;
	char crom_path[PATH_MAX];
	char pcm_path[PATH_MAX];
#endif

	if (plan == NULL)
	{
		msg_printf(TEXT(MEMORY_NOT_ENOUGH));
		return 0;
	}

	/* R10 allocation-shape probes retain the buffers that proved the requested
	 * shape is feasible. Adopt them here instead of freeing and reallocating the
	 * same sizes. Ownership transfers to the normal memory-region shutdown path. */
	GFX_MEMORY = (uint8_t *)preallocated_gfx;
#if (EMU_SYSTEM == MVS)
	if (preallocated_pcm != NULL)
		memory_region_sound1 = preallocated_pcm;
#else
	(void)preallocated_pcm;
#endif

	source_cache_blocks = (int)(((uint64_t)GFX_SIZE + CACHE_BLOCK_SIZE - 1) >> BLOCK_SHIFT);
	requested_cache_blocks = (int)(plan->gfx_cache_bytes >> BLOCK_SHIFT);
	if (requested_cache_blocks > source_cache_blocks)
		requested_cache_blocks = source_cache_blocks;
	if (requested_cache_blocks > MAX_CACHE_BLOCKS)
		requested_cache_blocks = MAX_CACHE_BLOCKS;
	if (requested_cache_blocks <= 0)
	{
		msg_printf(TEXT(MEMORY_NOT_ENOUGH));
		return 0;
	}
	minimum_cache_blocks = requested_cache_blocks < (int)MIN_RUNTIME_CACHE_BLOCKS ?
		requested_cache_blocks : (int)MIN_RUNTIME_CACHE_BLOCKS;

#if (EMU_SYSTEM == MVS)
	requested_pcm_blocks = (int)(plan->pcm_cache_bytes >> BLOCK_SHIFT);
	if (requested_pcm_blocks > MAX_PCM_BLOCKS)
		requested_pcm_blocks = MAX_PCM_BLOCKS;
	if (requested_pcm_blocks > (int)(((uint64_t)memory_length_sound1 + CACHE_BLOCK_SIZE - 1) >> BLOCK_SHIFT))
		requested_pcm_blocks = (int)(((uint64_t)memory_length_sound1 + CACHE_BLOCK_SIZE - 1) >> BLOCK_SHIFT);
#endif

	zip_entry_close(&cache_zip_entry);
	zip_archive_close(&cache_zip_archive);

#if (EMU_SYSTEM == MVS)

	msg_printf(TEXT(LOADING_CACHE_INFORMATION_DATA));

	found = 0;

	/* Try folder format first: {game}_cache/cache_info */
	if ((fd = mvs_processed_asset_open(MVS_PROCESSED_INFO)) >= 0)
	{
		cache_type = CACHE_RAWFILE;
		cache_read_legacy(fd, version_str, 8);

		if (strcmp(version_str, "MVS_" CACHE_VERSION) == 0)
		{
			cache_read_legacy(fd, gfx_pen_usage[2], memory_length_gfx3 / 128);
			found = 1;
		}
		close(fd);

		if (!found)
		{
			msg_printf(TEXT(UNSUPPORTED_VERSION_OF_CACHE_FILE), version_str[5], version_str[6]);
			msg_printf(TEXT(CURRENT_REQUIRED_VERSION_IS_x));
			msg_printf(TEXT(PLEASE_REBUILD_CACHE_FILE));
			return 0;
		}
	}

	/* Try zip format: {game}_cache.zip */
	if (!found)
	{
		cache_type = CACHE_ZIPFILE;

		if (use_parent_crom && parent_name[0])
		{
			if (!path_format(spr_cache_name, sizeof(spr_cache_name), "%s/%s_cache.zip", cache_dir, parent_name)) return 0;
			if (zip_archive_open(&cache_zip_archive, spr_cache_name))
				found = 1;
		}

		if (!found)
		{
			if (!path_format(spr_cache_name, sizeof(spr_cache_name), "%s/%s_cache.zip", cache_dir, game_name)) { found = 0; }
			if (zip_archive_open(&cache_zip_archive, spr_cache_name))
				found = 1;
		}

		if (found)
		{
			if (zip_entry_open(&cache_zip_archive, "cache_info", &cache_zip_entry))
			{
				memset(version_str, 0, 8);
				if (zip_entry_read(&cache_zip_entry, version_str, 8) != 8)
					found = 0;

				if (found && strcmp(version_str, "MVS_" CACHE_VERSION) == 0)
				{
					if (zip_entry_read(&cache_zip_entry, gfx_pen_usage[2],
						memory_length_gfx3 / 128) != memory_length_gfx3 / 128)
						found = 0;
				}
				else
				{
					found = 0;
				}
				if (!zip_entry_close(&cache_zip_entry))
					found = 0;
			}
			else
			{
				found = 0;
			}
			if (!found)
			{
				zip_archive_close(&cache_zip_archive);
				msg_printf(TEXT(UNSUPPORTED_VERSION_OF_CACHE_FILE), version_str[5], version_str[6]);
				msg_printf(TEXT(CURRENT_REQUIRED_VERSION_IS_x));
				msg_printf(TEXT(PLEASE_REBUILD_CACHE_FILE));
				return 0;
			}
		}
	}

	if (!found)
	{
		cache_type = CACHE_NOTFOUND;
		msg_printf(TEXT(COULD_NOT_OPEN_CACHE_FILE));
		return 0;
	}

	if (cache_type == CACHE_RAWFILE)
	{
		if (option_sound_enable && disable_sound && requested_pcm_blocks > 0)
		{
			pcm_fd = mvs_processed_asset_open(MVS_PROCESSED_VROM);
			if (pcm_fd >= 0)
			{
				pcm_file_pos = 0;
				pcm_storage_handle = cache_storage_open_optional(pcm_path, "pcm");
			}
		}
	}

	/* Open crom for block access (folder format only) */
	if (cache_type == CACHE_RAWFILE)
	{
		if ((cache_fd = cachefile_open_resolved(CACHE_CROM, crom_path,
			sizeof(crom_path))) < 0)
		{
			if (pcm_fd >= 0)
			{
				close(pcm_fd);
				pcm_fd = -1;
			}
			cache_storage_close_optional(&pcm_storage_handle);
			msg_printf(TEXT(COULD_NOT_OPEN_CACHE_FILE));
			return 0;
		}
		cache_file_pos = 0;
		cache_storage_handle = cache_storage_open_optional(crom_path, "crom");
	}
	/* For zip format, blocks will be accessed via zip_cache_open on demand */

#elif (EMU_SYSTEM == CPS2)
	found = 1;
	cache_type = CACHE_RAWFILE;

	/* Try raw file format: {game}.cache */
	if (!path_format(spr_cache_name, sizeof(spr_cache_name), "%s/%s.cache", cache_dir, game_name)) { found = 0; }
	if ((cache_fd = open(spr_cache_name, O_RDONLY, 0777)) < 0)
	{
		if (!path_format(spr_cache_name, sizeof(spr_cache_name), "%s/%s.cache", cache_dir, cache_parent_name)) { found = 0; }
		if ((cache_fd = open(spr_cache_name, O_RDONLY, 0777)) < 0)
		{
			found = 0;
		}
	}

	/* Try zip file format: {game}_cache.zip */
	if (!found)
	{
		found = 1;
		cache_type = CACHE_ZIPFILE;

		if (!path_format(spr_cache_name, sizeof(spr_cache_name), "%s/%s_cache.zip", cache_dir, game_name)) found = 0;
		if (!zip_archive_open(&cache_zip_archive, spr_cache_name))
		{
			if (!path_format(spr_cache_name, sizeof(spr_cache_name), "%s/%s_cache.zip", cache_dir, cache_parent_name)) { found = 0; }
			if (!zip_archive_open(&cache_zip_archive, spr_cache_name))
			{
				found = 0;
				zip_archive_close(&cache_zip_archive);
			}
		}
	}

	/* Try folder format: {game}_cache/cache_info */
	if (!found)
	{
		char path[PATH_MAX];
		found = 1;
		cache_type = CACHE_FOLDER;

		if (!path_format(spr_cache_name, sizeof(spr_cache_name), "%s/%s_cache", cache_dir, game_name)) { found = 0; }
		if (!path_format(path, sizeof(path), "%s/cache_info", spr_cache_name)) { found = 0; }
		if ((cache_fd = open(path, O_RDONLY, 0777)) < 0)
		{
			if (!path_format(spr_cache_name, sizeof(spr_cache_name), "%s/%s_cache", cache_dir, cache_parent_name)) { found = 0; }
			if (!path_format(path, sizeof(path), "%s/cache_info", spr_cache_name)) { found = 0; }
			if ((cache_fd = open(path, O_RDONLY, 0777)) < 0)
			{
				found = 0;
			}
		}
	}

	if (!found)
	{
		cache_type = CACHE_NOTFOUND;
		msg_printf(TEXT(COULD_NOT_OPEN_CACHE_FILE));
		return 0;
	}

	msg_printf(TEXT(LOADING_CACHE_INFORMATION_DATA));

	if (cache_type == CACHE_RAWFILE)
	{
		cache_read_legacy(cache_fd, version_str, 8);

		if (strcmp(version_str, "CPS2" CACHE_VERSION) == 0)
		{
			cache_read_legacy(cache_fd, gfx_pen_usage[TILE08], gfx_total_elements[TILE08]);
			cache_read_legacy(cache_fd, gfx_pen_usage[TILE16], gfx_total_elements[TILE16]);
			cache_read_legacy(cache_fd, gfx_pen_usage[TILE32], gfx_total_elements[TILE32]);
			cache_read_legacy(cache_fd, block_offset, MAX_CACHE_BLOCKS * sizeof(uint32_t));
		}
		else
		{
			close(cache_fd);
			found = 0;
		}
	}
	else if (cache_type == CACHE_ZIPFILE)
	{
		if (zip_entry_open(&cache_zip_archive, "cache_info", &cache_zip_entry))
		{
			if (zip_entry_read(&cache_zip_entry, version_str, 8) != 8)
				found = 0;

			if (found && strcmp(version_str, "CPS2" CACHE_VERSION) == 0)
			{
				if (zip_entry_read(&cache_zip_entry, gfx_pen_usage[TILE08],
					gfx_total_elements[TILE08]) != gfx_total_elements[TILE08] ||
					zip_entry_read(&cache_zip_entry, gfx_pen_usage[TILE16],
					gfx_total_elements[TILE16]) != gfx_total_elements[TILE16] ||
					zip_entry_read(&cache_zip_entry, gfx_pen_usage[TILE32],
					gfx_total_elements[TILE32]) != gfx_total_elements[TILE32] ||
					zip_entry_read(&cache_zip_entry, block_empty,
					MAX_CACHE_BLOCKS) != MAX_CACHE_BLOCKS)
					found = 0;
			}
			else
			{
				found = 0;
			}
			if (!zip_entry_close(&cache_zip_entry))
				found = 0;
		}
		else
		{
			found = 0;
		}
		if (!found)
			zip_archive_close(&cache_zip_archive);
	}
	else /* CACHE_FOLDER */
	{
		/* cache_fd is already open from the folder detection above */
		cache_read_legacy(cache_fd, version_str, 8);

		if (strcmp(version_str, "CPS2" CACHE_VERSION) == 0)
		{
			cache_read_legacy(cache_fd, gfx_pen_usage[TILE08], gfx_total_elements[TILE08]);
			cache_read_legacy(cache_fd, gfx_pen_usage[TILE16], gfx_total_elements[TILE16]);
			cache_read_legacy(cache_fd, gfx_pen_usage[TILE32], gfx_total_elements[TILE32]);
			cache_read_legacy(cache_fd, block_empty, MAX_CACHE_BLOCKS);
			close(cache_fd);
			cache_fd = -1;
		}
		else
		{
			close(cache_fd);
			cache_fd = -1;
			found = 0;
		}
	}
	if (!found)
	{
		msg_printf(TEXT(UNSUPPORTED_VERSION_OF_CACHE_FILE), version_str[5], version_str[6]);
		msg_printf(TEXT(CURRENT_REQUIRED_VERSION_IS_x));
		msg_printf(TEXT(PLEASE_REBUILD_CACHE_FILE));
		return 0;
	}

#endif

	i = requested_cache_blocks;

	if (GFX_MEMORY == NULL)
	{
		for (; i >= minimum_cache_blocks; --i)
		{
			size = (uint32_t)i << BLOCK_SHIFT;
			GFX_MEMORY = (uint8_t *)malloc(size);
			if (GFX_MEMORY != NULL)
				break;
		}

		if (GFX_MEMORY == NULL)
		{
			msg_printf(TEXT(COULD_NOT_ALLOCATE_CACHE_MEMORY));
			return 0;
		}
	}
	else
	{
		size = (uint32_t)i << BLOCK_SHIFT;
	}

	num_cache = i;
	memset(GFX_MEMORY, 0, size);

#if (EMU_SYSTEM == CPS2)
	if (num_cache >= source_cache_blocks)
	{
		read_cache = read_cache_static;
		update_cache = NULL;
	}
	else
#endif
	{
		if (cache_type == CACHE_RAWFILE)
			read_cache = read_cache_rawfile;
		else if (cache_type == CACHE_ZIPFILE)
			read_cache = read_cache_zipfile;
		else
			read_cache = read_cache_folder;
		update_cache = update_cache_dynamic;
	}

	cache_data = (cache_t *)malloc(sizeof(*cache_data) * (size_t)num_cache);
	if (cache_data == NULL)
	{
	#if (EMU_SYSTEM == MVS)
		cache_storage_close_optional(&cache_storage_handle);
		cache_storage_close_optional(&pcm_storage_handle);
	#endif
		msg_printf(TEXT(COULD_NOT_ALLOCATE_CACHE_MEMORY));
		return 0;
	}
	cache_list_init(cache_data, num_cache, &head, &tail);

#if (EMU_SYSTEM == MVS)
	/* GFX/C-ROM is the primary cache target. Only after it has been secured do
	 * we consume the PCM target, retrying down in cache-block increments when
	 * fragmentation prevents the planned contiguous allocation. */
	if (option_sound_enable && disable_sound)
	{
		if (requested_pcm_blocks > 0 && pcm_fd >= 0)
		{
			if (memory_region_sound1 != NULL)
			{
				num_pcm_cache = requested_pcm_blocks;
			}
			else
			{
				for (i = requested_pcm_blocks; i > 0; --i)
				{
					memory_region_sound1 = malloc((size_t)i << BLOCK_SHIFT);
					if (memory_region_sound1 != NULL)
					{
						num_pcm_cache = i;
						break;
					}
				}
			}
		}

		if (num_pcm_cache > 0)
		{
			pcm_cache_enable = 1;
			disable_sound = 0;
		}
		else
		{
			/* An empirical R10 shape may have retained a PCM buffer before the
			 * cache file was opened. If PCM streaming is unavailable, release
			 * that otherwise-unused allocation immediately. */
			if (preallocated_pcm != NULL && memory_region_sound1 != NULL)
			{
				free(memory_region_sound1);
				memory_region_sound1 = NULL;
			}
			if (pcm_fd >= 0)
			{
				close(pcm_fd);
				pcm_fd = -1;
			}
			cache_storage_close_optional(&pcm_storage_handle);
			memory_length_sound1 = 0;
		}
	}

	if (pcm_cache_enable)
	{
		pcm_data = (cache_t *)malloc(sizeof(*pcm_data) * (size_t)num_pcm_cache);
		if (pcm_data == NULL)
		{
			free(memory_region_sound1);
			memory_region_sound1 = NULL;
			close(pcm_fd);
			pcm_fd = -1;
			cache_storage_close_optional(&pcm_storage_handle);
			pcm_cache_enable = 0;
			num_pcm_cache = 0;
			disable_sound = 1;
			memory_length_sound1 = 0;
		}
		else
		{
			cache_list_init(pcm_data, num_pcm_cache, &pcm_head, &pcm_tail);
			msg_printf(TEXT(PCM_CACHE_ENABLED));
			msg_printf(TEXT(CACHE_USAGE_PCM),
				(num_pcm_cache << BLOCK_SHIFT) / 1024,
				memory_length_sound1 / 1024);
		}
	}
#endif

#if (EMU_SYSTEM == CPS2)
	msg_printf(TEXT(CACHE_USAGE_GFX),
		(num_cache << BLOCK_SHIFT) / 1024,
		(source_cache_blocks << BLOCK_SHIFT) / 1024);
#elif (EMU_SYSTEM == MVS)
	msg_printf(TEXT(CACHE_USAGE_CROM),
		(num_cache << BLOCK_SHIFT) / 1024,
		(source_cache_blocks << BLOCK_SHIFT) / 1024);
#endif

	if (!fill_cache())
	{
		msg_printf(TEXT(CACHE_LOAD_ERROR));
		pad_wait_press(PAD_WAIT_INFINITY);
		Loop = LOOP_BROWSER;
		return 0;
	}

	return 1;
}


/*------------------------------------------------------
	End Cache Processing
------------------------------------------------------*/

void cache_shutdown(void)
{
#if (EMU_SYSTEM == MVS)
	#ifdef CACHE_IO_PROFILE
	cache_io_profile_print("crom", &crom_io_profile);
	if (pcm_cache_enable || pcm_io_profile.hits || pcm_io_profile.misses)
		cache_io_profile_print("pcm", &pcm_io_profile);
	#endif
	cache_storage_close_optional(&pcm_storage_handle);
	cache_storage_close_optional(&cache_storage_handle);
	if (pcm_cache_enable)
	{
		if (pcm_fd != -1)
		{
			close(pcm_fd);
			pcm_fd = -1;
			}
			pcm_cache_enable = 0;
		}
		pcm_file_pos = -1;
		if (pcm_data)
		{
			free(pcm_data);
			pcm_data = NULL;
		}
		pcm_head = NULL;
		pcm_tail = NULL;
		num_pcm_cache = 0;
	#endif
	if (cache_type == CACHE_RAWFILE)
	{
		if (cache_fd != -1)
		{
			close((int32_t)cache_fd);
			cache_fd = -1;
		}
#if (EMU_SYSTEM == MVS)
		cache_file_pos = -1;
#endif
	}
	else if (cache_type == CACHE_ZIPFILE)
	{
		zip_entry_close(&cache_zip_entry);
		zip_archive_close(&cache_zip_archive);
	}
		/* CACHE_FOLDER: nothing to close (blocks opened/closed on demand) */

		if (cache_data)
		{
			free(cache_data);
			cache_data = NULL;
		}
		head = NULL;
		tail = NULL;
		num_cache = 0;
}


/*------------------------------------------------------
	Temporarily Stop/Resume Cache
------------------------------------------------------*/

void cache_sleep(int flag)
{
	if (num_cache)
	{
		if (flag)
		{
			if (cache_type == CACHE_RAWFILE)
			{
	#if (EMU_SYSTEM == MVS)
				cache_storage_close_optional(&cache_storage_handle);
	#endif
				close((int32_t)cache_fd);
#if (EMU_SYSTEM == MVS)
				cache_fd = -1;
				cache_file_pos = -1;
#endif
			}
			else if (cache_type == CACHE_ZIPFILE)
			{
				zip_entry_close(&cache_zip_entry);
				zip_archive_close(&cache_zip_archive);
			}
	#if (EMU_SYSTEM == MVS)
			if (pcm_cache_enable)
			{
				cache_storage_close_optional(&pcm_storage_handle);
				close(pcm_fd);
				pcm_fd = -1;
				pcm_file_pos = -1;
			}
#endif
		}
		else
		{
			if (cache_type == CACHE_RAWFILE)
			{
	#if (EMU_SYSTEM == MVS)
				char path[PATH_MAX];
				cache_fd = cachefile_open_resolved(CACHE_CROM, path, sizeof(path));
				cache_file_pos = cache_fd >= 0 ? 0 : -1;
				if (cache_fd >= 0)
					cache_storage_handle = cache_storage_open_optional(path, "crom");
	#else
				cache_fd = open(spr_cache_name, O_RDONLY, 0777);
#endif
			}
			else if (cache_type == CACHE_ZIPFILE)
			{
				zip_archive_open(&cache_zip_archive, spr_cache_name);
			}
			/* CACHE_FOLDER: nothing to reopen */
	#if (EMU_SYSTEM == MVS)
			if (pcm_cache_enable)
			{
				char path[PATH_MAX];
				pcm_fd = cachefile_open_resolved(CACHE_VROM, path, sizeof(path));
				pcm_file_pos = pcm_fd >= 0 ? 0 : -1;
				if (pcm_fd >= 0)
					pcm_storage_handle = cache_storage_open_optional(path, "pcm");
			}
#endif
		}
	}
}


#ifdef SAVE_STATE

/*------------------------------------------------------
	Temporarily Allocate State Save Area
------------------------------------------------------*/

uint8_t *cache_alloc_state_buffer(int32_t size)
{
	int32_t fd;
	char path[PATH_MAX];

	if (!path_format(path, sizeof(path), "%sstate/cache.tmp", launchDir)) return NULL;

	if ((fd = open(path, O_WRONLY|O_CREAT, 0777)) >= 0)
	{
		{ ssize_t io_result = write(fd, GFX_MEMORY, size); (void)io_result; }
		close(fd);
		return GFX_MEMORY;
	}
	return NULL;
}

/*------------------------------------------------------
	Free Save Area and Restore Backed Up Cache
------------------------------------------------------*/

void cache_free_state_buffer(int32_t size)
{
	int32_t fd;
	char path[PATH_MAX];

	if (!path_format(path, sizeof(path), "%sstate/cache.tmp", launchDir)) return;

	if ((fd = open(path, O_RDONLY, 0777)) >= 0)
	{
		cache_read_legacy(fd, GFX_MEMORY, size);
		close(fd);
	}
	remove(path);
}

#endif /* SAVE_STATE */

#endif /* USE_CACHE */
