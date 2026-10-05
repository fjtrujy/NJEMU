#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define CPS2 2
#define MVS 3
#define EMU_SYSTEM CPS2
#define BLOCK_SHIFT 16
#define BLOCK_MASK 65535
#define CACHE_BLOCK_SIZE 65536
#define CACHE_READ_QUARTER_SIZE 16384
#define BLOCK_NOT_CACHED 65535
#define GFX_MEMORY mem

static uint8_t mem[2 * CACHE_BLOCK_SIZE];
static uint8_t gfx_valid_parts[2];
static uint16_t blocks[8];
static uint32_t block_offset[8];

typedef struct cache {
	int idx;
	int block;
	struct cache *prev;
	struct cache *next;
} cache_t;

static cache_t cache_data[2];
static cache_t *head;
static cache_t *tail;
static int cache_fd;
static int reads;
static off_t pos;
static size_t demand_size = CACHE_READ_QUARTER_SIZE;

static size_t cache_resolved_read_size(void)
{
	return demand_size;
}

static off_t mock_lseek(int fd, off_t new_pos, int whence)
{
	(void)fd;
	(void)whence;
	pos = new_pos;
	return new_pos;
}

static ssize_t mock_read(int fd, void *dst, size_t size)
{
	size_t i;
	(void)fd;
	reads++;
	for (i = 0; i < size; ++i)
		((uint8_t *)dst)[i] = (uint8_t)((pos + (off_t)i) / CACHE_READ_QUARTER_SIZE);
	pos += (off_t)size;
	return (ssize_t)size;
}

#define lseek mock_lseek
#define read mock_read
#include "common/cache_raw_read.inc"
#undef read
#undef lseek

static void reset(void)
{
	int i;
	memset(blocks, 0xff, sizeof(blocks));
	memset(gfx_valid_parts, 0, sizeof(gfx_valid_parts));
	for (i = 0; i < 8; ++i)
		block_offset[i] = (uint32_t)i * CACHE_BLOCK_SIZE;
	cache_data[0] = (cache_t){0, -1, NULL, &cache_data[1]};
	cache_data[1] = (cache_t){1, -1, &cache_data[0], NULL};
	head = &cache_data[0];
	tail = &cache_data[1];
	reads = 0;
}

int main(void)
{
	uint32_t location;

	reset();
	demand_size = 16384;
	location = read_cache_rawfile(2 * 65536 + 16384 + 128);
	assert(reads == 1 && mem[location] == 9 && gfx_valid_parts[0] == 2);
	read_cache_rawfile(2 * 65536 + 16384 + 256);
	assert(reads == 1);

	reset();
	demand_size = 32768;
	location = read_cache_rawfile(32768 + 128);
	assert(reads == 1 && gfx_valid_parts[0] == 12 && mem[location] == 2);
	read_cache_rawfile(49152);
	assert(reads == 1);

	reset();
	demand_size = 65536;
	location = read_cache_rawfile(128);
	assert(reads == 1 && gfx_valid_parts[0] == 15 && mem[location] == 0);
	read_cache_rawfile(49152);
	assert(reads == 1);
	return 0;
}
