#ifndef PSP_ME_SPSC_RING_H
#define PSP_ME_SPSC_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PSP_ME_SPSC_RING_CACHE_LINE 64u
#define PSP_ME_SPSC_RING_MAGIC 0x52474d45u /* "EMGR" */
#define PSP_ME_SPSC_RING_VERSION 1u

typedef void (*psp_me_spsc_ring_cache_fn)(void *address, uint32_t size,
	void *opaque);

typedef struct psp_me_spsc_ring_cache_ops
{
	psp_me_spsc_ring_cache_fn publish;
	psp_me_spsc_ring_cache_fn acquire;
	void *opaque;
} psp_me_spsc_ring_cache_ops_t;

typedef enum psp_me_spsc_ring_result
{
	PSP_ME_SPSC_RING_OK = 0,
	PSP_ME_SPSC_RING_EMPTY,
	PSP_ME_SPSC_RING_FULL,
	PSP_ME_SPSC_RING_SEQUENCE_MISMATCH,
	PSP_ME_SPSC_RING_CORRUPT,
	PSP_ME_SPSC_RING_INVALID
} psp_me_spsc_ring_result_t;

typedef struct __attribute__((aligned(PSP_ME_SPSC_RING_CACHE_LINE)))
	psp_me_spsc_ring_producer_state
{
	uint32_t write_sequence;
	uint32_t overflow_count;
	uint32_t high_water;
	uint32_t corrupt_count;
	uint32_t reserved[12];
} psp_me_spsc_ring_producer_state_t;

typedef struct __attribute__((aligned(PSP_ME_SPSC_RING_CACHE_LINE)))
	psp_me_spsc_ring_consumer_state
{
	uint32_t read_sequence;
	uint32_t underflow_count;
	uint32_t sequence_error_count;
	uint32_t corrupt_count;
	uint32_t reserved[12];
} psp_me_spsc_ring_consumer_state_t;

typedef struct __attribute__((aligned(PSP_ME_SPSC_RING_CACHE_LINE)))
	psp_me_spsc_ring_config
{
	uint32_t magic;
	uint32_t version;
	uint32_t capacity;
	uint32_t mask;
	uint32_t payload_size;
	uint32_t slot_stride;
	uint32_t storage_size;
	uint32_t reserved[9];
} psp_me_spsc_ring_config_t;

typedef struct __attribute__((aligned(PSP_ME_SPSC_RING_CACHE_LINE)))
	psp_me_spsc_ring
{
	psp_me_spsc_ring_producer_state_t producer;
	psp_me_spsc_ring_consumer_state_t consumer;
	psp_me_spsc_ring_config_t config;
	uint8_t slots[];
} psp_me_spsc_ring_t;

_Static_assert(sizeof(psp_me_spsc_ring_producer_state_t) == PSP_ME_SPSC_RING_CACHE_LINE,
	"producer cursor must occupy one cache line");
_Static_assert(sizeof(psp_me_spsc_ring_consumer_state_t) == PSP_ME_SPSC_RING_CACHE_LINE,
	"consumer cursor must occupy one cache line");
_Static_assert(sizeof(psp_me_spsc_ring_config_t) == PSP_ME_SPSC_RING_CACHE_LINE,
	"ring config must occupy one cache line");
_Static_assert(offsetof(psp_me_spsc_ring_t, consumer) == PSP_ME_SPSC_RING_CACHE_LINE,
	"producer and consumer cursors must not share a cache line");
_Static_assert(offsetof(psp_me_spsc_ring_t, config) == 2u * PSP_ME_SPSC_RING_CACHE_LINE,
	"ring config must have its own cache line");
_Static_assert(offsetof(psp_me_spsc_ring_t, slots) == 3u * PSP_ME_SPSC_RING_CACHE_LINE,
	"ring slots must begin on a cache-line boundary");

size_t psp_me_spsc_ring_storage_size(uint32_t capacity, uint32_t payload_size);
bool psp_me_spsc_ring_init(psp_me_spsc_ring_t *ring, size_t storage_size,
	uint32_t capacity, uint32_t payload_size);
bool psp_me_spsc_ring_is_valid(const psp_me_spsc_ring_t *ring);
void psp_me_spsc_ring_publish_initial(psp_me_spsc_ring_t *ring,
	const psp_me_spsc_ring_cache_ops_t *cache_ops);
void psp_me_spsc_ring_acquire_initial(psp_me_spsc_ring_t *ring,
	const psp_me_spsc_ring_cache_ops_t *cache_ops);
psp_me_spsc_ring_result_t psp_me_spsc_ring_try_push(psp_me_spsc_ring_t *ring,
	const psp_me_spsc_ring_cache_ops_t *cache_ops, const void *payload,
	uint32_t *sequence_out);
psp_me_spsc_ring_result_t psp_me_spsc_ring_try_pop(psp_me_spsc_ring_t *ring,
	const psp_me_spsc_ring_cache_ops_t *cache_ops, void *payload,
	uint32_t *sequence_out);

#endif /* PSP_ME_SPSC_RING_H */
