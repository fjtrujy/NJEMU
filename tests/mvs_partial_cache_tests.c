#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BLOCK_SHIFT 16
#define BLOCK_MASK 65535
#define CACHE_BLOCK_SIZE 65536
#define CACHE_READ_QUARTER_SIZE 16384
#define MVS 1
#define EMU_SYSTEM MVS
#define BLOCK_NOT_CACHED 65535
#define GFX_MEMORY mem

static uint8_t mem[2 * CACHE_BLOCK_SIZE];
static uint8_t gfx_valid_parts[2];
static size_t demand_size = CACHE_READ_QUARTER_SIZE;
static uint16_t blocks[8];

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
static int cache_storage_handle;
static int reads;
static int fail;
static int64_t cache_file_pos;

static size_t cache_resolved_read_size(void)
{
	return demand_size;
}

static int mvs_cache_read_range(int fd, int *handle, int64_t *position,
	uint16_t block, uint8_t *dst, const char *name, unsigned int within,
	unsigned int size)
{
	(void)fd;
	(void)handle;
	(void)position;
	(void)name;
	assert(size == demand_size && within % demand_size == 0);
	reads++;
	if (fail)
		return 0;
	memset(dst, block * 4u + within / CACHE_READ_QUARTER_SIZE, size);
	return 1;
}

#include "common/cache_raw_read.inc"

static void reset_empty(void)
{
	memset(blocks, 0xff, sizeof(blocks));
	memset(gfx_valid_parts, 0, sizeof(gfx_valid_parts));
	cache_data[0] = (cache_t){0, -1, NULL, &cache_data[1]};
	cache_data[1] = (cache_t){1, -1, &cache_data[0], NULL};
	head = &cache_data[0];
	tail = &cache_data[1];
	reads = 0;
}

int main(void)
{
	uint32_t location;
	uint32_t rng;
	unsigned int i;

	memset(blocks, 0xff, sizeof(blocks));
	memset(mem, 0xa5, sizeof(mem));
	cache_data[0] = (cache_t){0, 0, NULL, &cache_data[1]};
	cache_data[1] = (cache_t){1, 1, &cache_data[0], NULL};
	blocks[0] = 0;
	blocks[1] = 1;
	head = &cache_data[0];
	tail = &cache_data[1];
	gfx_valid_parts[0] = gfx_valid_parts[1] = 15;

	location = read_cache_rawfile(2 * 65536 + 16384 + 128);
	assert(reads == 1 && mem[location] == 9 && gfx_valid_parts[0] == 2);
	assert(mem[0] == 0xa5 && mem[32768] == 0xa5);
	assert(read_cache_rawfile(2 * 65536 + 16384 + 256) == location + 128 && reads == 1);
	location = read_cache_rawfile(2 * 65536 + 49152);
	assert(reads == 2 && mem[location] == 11 && gfx_valid_parts[0] == 10);
	read_cache_rawfile(3 * 65536);
	assert(reads == 3);
	fail = 1;
	location = read_cache_rawfile(4 * 65536);
	assert(reads == 4 && mem[location] == 0 && gfx_valid_parts[0] == 0);
	fail = 0;
	location = read_cache_rawfile(4 * 65536);
	assert(reads == 5 && mem[location] == 16 && gfx_valid_parts[0] == 1);
	assert(blocks[2] == BLOCK_NOT_CACHED);

	demand_size = 32768;
	reset_empty();
	location = read_cache_rawfile(32768 + 128);
	assert(reads == 1 && gfx_valid_parts[0] == 12);
	read_cache_rawfile(49152);
	assert(reads == 1);

	demand_size = 65536;
	reset_empty();
	location = read_cache_rawfile(128);
	assert(reads == 1 && gfx_valid_parts[0] == 15);
	read_cache_rawfile(49152);
	assert(reads == 1);

	demand_size = 16384;
	rng = 12345;
	for (i = 0; i < 10000; ++i) {
		unsigned int address;
		unsigned int j;
		rng = rng * 1664525u + 1013904223u;
		address = ((rng >> 8) % (8 * 512)) * 128;
		location = read_cache_rawfile(address);
		for (j = 0; j < 128; ++j)
			assert(mem[location + j] == address / CACHE_READ_QUARTER_SIZE);
	}
	return 0;
}
