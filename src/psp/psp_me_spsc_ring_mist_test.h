#ifndef PSP_ME_SPSC_RING_MIST_TEST_H
#define PSP_ME_SPSC_RING_MIST_TEST_H

#include <stdbool.h>
#include <stdint.h>

#define PSP_ME_SPSC_RING_MIST_LATENCY_MESSAGES 4096u
#define PSP_ME_SPSC_RING_MIST_BULK_MESSAGES 1000000u

typedef bool (*psp_me_spsc_ring_mist_start_fn)(void (*task)(void *),
	void *data, uint32_t size, void *opaque);
typedef void (*psp_me_spsc_ring_mist_wait_fn)(void *opaque);

typedef struct psp_me_spsc_ring_mist_dispatch
{
	psp_me_spsc_ring_mist_start_fn start;
	psp_me_spsc_ring_mist_wait_fn wait;
	void *opaque;
} psp_me_spsc_ring_mist_dispatch_t;

typedef enum psp_me_spsc_ring_mist_error
{
	PSP_ME_SPSC_RING_MIST_ERROR_NONE = 0,
	PSP_ME_SPSC_RING_MIST_ERROR_ALLOCATION,
	PSP_ME_SPSC_RING_MIST_ERROR_DISPATCH,
	PSP_ME_SPSC_RING_MIST_ERROR_RING,
	PSP_ME_SPSC_RING_MIST_ERROR_PAYLOAD,
	PSP_ME_SPSC_RING_MIST_ERROR_ABORTED,
	PSP_ME_SPSC_RING_MIST_ERROR_TIMEOUT
} psp_me_spsc_ring_mist_error_t;

typedef struct psp_me_spsc_ring_mist_result
{
	uint64_t latency_us;
	uint64_t bulk_us;
	uint32_t completed;
	uint32_t error;
	uint32_t to_me_high_water;
	uint32_t to_me_overflow;
	uint32_t to_me_underflow;
	uint32_t to_me_sequence_errors;
	uint32_t to_me_corrupt;
	uint32_t to_main_high_water;
	uint32_t to_main_overflow;
	uint32_t to_main_underflow;
	uint32_t to_main_sequence_errors;
	uint32_t to_main_corrupt;
} psp_me_spsc_ring_mist_result_t;

bool psp_me_spsc_ring_mist_test_run(
	const psp_me_spsc_ring_mist_dispatch_t *dispatch,
	psp_me_spsc_ring_mist_result_t *result);

#endif /* PSP_ME_SPSC_RING_MIST_TEST_H */
