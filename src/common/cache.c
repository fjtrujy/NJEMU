/******************************************************************************

	cache.c

	Memory Cache Interface Functions

******************************************************************************/

#include <limits.h>
#include <sys/param.h>
#include "emumain.h"
#include "common/memory_sizes.h"

#if USE_CACHE
#ifdef LARGE_MEMORY
#define MIN_CACHE_SIZE		0x40		// Lower limit, original 0x40, 4MB
#else
#define MIN_CACHE_SIZE		0x20		// Lower limit, original 0x40, 4MB
#endif
/*
32MB 0x200 660CFW exit pspfiler and tempgba,crash.
26MB 0x1a0 620CFW push vol and screen button crash.
*/
#ifdef LARGE_MEMORY
#define MAX_CACHE_SIZE		0x200		// Upper limit 32MB 0x200  
#else
#define MAX_CACHE_SIZE		0x140		// Upper limit 20MB 0x140
#endif
#define CACHE_SAFETY		0x20000		// Free memory size after cache allocation 128KB
#define BLOCK_MASK			0xffff
#define BLOCK_SHIFT			16			// 16
#define BLOCK_NOT_CACHED	0xffff
#define BLOCK_EMPTY			0xffffffff

#if (EMU_SYSTEM == MVS)
#define MAX_PCM_BLOCKS		0x140		// 0x100 PCM for 3xx
#define MAX_PCM_SIZE		0x30		// 0x30 smaller value = more remaining memory
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


static cache_t ALIGN16_DATA cache_data[MAX_CACHE_SIZE];
static cache_t *head;
static cache_t *tail;

static int num_cache;
static uint16_t ALIGN16_DATA blocks[MAX_CACHE_BLOCKS];
static int64_t cache_fd;

int cache_type;
static char spr_cache_name[PATH_MAX];

#if (EMU_SYSTEM == MVS)
/* Phase 2b.1: PCM cache infrastructure is always compiled. pcm_cache_enable
 * is the runtime gate; LARGE_MEMORY (or large tier with preload_sound) keeps
 * it at 0 so the streaming paths are inert.
 */
int pcm_cache_enable;

static cache_t ALIGN16_DATA pcm_data[MAX_PCM_SIZE];
static cache_t *pcm_head;
static cache_t *pcm_tail;

static uint16_t ALIGN16_DATA pcm_blocks[MAX_PCM_BLOCKS];
static int32_t pcm_fd;
static int64_t cache_file_pos;
static int64_t pcm_file_pos;

#ifdef CACHE_IO_PROFILE
typedef struct cache_io_profile_s
{
	uint64_t preload_reads;
	uint64_t preload_seeks;
	uint64_t preload_seek_skips;
	uint64_t preload_bytes;
	uint64_t preload_time_us;
	uint64_t hits;
	uint64_t misses;
	uint64_t sequential_misses;
	uint64_t seeks;
	uint64_t seek_skips;
	uint64_t bytes_read;
	uint64_t miss_time_us;
	uint64_t max_miss_time_us;
	int32_t last_miss_block;
} cache_io_profile_t;

static cache_io_profile_t crom_io_profile;
static cache_io_profile_t pcm_io_profile;

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
}

static void cache_io_profile_miss(cache_io_profile_t *profile, int block)
{
	profile->misses++;
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
	profile->miss_time_us += elapsed;
	if (elapsed > profile->max_miss_time_us)
		profile->max_miss_time_us = elapsed;
}

static void cache_io_profile_print(const char *name, const cache_io_profile_t *profile)
{
	uint64_t accesses = profile->hits + profile->misses;
	uint64_t avg_us = profile->misses ? profile->miss_time_us / profile->misses : 0;
	uint64_t rate_x100 = accesses ? (profile->hits * 10000) / accesses : 0;

	printf("[cache-io] %s preload_reads=%llu preload_seeks=%llu "
		"preload_seek_skips=%llu preload_bytes=%llu preload_time_us=%llu "
		"hits=%llu misses=%llu hit_rate=%llu.%02llu%% "
		"sequential_misses=%llu seeks=%llu seek_skips=%llu bytes=%llu "
		"avg_miss_us=%llu max_miss_us=%llu\n",
		name,
		(unsigned long long)profile->preload_reads,
		(unsigned long long)profile->preload_seeks,
		(unsigned long long)profile->preload_seek_skips,
		(unsigned long long)profile->preload_bytes,
		(unsigned long long)profile->preload_time_us,
		(unsigned long long)profile->hits,
		(unsigned long long)profile->misses,
		(unsigned long long)(rate_x100 / 100),
		(unsigned long long)(rate_x100 % 100),
		(unsigned long long)profile->sequential_misses,
		(unsigned long long)profile->seeks,
		(unsigned long long)profile->seek_skips,
		(unsigned long long)profile->bytes_read,
		(unsigned long long)avg_us,
		(unsigned long long)profile->max_miss_time_us);
}

static void cache_io_profile_snapshot(const cache_io_profile_t *profile)
{
	if (profile->misses == 0 || (profile->misses & 15) != 0)
		return;

	cache_io_profile_print(profile == &pcm_io_profile ? "pcm" : "crom", profile);
}
#endif

static int mvs_cache_read_block(int fd, int64_t *known_pos, uint16_t block,
	uint8_t *dst
#ifdef CACHE_IO_PROFILE
	, cache_io_profile_t *profile, int runtime_miss
#endif
)
{
	const int64_t offset = (int64_t)block << BLOCK_SHIFT;
	ssize_t bytes;
#ifdef CACHE_IO_PROFILE
	uint64_t start = cache_io_now_us();
#endif

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

	bytes = read(fd, dst, CACHE_BLOCK_SIZE);
	if (bytes == CACHE_BLOCK_SIZE)
		*known_pos = offset + CACHE_BLOCK_SIZE;
	else
		*known_pos = -1;

#ifdef CACHE_IO_PROFILE
	if (runtime_miss)
	{
		if (bytes > 0)
			profile->bytes_read += (uint64_t)bytes;
		cache_io_profile_time(profile, start);
		cache_io_profile_snapshot(profile);
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

	return bytes == CACHE_BLOCK_SIZE;
}
#endif


/******************************************************************************
	Local Functions
******************************************************************************/

#if (EMU_SYSTEM == MVS)

/*------------------------------------------------------
	Read PCM Cache
------------------------------------------------------*/

uint8_t *pcm_cache_read(uint16_t new_block)
{
	uint32_t idx = pcm_blocks[new_block];
	cache_t *p;

	if (idx == BLOCK_NOT_CACHED)
	{
#ifdef CACHE_IO_PROFILE
		cache_io_profile_miss(&pcm_io_profile, new_block);
#endif
		p = pcm_head;
		pcm_blocks[p->block] = BLOCK_NOT_CACHED;

		p->block = new_block;
		pcm_blocks[new_block] = p->idx;

		mvs_cache_read_block(pcm_fd, &pcm_file_pos, new_block,
			&memory_region_sound1[p->idx << BLOCK_SHIFT]
#ifdef CACHE_IO_PROFILE
			, &pcm_io_profile, 1
#endif
		);
	}
	else
	{
#ifdef CACHE_IO_PROFILE
		pcm_io_profile.hits++;
#endif
		p = &pcm_data[idx];
	}

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

	return &memory_region_sound1[p->idx << BLOCK_SHIFT];
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

	cache_fd = zopen(fname);

	return cache_fd != -1;
}


/*------------------------------------------------------
	Read Data File from ZIP Cache File
------------------------------------------------------*/

#define zip_cache_load(offs)									\
	zread(cache_fd, &GFX_MEMORY[offs << 16], CACHE_BLOCK_SIZE);	\
	zclose(cache_fd);											\
	cache_fd = -1;


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

	sprintf(fname, "%s/%c%c%c", spr_cache_name,
		cnv_table[(number >> 8) & 0x0f],
		cnv_table[(number >> 4) & 0x0f],
		cnv_table[ number       & 0x0f]);

	cache_fd = open(fname, O_RDONLY, 0777);

	return cache_fd != -1;
}


/*------------------------------------------------------
	Read Data File from Folder Cache
------------------------------------------------------*/

#define folder_cache_load(offs)									\
	read(cache_fd, &GFX_MEMORY[offs << 16], CACHE_BLOCK_SIZE);	\
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
			p->block = block;
			blocks[block] = p->idx;

			mvs_cache_read_block((int32_t)cache_fd, &cache_file_pos, block,
				&GFX_MEMORY[p->idx << BLOCK_SHIFT]
#ifdef CACHE_IO_PROFILE
				, &crom_io_profile, 0
#endif
			);

			head = p->next;
			head->prev = NULL;

			p->prev = tail;
			p->next = NULL;

			tail->next = p;
			tail = p;
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
				zip_cache_load(p->idx)
			} else {
				folder_cache_load(p->idx)
			}

			head = p->next;
			head->prev = NULL;

			p->prev = tail;
			p->next = NULL;

			tail->next = p;
			tail = p;
			i++;

			if (++block >= MAX_CACHE_BLOCKS)
				break;
		}
	}
	if (pcm_cache_enable)
	{
		i = 0;
		block = 0;

		while (i < MAX_PCM_SIZE)
		{
			p = pcm_head;
			p->block = block;
			pcm_blocks[block] = p->idx;

			mvs_cache_read_block(pcm_fd, &pcm_file_pos, block,
				&memory_region_sound1[p->idx << BLOCK_SHIFT]
#ifdef CACHE_IO_PROFILE
				, &pcm_io_profile, 0
#endif
			);

			pcm_head = p->next;
			pcm_head->prev = NULL;

			p->prev = pcm_tail;
			p->next = NULL;

			pcm_tail->next = p;
			pcm_tail = p;
			i++;
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

				lseek(cache_fd, block_offset[block], SEEK_SET);
				read(cache_fd, &GFX_MEMORY[p->idx << BLOCK_SHIFT], CACHE_BLOCK_SIZE);

				head = p->next;
				head->prev = NULL;

				p->prev = tail;
				p->next = NULL;

				tail->next = p;
				tail = p;
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
					zip_cache_load(p->idx)
				} else {
					folder_cache_load(p->idx)
				}

				head = p->next;
				head->prev = NULL;

				p->prev = tail;
				p->next = NULL;

				tail->next = p;
				tail = p;
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

	if (idx == BLOCK_NOT_CACHED)
	{
		p = head;
		blocks[p->block] = BLOCK_NOT_CACHED;

		p->block = new_block;
		blocks[new_block] = p->idx;

#if (EMU_SYSTEM == MVS)
	#ifdef CACHE_IO_PROFILE
		cache_io_profile_miss(&crom_io_profile, new_block);
	#endif
		mvs_cache_read_block((int32_t)cache_fd, &cache_file_pos, new_block,
			&GFX_MEMORY[p->idx << BLOCK_SHIFT]
	#ifdef CACHE_IO_PROFILE
			, &crom_io_profile, 1
	#endif
		);
#else
		lseek((int32_t)cache_fd, block_offset[new_block], SEEK_SET);
		read((int32_t)cache_fd, &GFX_MEMORY[p->idx << BLOCK_SHIFT], CACHE_BLOCK_SIZE);
	#endif
	}
	else
	{
	#if (EMU_SYSTEM == MVS) && defined(CACHE_IO_PROFILE)
		crom_io_profile.hits++;
	#endif
		p = &cache_data[idx];
	}

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

		zip_cache_load(p->idx);
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

	num_cache = 0;
	cache_fd = -1;

#if (EMU_SYSTEM == MVS)
	cache_type = CACHE_NOTFOUND;
	read_cache = NULL;
#else
	cache_type = CACHE_NOTFOUND;
	read_cache = read_cache_static;
#endif
	update_cache = NULL;

	for (i = 0; i < MAX_CACHE_BLOCKS; i++)
		blocks[i] = BLOCK_NOT_CACHED;

#if (EMU_SYSTEM == MVS)
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

int cache_start(void)
{
	int i, found;
	uint32_t size = 0;
	char version_str[8] = {0};
#if (EMU_SYSTEM == MVS)
	int32_t fd;
#endif

	zip_close();

#if (EMU_SYSTEM == MVS)

	msg_printf(TEXT(LOADING_CACHE_INFORMATION_DATA));

	found = 0;

	/* Try folder format first: {game}_cache/cache_info */
	if ((fd = cachefile_open(CACHE_INFO)) >= 0)
	{
		cache_type = CACHE_RAWFILE;
		read(fd, version_str, 8);

		if (strcmp(version_str, "MVS_" CACHE_VERSION) == 0)
		{
			read(fd, gfx_pen_usage[2], memory_length_gfx3 / 128);
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
			sprintf(spr_cache_name, "%s/%s_cache.zip", cache_dir, parent_name);
			if (zip_open(spr_cache_name) != -1)
			{
				found = 1;
			}
		}

		if (!found)
		{
			sprintf(spr_cache_name, "%s/%s_cache.zip", cache_dir, game_name);
			if (zip_open(spr_cache_name) != -1)
				found = 1;
		}

		if (found)
		{
			if ((cache_fd = zopen("cache_info")) != -1)
			{
				memset(version_str, 0, 8);
				zread(cache_fd, version_str, 8);

				if (strcmp(version_str, "MVS_" CACHE_VERSION) == 0)
				{
					zread(cache_fd, gfx_pen_usage[2], memory_length_gfx3 / 128);
					zclose(cache_fd);
				}
				else
				{
					zclose(cache_fd);
					found = 0;
				}
			}
			else
			{
				found = 0;
			}
			if (!found)
			{
				zip_close();
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
		if (option_sound_enable && disable_sound)
		{
			if ((pcm_fd = cachefile_open(CACHE_VROM)) >= 0)
			{
				pcm_file_pos = 0;
				if ((memory_region_sound1 = malloc(MAX_PCM_SIZE * CACHE_BLOCK_SIZE)) != NULL)
				{
					pcm_cache_enable = 1;
					disable_sound = 0;
					msg_printf(TEXT(PCM_CACHE_ENABLED));
				}
			}
			if (!pcm_cache_enable)
			{
				if (pcm_fd >= 0)
				{
					close(pcm_fd);
					pcm_fd = -1;
				}
				memory_length_sound1 = 0;
			}
		}
	}

	/* Open crom for block access (folder format only) */
	if (cache_type == CACHE_RAWFILE)
	{
		if ((cache_fd = cachefile_open(CACHE_CROM)) < 0)
		{
			msg_printf(TEXT(COULD_NOT_OPEN_CACHE_FILE));
			return 0;
		}
		cache_file_pos = 0;
	}
	/* For zip format, blocks will be accessed via zip_cache_open on demand */

#elif (EMU_SYSTEM == CPS2)
	found = 1;
	cache_type = CACHE_RAWFILE;

	/* Try raw file format: {game}.cache */
	sprintf(spr_cache_name, "%s/%s.cache", cache_dir, game_name);
	if ((cache_fd = open(spr_cache_name, O_RDONLY, 0777)) < 0)
	{
		sprintf(spr_cache_name, "%s/%s.cache", cache_dir, cache_parent_name);
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

		sprintf(spr_cache_name, "%s/%s_cache.zip", cache_dir, game_name);
		if (zip_open(spr_cache_name) == -1)
		{
			sprintf(spr_cache_name, "%s/%s_cache.zip", cache_dir, cache_parent_name);
			if (zip_open(spr_cache_name) == -1)
			{
				found = 0;
				zip_close();
			}
		}
	}

	/* Try folder format: {game}_cache/cache_info */
	if (!found)
	{
		char path[PATH_MAX];
		found = 1;
		cache_type = CACHE_FOLDER;

		sprintf(spr_cache_name, "%s/%s_cache", cache_dir, game_name);
		sprintf(path, "%s/cache_info", spr_cache_name);
		if ((cache_fd = open(path, O_RDONLY, 0777)) < 0)
		{
			sprintf(spr_cache_name, "%s/%s_cache", cache_dir, cache_parent_name);
			sprintf(path, "%s/cache_info", spr_cache_name);
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
		read(cache_fd, version_str, 8);

		if (strcmp(version_str, "CPS2" CACHE_VERSION) == 0)
		{
			read(cache_fd, gfx_pen_usage[TILE08], gfx_total_elements[TILE08]);
			read(cache_fd, gfx_pen_usage[TILE16], gfx_total_elements[TILE16]);
			read(cache_fd, gfx_pen_usage[TILE32], gfx_total_elements[TILE32]);
			read(cache_fd, block_offset, MAX_CACHE_BLOCKS * sizeof(uint32_t));
		}
		else
		{
			close(cache_fd);
			found = 0;
		}
	}
	else if (cache_type == CACHE_ZIPFILE)
	{
		if ((cache_fd = zopen("cache_info")) != -1)
		{
			zread(cache_fd, version_str, 8);

			if (strcmp(version_str, "CPS2" CACHE_VERSION) == 0)
			{
				zread(cache_fd, gfx_pen_usage[TILE08], gfx_total_elements[TILE08]);
				zread(cache_fd, gfx_pen_usage[TILE16], gfx_total_elements[TILE16]);
				zread(cache_fd, gfx_pen_usage[TILE32], gfx_total_elements[TILE32]);
				zread(cache_fd, block_empty, MAX_CACHE_BLOCKS);
				zclose(cache_fd);
			}
			else
			{
				zclose(cache_fd);
				found = 0;
			}
		}
		else
		{
			found = 0;
		}
		if (!found) zip_close();
	}
	else /* CACHE_FOLDER */
	{
		/* cache_fd is already open from the folder detection above */
		read(cache_fd, version_str, 8);

		if (strcmp(version_str, "CPS2" CACHE_VERSION) == 0)
		{
			read(cache_fd, gfx_pen_usage[TILE08], gfx_total_elements[TILE08]);
			read(cache_fd, gfx_pen_usage[TILE16], gfx_total_elements[TILE16]);
			read(cache_fd, gfx_pen_usage[TILE32], gfx_total_elements[TILE32]);
			read(cache_fd, block_empty, MAX_CACHE_BLOCKS);
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

	if ((GFX_MEMORY = (uint8_t *)malloc(GFX_SIZE + CACHE_SAFETY)) != NULL)
	{
		free(GFX_MEMORY);
		GFX_MEMORY = (uint8_t *)malloc(GFX_SIZE);
		memset(GFX_MEMORY, 0, GFX_SIZE);

		num_cache = GFX_SIZE >> 16;
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

		// Check allocatable size

		{
			/* Translate profile MB bounds to cache blocks, clamped to the
			 * compile-time MIN/MAX (which size the static cache_data[] array
			 * and the malloc-probe envelope).
			 */
			const memory_profile_t *profile = memory_profile_current();
			int profile_max_blocks = MAX_CACHE_SIZE;
			int profile_min_blocks = MIN_CACHE_SIZE;
			if (profile != NULL) {
				int p_max = (int)((profile->cache_max_mb << 20) >> BLOCK_SHIFT);
				int p_min = (int)((profile->cache_min_mb << 20) >> BLOCK_SHIFT);
				if (p_max > 0 && p_max < profile_max_blocks) profile_max_blocks = p_max;
				if (p_min > profile_min_blocks)              profile_min_blocks = p_min;
			}

#ifdef LARGE_MEMORY
			if ((profile == NULL || profile->use_psp2k_region)
			    && psp2k_mem_left == PSP2K_MEM_SIZE)//ui32 bug
			{
				GFX_MEMORY = (uint8_t *)PSP2K_MEM_TOP;
				i = profile_max_blocks;
				size = i << BLOCK_SHIFT;
			}
			else
#endif
			{
				for (i = MIN(GFX_SIZE >> BLOCK_SHIFT, profile_max_blocks); i >= profile_min_blocks; i--)
				{
					if ((GFX_MEMORY = (uint8_t *)malloc((i << BLOCK_SHIFT) + CACHE_SAFETY)) != NULL)
					{
						size = i << BLOCK_SHIFT;
						free(GFX_MEMORY);
						GFX_MEMORY = NULL;
						break;
					}
				}

				if (i < profile_min_blocks)
				{
					msg_printf(TEXT(MEMORY_NOT_ENOUGH));
					return 0;
				}

				if ((GFX_MEMORY = (uint8_t *)malloc(size)) == NULL)
				{
					msg_printf(TEXT(COULD_NOT_ALLOCATE_CACHE_MEMORY));
					return 0;
				}
			}
		}

		memset(GFX_MEMORY, 0, size);

		num_cache = i;
	}

	msg_printf(TEXT(xKB_CACHE_ALLOCATED), (num_cache << BLOCK_SHIFT) / 1024);

	for (i = 0; i < num_cache; i++)
		cache_data[i].idx = i;

	for (i = 1; i < num_cache; i++)
		cache_data[i].prev = &cache_data[i - 1];

	for (i = 0; i < num_cache - 1; i++)
		cache_data[i].next = &cache_data[i + 1];

	cache_data[0].prev = NULL;
	cache_data[num_cache - 1].next = NULL;

	head = &cache_data[0];
	tail = &cache_data[num_cache - 1];

#if (EMU_SYSTEM == MVS)
	for (i = 0; i < MAX_PCM_SIZE; i++)
		pcm_data[i].idx = i;

	for (i = 1; i < MAX_PCM_SIZE; i++)
		pcm_data[i].prev = &pcm_data[i - 1];

	for (i = 0; i < MAX_PCM_SIZE - 1; i++)
		pcm_data[i].next = &pcm_data[i + 1];

	pcm_data[0].prev = NULL;
	pcm_data[MAX_PCM_SIZE - 1].next = NULL;

	pcm_head = &pcm_data[0];
	pcm_tail = &pcm_data[MAX_PCM_SIZE - 1];
#endif

	if (!fill_cache())
	{
		msg_printf(TEXT(CACHE_LOAD_ERROR));
		pad_wait_press(PAD_WAIT_INFINITY);
		Loop = LOOP_BROWSER;
		return 0;
	}

	if (size == 0) cache_shutdown();

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
		zip_close();
	}
	/* CACHE_FOLDER: nothing to close (blocks opened/closed on demand) */

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
				close((int32_t)cache_fd);
#if (EMU_SYSTEM == MVS)
				cache_fd = -1;
				cache_file_pos = -1;
#endif
			}
			else if (cache_type == CACHE_ZIPFILE)
			{
				zip_close();
			}
#if (EMU_SYSTEM == MVS)
			if (pcm_cache_enable)
			{
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
				cache_fd = cachefile_open(CACHE_CROM);
				cache_file_pos = cache_fd >= 0 ? 0 : -1;
#else
				cache_fd = open(spr_cache_name, O_RDONLY, 0777);
#endif
			}
			else if (cache_type == CACHE_ZIPFILE)
			{
				zip_open(spr_cache_name);
			}
			/* CACHE_FOLDER: nothing to reopen */
#if (EMU_SYSTEM == MVS)
			if (pcm_cache_enable)
			{
				pcm_fd = cachefile_open(CACHE_VROM);
				pcm_file_pos = pcm_fd >= 0 ? 0 : -1;
			}
#endif
		}
	}
}


#ifdef STATE_SAVE

/*------------------------------------------------------
	Temporarily Allocate State Save Area
------------------------------------------------------*/

/* Phase 2b.6: cache_alloc_type is always declared. 1 = state-save was
 * staged into the PSP2K kernel region; 0 = staged into a file. The
 * !LARGE_MEMORY path always uses the file (use_psp2k=false keeps it 0).
 */
static int cache_alloc_type = 0;

uint8_t *cache_alloc_state_buffer(int32_t size)
{
	cache_alloc_type = 0;

#ifdef LARGE_MEMORY
	{
		const memory_profile_t *profile = memory_profile_current();
		if ((profile == NULL || profile->use_psp2k_region) &&
		    size < psp2k_mem_left)
		{
			cache_alloc_type = 1;
			return (uint8_t *)psp2k_mem_offset;
		}
	}
#endif
	{
		int32_t fd;
		char path[PATH_MAX];

		sprintf(path, "%sstate/cache.tmp", launchDir);

		if ((fd = open(path, O_WRONLY|O_CREAT, 0777)) >= 0)
		{
			write(fd, GFX_MEMORY, size);
			close(fd);
			return GFX_MEMORY;
		}
		return NULL;
	}
}

/*------------------------------------------------------
	Free Save Area and Restore Backed Up Cache
------------------------------------------------------*/

void cache_free_state_buffer(int32_t size)
{
	if (!cache_alloc_type)
	{
		uint32_t fd;
		char path[PATH_MAX];

		sprintf(path, "%sstate/cache.tmp", launchDir);

		if ((fd = open(path, O_RDONLY, 0777)) >= 0)
		{
			read(fd, GFX_MEMORY, size);
			close(fd);
		}
		remove(path);
	}

	cache_alloc_type = 0;
}

#endif /* STATE_SAVE */

#endif /* USE_CACHE */
