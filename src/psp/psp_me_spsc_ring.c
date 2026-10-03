#include "psp_me_spsc_ring.h"

#define PSP_ME_SPSC_RING_SLOT_HEADER_SIZE 8u

typedef struct psp_me_spsc_ring_slot_header
{
	uint32_t sequence;
	uint32_t payload_size;
} psp_me_spsc_ring_slot_header_t;

static uint32_t align_up_cache_line(uint32_t value)
{
	if (value > UINT32_MAX - (PSP_ME_SPSC_RING_CACHE_LINE - 1u))
		return 0;
	return (value + PSP_ME_SPSC_RING_CACHE_LINE - 1u) &
		~(PSP_ME_SPSC_RING_CACHE_LINE - 1u);
}

static bool is_power_of_two(uint32_t value)
{
	return value != 0 && (value & (value - 1u)) == 0;
}

static void memory_barrier(void)
{
	__sync_synchronize();
}

static void cache_publish(const psp_me_spsc_ring_cache_ops_t *cache_ops,
	void *address, uint32_t size)
{
	memory_barrier();
	if (cache_ops && cache_ops->publish)
		cache_ops->publish(address, size, cache_ops->opaque);
	memory_barrier();
}

static void cache_acquire(const psp_me_spsc_ring_cache_ops_t *cache_ops,
	void *address, uint32_t size)
{
	if (cache_ops && cache_ops->acquire)
		cache_ops->acquire(address, size, cache_ops->opaque);
	memory_barrier();
}

static void byte_copy(void *dst, const void *src, uint32_t size)
{
	uint8_t *out = (uint8_t *)dst;
	const uint8_t *in = (const uint8_t *)src;
	uint32_t i;

	for (i = 0; i < size; i++)
		out[i] = in[i];
}

static void byte_zero(void *dst, uint32_t size)
{
	uint8_t *out = (uint8_t *)dst;
	uint32_t i;

	for (i = 0; i < size; i++)
		out[i] = 0;
}

static void increment_saturating(uint32_t *value)
{
	if (*value != UINT32_MAX)
		(*value)++;
}

static uint8_t *slot_address(psp_me_spsc_ring_t *ring, uint32_t sequence)
{
	return ring->slots + ((sequence & ring->config.mask) * ring->config.slot_stride);
}

size_t psp_me_spsc_ring_storage_size(uint32_t capacity, uint32_t payload_size)
{
	uint32_t slot_stride;
	uint64_t total;

	if (!is_power_of_two(capacity) || payload_size == 0)
		return 0;
	if (payload_size > UINT32_MAX - PSP_ME_SPSC_RING_SLOT_HEADER_SIZE)
		return 0;

	slot_stride = align_up_cache_line(PSP_ME_SPSC_RING_SLOT_HEADER_SIZE + payload_size);
	if (slot_stride == 0)
		return 0;

	total = (uint64_t)offsetof(psp_me_spsc_ring_t, slots) +
		(uint64_t)capacity * slot_stride;
	if (total > UINT32_MAX || total > SIZE_MAX)
		return 0;
	return (size_t)total;
}

bool psp_me_spsc_ring_init(psp_me_spsc_ring_t *ring, size_t storage_size,
	uint32_t capacity, uint32_t payload_size)
{
	size_t required;
	uint32_t slot_stride;

	if (!ring || ((uintptr_t)ring & (PSP_ME_SPSC_RING_CACHE_LINE - 1u)) != 0)
		return false;

	required = psp_me_spsc_ring_storage_size(capacity, payload_size);
	if (required == 0 || storage_size < required)
		return false;

	slot_stride = align_up_cache_line(PSP_ME_SPSC_RING_SLOT_HEADER_SIZE + payload_size);
	byte_zero(ring, (uint32_t)required);
	ring->config.magic = PSP_ME_SPSC_RING_MAGIC;
	ring->config.version = PSP_ME_SPSC_RING_VERSION;
	ring->config.capacity = capacity;
	ring->config.mask = capacity - 1u;
	ring->config.payload_size = payload_size;
	ring->config.slot_stride = slot_stride;
	ring->config.storage_size = (uint32_t)required;
	return true;
}

bool psp_me_spsc_ring_is_valid(const psp_me_spsc_ring_t *ring)
{
	size_t expected;

	if (!ring || ((uintptr_t)ring & (PSP_ME_SPSC_RING_CACHE_LINE - 1u)) != 0)
		return false;
	if (ring->config.magic != PSP_ME_SPSC_RING_MAGIC ||
		ring->config.version != PSP_ME_SPSC_RING_VERSION ||
		!is_power_of_two(ring->config.capacity) ||
		ring->config.mask != ring->config.capacity - 1u ||
		ring->config.payload_size == 0 ||
		(ring->config.slot_stride & (PSP_ME_SPSC_RING_CACHE_LINE - 1u)) != 0)
		return false;

	expected = psp_me_spsc_ring_storage_size(ring->config.capacity,
		ring->config.payload_size);
	return expected != 0 && ring->config.storage_size == expected &&
		ring->config.slot_stride ==
			align_up_cache_line(PSP_ME_SPSC_RING_SLOT_HEADER_SIZE +
				ring->config.payload_size);
}

void psp_me_spsc_ring_publish_initial(psp_me_spsc_ring_t *ring,
	const psp_me_spsc_ring_cache_ops_t *cache_ops)
{
	if (!psp_me_spsc_ring_is_valid(ring))
		return;
	cache_publish(cache_ops, ring, ring->config.storage_size);
}

void psp_me_spsc_ring_acquire_initial(psp_me_spsc_ring_t *ring,
	const psp_me_spsc_ring_cache_ops_t *cache_ops)
{
	if (!ring)
		return;
	cache_acquire(cache_ops, ring, 3u * PSP_ME_SPSC_RING_CACHE_LINE);
}

psp_me_spsc_ring_result_t psp_me_spsc_ring_try_push(psp_me_spsc_ring_t *ring,
	const psp_me_spsc_ring_cache_ops_t *cache_ops, const void *payload,
	uint32_t *sequence_out)
{
	psp_me_spsc_ring_slot_header_t *slot;
	uint32_t write_sequence;
	uint32_t read_sequence;
	uint32_t used;
	uint8_t *slot_bytes;

	if (!payload || !psp_me_spsc_ring_is_valid(ring))
		return PSP_ME_SPSC_RING_INVALID;

	cache_acquire(cache_ops, &ring->consumer, PSP_ME_SPSC_RING_CACHE_LINE);
	write_sequence = ring->producer.write_sequence;
	read_sequence = ring->consumer.read_sequence;
	used = write_sequence - read_sequence;
	if (used > ring->config.capacity)
	{
		increment_saturating(&ring->producer.corrupt_count);
		cache_publish(cache_ops, &ring->producer, PSP_ME_SPSC_RING_CACHE_LINE);
		return PSP_ME_SPSC_RING_CORRUPT;
	}
	if (used == ring->config.capacity)
	{
		increment_saturating(&ring->producer.overflow_count);
		cache_publish(cache_ops, &ring->producer, PSP_ME_SPSC_RING_CACHE_LINE);
		return PSP_ME_SPSC_RING_FULL;
	}

	slot_bytes = slot_address(ring, write_sequence);
	slot = (psp_me_spsc_ring_slot_header_t *)slot_bytes;
	slot->sequence = write_sequence;
	slot->payload_size = ring->config.payload_size;
	byte_copy(slot_bytes + PSP_ME_SPSC_RING_SLOT_HEADER_SIZE, payload,
		ring->config.payload_size);
	cache_publish(cache_ops, slot_bytes, ring->config.slot_stride);

	ring->producer.write_sequence = write_sequence + 1u;
	if (used + 1u > ring->producer.high_water)
		ring->producer.high_water = used + 1u;
	cache_publish(cache_ops, &ring->producer, PSP_ME_SPSC_RING_CACHE_LINE);

	if (sequence_out)
		*sequence_out = write_sequence;
	return PSP_ME_SPSC_RING_OK;
}

psp_me_spsc_ring_result_t psp_me_spsc_ring_try_pop(psp_me_spsc_ring_t *ring,
	const psp_me_spsc_ring_cache_ops_t *cache_ops, void *payload,
	uint32_t *sequence_out)
{
	psp_me_spsc_ring_slot_header_t *slot;
	uint32_t write_sequence;
	uint32_t read_sequence;
	uint32_t available;
	uint8_t *slot_bytes;

	if (!payload || !psp_me_spsc_ring_is_valid(ring))
		return PSP_ME_SPSC_RING_INVALID;

	cache_acquire(cache_ops, &ring->producer, PSP_ME_SPSC_RING_CACHE_LINE);
	write_sequence = ring->producer.write_sequence;
	read_sequence = ring->consumer.read_sequence;
	available = write_sequence - read_sequence;
	if (available > ring->config.capacity)
	{
		increment_saturating(&ring->consumer.corrupt_count);
		cache_publish(cache_ops, &ring->consumer, PSP_ME_SPSC_RING_CACHE_LINE);
		return PSP_ME_SPSC_RING_CORRUPT;
	}
	if (available == 0)
	{
		increment_saturating(&ring->consumer.underflow_count);
		cache_publish(cache_ops, &ring->consumer, PSP_ME_SPSC_RING_CACHE_LINE);
		return PSP_ME_SPSC_RING_EMPTY;
	}

	slot_bytes = slot_address(ring, read_sequence);
	cache_acquire(cache_ops, slot_bytes, ring->config.slot_stride);
	slot = (psp_me_spsc_ring_slot_header_t *)slot_bytes;
	if (slot->sequence != read_sequence ||
		slot->payload_size != ring->config.payload_size)
	{
		increment_saturating(&ring->consumer.sequence_error_count);
		cache_publish(cache_ops, &ring->consumer, PSP_ME_SPSC_RING_CACHE_LINE);
		return PSP_ME_SPSC_RING_SEQUENCE_MISMATCH;
	}

	byte_copy(payload, slot_bytes + PSP_ME_SPSC_RING_SLOT_HEADER_SIZE,
		ring->config.payload_size);
	memory_barrier();
	ring->consumer.read_sequence = read_sequence + 1u;
	cache_publish(cache_ops, &ring->consumer, PSP_ME_SPSC_RING_CACHE_LINE);

	if (sequence_out)
		*sequence_out = read_sequence;
	return PSP_ME_SPSC_RING_OK;
}
