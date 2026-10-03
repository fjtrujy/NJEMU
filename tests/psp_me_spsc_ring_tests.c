#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psp/psp_me_spsc_ring.h"

#define TEST_CAPACITY 32u
#define STRESS_MESSAGES 1000000u

typedef struct test_payload
{
	uint32_t sequence;
	uint32_t inverse;
	uint32_t pattern_a;
	uint32_t pattern_b;
} test_payload_t;

typedef struct cache_tracker
{
	uint32_t publish_count;
	uint32_t acquire_count;
	uint32_t bad_range_count;
} cache_tracker_t;

typedef struct ring_allocation
{
	void *raw;
	psp_me_spsc_ring_t *ring;
	size_t size;
} ring_allocation_t;

static void track_cache_range(void *address, uint32_t size, uint32_t *count,
	cache_tracker_t *tracker)
{
	(*count)++;
	if (((uintptr_t)address & (PSP_ME_SPSC_RING_CACHE_LINE - 1u)) != 0 ||
		size == 0 || (size & (PSP_ME_SPSC_RING_CACHE_LINE - 1u)) != 0)
		tracker->bad_range_count++;
}

static void track_publish(void *address, uint32_t size, void *opaque)
{
	cache_tracker_t *tracker = (cache_tracker_t *)opaque;
	track_cache_range(address, size, &tracker->publish_count, tracker);
}

static void track_acquire(void *address, uint32_t size, void *opaque)
{
	cache_tracker_t *tracker = (cache_tracker_t *)opaque;
	track_cache_range(address, size, &tracker->acquire_count, tracker);
}

static psp_me_spsc_ring_cache_ops_t make_cache_ops(cache_tracker_t *tracker)
{
	psp_me_spsc_ring_cache_ops_t ops;

	ops.publish = track_publish;
	ops.acquire = track_acquire;
	ops.opaque = tracker;
	return ops;
}

static ring_allocation_t allocate_ring(uint32_t capacity, uint32_t payload_size)
{
	ring_allocation_t allocation = { 0 };
	uintptr_t aligned;

	allocation.size = psp_me_spsc_ring_storage_size(capacity, payload_size);
	if (allocation.size == 0)
		return allocation;
	allocation.raw = malloc(allocation.size + PSP_ME_SPSC_RING_CACHE_LINE - 1u);
	if (!allocation.raw)
		return allocation;
	aligned = ((uintptr_t)allocation.raw + PSP_ME_SPSC_RING_CACHE_LINE - 1u) &
		~(uintptr_t)(PSP_ME_SPSC_RING_CACHE_LINE - 1u);
	allocation.ring = (psp_me_spsc_ring_t *)aligned;
	if (!psp_me_spsc_ring_init(allocation.ring, allocation.size,
		capacity, payload_size))
	{
		free(allocation.raw);
		memset(&allocation, 0, sizeof(allocation));
	}
	return allocation;
}

static void free_ring(ring_allocation_t *allocation)
{
	free(allocation->raw);
	memset(allocation, 0, sizeof(*allocation));
}

static test_payload_t make_payload(uint32_t sequence)
{
	test_payload_t payload;

	payload.sequence = sequence;
	payload.inverse = ~sequence;
	payload.pattern_a = sequence * 1664525u + 1013904223u;
	payload.pattern_b = payload.pattern_a ^ 0xa5a55a5au;
	return payload;
}

static int payload_matches(const test_payload_t *payload, uint32_t sequence)
{
	test_payload_t expected = make_payload(sequence);
	return memcmp(payload, &expected, sizeof(expected)) == 0;
}

static int test_layout_and_validation(void)
{
	size_t size = psp_me_spsc_ring_storage_size(TEST_CAPACITY,
		sizeof(test_payload_t));
	void *raw;
	uintptr_t aligned;
	psp_me_spsc_ring_t *ring;

	if (sizeof(psp_me_spsc_ring_producer_state_t) != PSP_ME_SPSC_RING_CACHE_LINE ||
		sizeof(psp_me_spsc_ring_consumer_state_t) != PSP_ME_SPSC_RING_CACHE_LINE ||
		sizeof(psp_me_spsc_ring_config_t) != PSP_ME_SPSC_RING_CACHE_LINE ||
		offsetof(psp_me_spsc_ring_t, slots) != 3u * PSP_ME_SPSC_RING_CACHE_LINE)
	{
		fprintf(stderr, "ring cache-line layout mismatch\n");
		return 0;
	}
	if (size == 0 || (size & (PSP_ME_SPSC_RING_CACHE_LINE - 1u)) != 0 ||
		psp_me_spsc_ring_storage_size(0, sizeof(test_payload_t)) != 0 ||
		psp_me_spsc_ring_storage_size(3, sizeof(test_payload_t)) != 0 ||
		psp_me_spsc_ring_storage_size(4, 0) != 0)
	{
		fprintf(stderr, "ring size validation mismatch\n");
		return 0;
	}

	raw = malloc(size + PSP_ME_SPSC_RING_CACHE_LINE);
	if (!raw)
		return 0;
	aligned = ((uintptr_t)raw + PSP_ME_SPSC_RING_CACHE_LINE - 1u) &
		~(uintptr_t)(PSP_ME_SPSC_RING_CACHE_LINE - 1u);
	ring = (psp_me_spsc_ring_t *)aligned;
	if (psp_me_spsc_ring_init((psp_me_spsc_ring_t *)(aligned + 1u), size,
		TEST_CAPACITY, sizeof(test_payload_t)) ||
		psp_me_spsc_ring_init(ring, size - 1u, TEST_CAPACITY,
			sizeof(test_payload_t)) ||
		!psp_me_spsc_ring_init(ring, size, TEST_CAPACITY,
			sizeof(test_payload_t)) ||
		!psp_me_spsc_ring_is_valid(ring))
	{
		fprintf(stderr, "ring initialization validation mismatch\n");
		free(raw);
		return 0;
	}

	ring->config.magic ^= 1u;
	if (psp_me_spsc_ring_is_valid(ring))
	{
		fprintf(stderr, "corrupt ring config was accepted\n");
		free(raw);
		return 0;
	}
	free(raw);
	return 1;
}

static int test_full_empty_and_ownership(void)
{
	ring_allocation_t allocation = allocate_ring(TEST_CAPACITY,
		sizeof(test_payload_t));
	cache_tracker_t tracker = { 0 };
	psp_me_spsc_ring_cache_ops_t cache_ops = make_cache_ops(&tracker);
	psp_me_spsc_ring_consumer_state_t consumer_before;
	psp_me_spsc_ring_producer_state_t producer_before;
	uint32_t i;

	if (!allocation.ring)
		return 0;
	psp_me_spsc_ring_publish_initial(allocation.ring, &cache_ops);
	psp_me_spsc_ring_acquire_initial(allocation.ring, &cache_ops);

	{
		test_payload_t payload;
		if (psp_me_spsc_ring_try_pop(allocation.ring, &cache_ops, &payload, NULL) !=
			PSP_ME_SPSC_RING_EMPTY || allocation.ring->consumer.underflow_count != 1u)
		{
			fprintf(stderr, "empty ring was not detected\n");
			free_ring(&allocation);
			return 0;
		}
	}

	consumer_before = allocation.ring->consumer;
	for (i = 0; i < TEST_CAPACITY; i++)
	{
		test_payload_t payload = make_payload(i);
		uint32_t sequence = UINT32_MAX;
		if (psp_me_spsc_ring_try_push(allocation.ring, &cache_ops, &payload,
			&sequence) != PSP_ME_SPSC_RING_OK || sequence != i)
		{
			fprintf(stderr, "push failed at %u\n", i);
			free_ring(&allocation);
			return 0;
		}
	}
	if (memcmp(&consumer_before, &allocation.ring->consumer,
		sizeof(consumer_before)) != 0)
	{
		fprintf(stderr, "producer modified consumer-owned state\n");
		free_ring(&allocation);
		return 0;
	}
	if (allocation.ring->producer.high_water != TEST_CAPACITY)
	{
		fprintf(stderr, "high-water mark mismatch\n");
		free_ring(&allocation);
		return 0;
	}
	{
		test_payload_t payload = make_payload(TEST_CAPACITY);
		if (psp_me_spsc_ring_try_push(allocation.ring, &cache_ops, &payload, NULL) !=
			PSP_ME_SPSC_RING_FULL || allocation.ring->producer.overflow_count != 1u)
		{
			fprintf(stderr, "full ring was not detected\n");
			free_ring(&allocation);
			return 0;
		}
	}

	producer_before = allocation.ring->producer;
	for (i = 0; i < TEST_CAPACITY; i++)
	{
		test_payload_t payload;
		uint32_t sequence = UINT32_MAX;
		if (psp_me_spsc_ring_try_pop(allocation.ring, &cache_ops, &payload,
			&sequence) != PSP_ME_SPSC_RING_OK || sequence != i ||
			!payload_matches(&payload, i))
		{
			fprintf(stderr, "pop mismatch at %u\n", i);
			free_ring(&allocation);
			return 0;
		}
	}
	if (memcmp(&producer_before, &allocation.ring->producer,
		sizeof(producer_before)) != 0)
	{
		fprintf(stderr, "consumer modified producer-owned state\n");
		free_ring(&allocation);
		return 0;
	}
	if (tracker.bad_range_count != 0 || tracker.publish_count == 0 ||
		tracker.acquire_count == 0)
	{
		fprintf(stderr, "cache maintenance range validation failed\n");
		free_ring(&allocation);
		return 0;
	}

	free_ring(&allocation);
	return 1;
}

static int test_sequence_mismatch(void)
{
	ring_allocation_t allocation = allocate_ring(4u, sizeof(test_payload_t));
	test_payload_t input = make_payload(7u);
	test_payload_t output;
	uint32_t slot_stride;
	uint32_t *slot_header;

	if (!allocation.ring)
		return 0;
	if (psp_me_spsc_ring_try_push(allocation.ring, NULL, &input, NULL) !=
		PSP_ME_SPSC_RING_OK)
	{
		free_ring(&allocation);
		return 0;
	}

	slot_stride = allocation.ring->config.slot_stride;
	slot_header = (uint32_t *)(allocation.ring->slots +
		((allocation.ring->consumer.read_sequence & allocation.ring->config.mask) *
			slot_stride));
	slot_header[0]++;
	if (psp_me_spsc_ring_try_pop(allocation.ring, NULL, &output, NULL) !=
		PSP_ME_SPSC_RING_SEQUENCE_MISMATCH ||
		allocation.ring->consumer.sequence_error_count != 1u ||
		allocation.ring->consumer.read_sequence != 0u)
	{
		fprintf(stderr, "slot sequence corruption was not detected\n");
		free_ring(&allocation);
		return 0;
	}

	slot_header[0]--;
	if (psp_me_spsc_ring_try_pop(allocation.ring, NULL, &output, NULL) !=
		PSP_ME_SPSC_RING_OK || !payload_matches(&output, 7u))
	{
		fprintf(stderr, "ring did not recover after repaired sequence\n");
		free_ring(&allocation);
		return 0;
	}
	free_ring(&allocation);
	return 1;
}

static int test_sequence_wrap(void)
{
	ring_allocation_t allocation = allocate_ring(8u, sizeof(test_payload_t));
	uint32_t expected = UINT32_MAX - 3u;
	uint32_t i;

	if (!allocation.ring)
		return 0;
	allocation.ring->producer.write_sequence = expected;
	allocation.ring->consumer.read_sequence = expected;

	for (i = 0; i < 12u; i++)
	{
		test_payload_t input = make_payload(i + 100u);
		test_payload_t output;
		uint32_t produced;
		uint32_t consumed;

		if (psp_me_spsc_ring_try_push(allocation.ring, NULL, &input, &produced) !=
			PSP_ME_SPSC_RING_OK || produced != expected ||
			psp_me_spsc_ring_try_pop(allocation.ring, NULL, &output, &consumed) !=
			PSP_ME_SPSC_RING_OK || consumed != expected ||
			memcmp(&input, &output, sizeof(input)) != 0)
		{
			fprintf(stderr, "sequence wrap mismatch at iteration %u\n", i);
			free_ring(&allocation);
			return 0;
		}
		expected++;
	}
	free_ring(&allocation);
	return 1;
}

static int test_million_message_order(void)
{
	ring_allocation_t allocation = allocate_ring(TEST_CAPACITY,
		sizeof(test_payload_t));
	cache_tracker_t tracker = { 0 };
	psp_me_spsc_ring_cache_ops_t cache_ops = make_cache_ops(&tracker);
	uint32_t base = 0;

	if (!allocation.ring)
		return 0;
	while (base < STRESS_MESSAGES)
	{
		uint32_t count = STRESS_MESSAGES - base;
		uint32_t i;
		if (count > TEST_CAPACITY)
			count = TEST_CAPACITY;

		for (i = 0; i < count; i++)
		{
			test_payload_t payload = make_payload(base + i);
			uint32_t sequence;
			if (psp_me_spsc_ring_try_push(allocation.ring, &cache_ops, &payload,
				&sequence) != PSP_ME_SPSC_RING_OK || sequence != base + i)
			{
				fprintf(stderr, "stress push mismatch at %u\n", base + i);
				free_ring(&allocation);
				return 0;
			}
		}
		for (i = 0; i < count; i++)
		{
			test_payload_t payload;
			uint32_t sequence;
			if (psp_me_spsc_ring_try_pop(allocation.ring, &cache_ops, &payload,
				&sequence) != PSP_ME_SPSC_RING_OK || sequence != base + i ||
				!payload_matches(&payload, base + i))
			{
				fprintf(stderr, "stress pop mismatch at %u\n", base + i);
				free_ring(&allocation);
				return 0;
			}
		}
		base += count;
	}

	if (allocation.ring->producer.write_sequence != STRESS_MESSAGES ||
		allocation.ring->consumer.read_sequence != STRESS_MESSAGES ||
		allocation.ring->producer.high_water != TEST_CAPACITY ||
		tracker.bad_range_count != 0)
	{
		fprintf(stderr, "stress final state mismatch\n");
		free_ring(&allocation);
		return 0;
	}
	free_ring(&allocation);
	return 1;
}

int main(void)
{
	if (!test_layout_and_validation() ||
		!test_full_empty_and_ownership() ||
		!test_sequence_mismatch() ||
		!test_sequence_wrap() ||
		!test_million_message_order())
		return 1;

	printf("PSP ME SPSC ring: layout/full/empty/wrap/cache and 1,000,000 ordered messages passed\n");
	return 0;
}
