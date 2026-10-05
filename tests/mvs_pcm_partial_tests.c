#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PCM_CACHE_SHIFT 14
#define CACHE_BLOCK_SIZE 65536
#define CACHE_READ_QUARTER_SIZE 16384
#define BLOCK_SHIFT 16
#define BLOCK_NOT_CACHED 65535

static size_t demand_size = CACHE_READ_QUARTER_SIZE;
static uint8_t memory_region_sound1[2 * CACHE_BLOCK_SIZE];
static uint8_t pcm_valid_parts[2];
static uint16_t pcm_blocks[8];

typedef struct cache {
	int idx;
	int block;
	int frame;
	struct cache *prev;
	struct cache *next;
} cache_t;

static cache_t pcm_data[2];
static cache_t *pcm_head;
static cache_t *pcm_tail;
static int pcm_fd;
static int pcm_storage_handle;
static int frames_displayed = 1;
static int reads;
static int fail;
static int64_t pcm_file_pos;

static size_t cache_resolved_read_size(void)
{
	return demand_size;
}

static int mvs_cache_read_range(int fd, int *handle, int64_t *position,
	uint16_t block, uint8_t *dst, const char *name, unsigned int within,
	unsigned int size)
{
	unsigned int i;
	(void)fd;
	(void)handle;
	(void)position;
	(void)name;
	assert(size == demand_size && within % demand_size == 0);
	reads++;
	if (fail)
		return 0;
	for (i = 0; i < size; ++i)
		dst[i] = (uint8_t)(block * 4u + within / CACHE_READ_QUARTER_SIZE + i);
	return 1;
}

#include "common/cache_pcm_read.inc"

int main(void)
{
	uint8_t *data;
	uint32_t rng;
	unsigned int address;
	int i;

	memset(pcm_blocks, 0xff, sizeof(pcm_blocks));
	pcm_data[0] = (cache_t){0, 0, 0, NULL, &pcm_data[1]};
	pcm_data[1] = (cache_t){1, 1, 0, &pcm_data[0], NULL};
	pcm_blocks[0] = 0;
	pcm_blocks[1] = 1;
	pcm_head = &pcm_data[0];
	pcm_tail = &pcm_data[1];

	data = pcm_cache_read(9);
	assert(reads == 1 && data[0] == 9 && data[16383] == 8);
	assert(pcm_cache_read(9) == data && reads == 1);

	demand_size = 32768;
	data = pcm_cache_read(10);
	assert(reads == 2 && data[0] == 10);
	assert(pcm_cache_read(11) == data + 16384 && reads == 2);

	demand_size = 16384;
	data = pcm_cache_read(10);
	assert(reads == 2 && data[0] == 10);
	pcm_valid_parts[pcm_blocks[2]] &= (uint8_t)~8u;
	fail = 1;
	data = pcm_cache_read(11);
	assert(data[0] == 0 && data[16383] == 0);
	fail = 0;
	data = pcm_cache_read(11);
	assert(reads == 4 && data[0] == 11);

	rng = 123;
	for (i = 0; i < 10000; ++i) {
		unsigned int key;
		frames_displayed++;
		rng = rng * 1664525u + 1013904223u;
		key = (rng >> 16) % 32;
		data = pcm_cache_read((uint16_t)key);
		assert(data[0] == key && data[16383] == (uint8_t)(key + 16383));
	}

	for (address = 0; address < 8 * CACHE_BLOCK_SIZE; ++address) {
		if ((address & (CACHE_READ_QUARTER_SIZE - 1u)) == 0) {
			frames_displayed++;
			data = pcm_cache_read((uint16_t)(address >> PCM_CACHE_SHIFT));
		}
		assert(data[address & (CACHE_READ_QUARTER_SIZE - 1u)]
			== (uint8_t)((address >> PCM_CACHE_SHIFT)
				+ (address & (CACHE_READ_QUARTER_SIZE - 1u))));
	}
	return 0;
}
