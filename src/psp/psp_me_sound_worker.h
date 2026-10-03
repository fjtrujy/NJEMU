#ifndef PSP_ME_SOUND_WORKER_H
#define PSP_ME_SOUND_WORKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "psp/psp_me_spsc_ring.h"

typedef bool (*psp_me_sound_worker_start_fn)(void (*task)(void *), void *data,
	uint32_t size, void *opaque);
typedef void (*psp_me_sound_worker_wait_fn)(void *opaque);

typedef struct psp_me_sound_worker_dispatch
{
	psp_me_sound_worker_start_fn start;
	psp_me_sound_worker_wait_fn wait;
	void *opaque;
} psp_me_sound_worker_dispatch_t;

typedef enum psp_me_sound_worker_command_type
{
	PSP_ME_SOUND_WORKER_COMMAND_RESET = 1,
	PSP_ME_SOUND_WORKER_COMMAND_SYNC,
	PSP_ME_SOUND_WORKER_COMMAND_SHUTDOWN,
	PSP_ME_SOUND_WORKER_COMMAND_SHADOW_SOUND
} psp_me_sound_worker_command_type_t;

typedef enum psp_me_sound_worker_event_type
{
	PSP_ME_SOUND_WORKER_EVENT_READY = 1,
	PSP_ME_SOUND_WORKER_EVENT_RESET_ACK,
	PSP_ME_SOUND_WORKER_EVENT_SYNC_ACK,
	PSP_ME_SOUND_WORKER_EVENT_SHUTDOWN_ACK,
	PSP_ME_SOUND_WORKER_EVENT_SHADOW_SOUND_ECHO,
	PSP_ME_SOUND_WORKER_EVENT_ERROR
} psp_me_sound_worker_event_type_t;

typedef enum psp_me_sound_worker_error
{
	PSP_ME_SOUND_WORKER_ERROR_NONE = 0,
	PSP_ME_SOUND_WORKER_ERROR_ALLOCATION,
	PSP_ME_SOUND_WORKER_ERROR_DISPATCH,
	PSP_ME_SOUND_WORKER_ERROR_RING,
	PSP_ME_SOUND_WORKER_ERROR_PROTOCOL,
	PSP_ME_SOUND_WORKER_ERROR_GENERATION,
	PSP_ME_SOUND_WORKER_ERROR_TIME_REGRESSION,
	PSP_ME_SOUND_WORKER_ERROR_TIMEOUT,
	PSP_ME_SOUND_WORKER_ERROR_ABORTED
} psp_me_sound_worker_error_t;

typedef struct psp_me_sound_worker_message
{
	uint32_t type;
	uint32_t generation;
	uint32_t token;
	uint32_t flags;
	uint64_t emulated_time;
	uint32_t value;
	uint32_t reserved;
} psp_me_sound_worker_message_t;

_Static_assert(sizeof(psp_me_sound_worker_message_t) == 32,
	"sound worker protocol message must remain fixed-size");

typedef struct psp_me_sound_worker_stats
{
	uint32_t generation;
	uint32_t commands_processed;
	uint32_t resets;
	uint32_t syncs;
	uint32_t shutdowns;
	uint32_t shadow_commands;
	uint32_t heartbeat;
	uint32_t fatal_error;
	uint32_t command_high_water;
	uint32_t command_overflow;
	uint32_t command_underflow;
	uint32_t event_high_water;
	uint32_t event_overflow;
	uint32_t event_underflow;
	uint32_t shadow_sent;
	uint32_t shadow_matched;
	uint32_t shadow_mismatches;
	uint32_t shadow_send_failures;
	uint32_t shadow_pending;
	uint32_t shadow_pending_high_water;
	uint64_t emulated_time;
} psp_me_sound_worker_stats_t;

#define PSP_ME_SOUND_WORKER_SHADOW_EXPECTED_CAPACITY 64u

_Static_assert((PSP_ME_SOUND_WORKER_SHADOW_EXPECTED_CAPACITY &
	(PSP_ME_SOUND_WORKER_SHADOW_EXPECTED_CAPACITY - 1u)) == 0,
	"shadow expectation capacity must remain a power of two");

typedef struct psp_me_sound_worker_shadow_expected
{
	uint32_t token;
	uint32_t generation;
	uint64_t emulated_time;
	uint32_t command;
} psp_me_sound_worker_shadow_expected_t;

typedef struct psp_me_sound_worker
{
	psp_me_sound_worker_dispatch_t dispatch;
	psp_me_spsc_ring_t *commands;
	psp_me_spsc_ring_t *events;
	void *shared_context;
	void *main_control;
	void *progress;
	size_t ring_size;
	uint32_t capacity;
	uint32_t next_token;
	uint32_t generation;
	psp_me_sound_worker_shadow_expected_t
		shadow_expected[PSP_ME_SOUND_WORKER_SHADOW_EXPECTED_CAPACITY];
	uint32_t shadow_expected_head;
	uint32_t shadow_expected_count;
	uint32_t shadow_sent;
	uint32_t shadow_matched;
	uint32_t shadow_mismatches;
	uint32_t shadow_send_failures;
	uint32_t shadow_pending_high_water;
	bool running;
	psp_me_sound_worker_stats_t last_stats;
} psp_me_sound_worker_t;

bool psp_me_sound_worker_start(psp_me_sound_worker_t *worker,
	const psp_me_sound_worker_dispatch_t *dispatch, uint32_t capacity,
	uint64_t timeout_us);
bool psp_me_sound_worker_reset(psp_me_sound_worker_t *worker,
	uint32_t generation, uint64_t timeout_us);
bool psp_me_sound_worker_sync(psp_me_sound_worker_t *worker,
	uint64_t emulated_time, uint64_t timeout_us);
bool psp_me_sound_worker_shadow_sound(psp_me_sound_worker_t *worker,
	uint8_t command, uint64_t emulated_time);
bool psp_me_sound_worker_poll(psp_me_sound_worker_t *worker);
bool psp_me_sound_worker_shutdown(psp_me_sound_worker_t *worker,
	uint64_t timeout_us);
void psp_me_sound_worker_abort(psp_me_sound_worker_t *worker);
void psp_me_sound_worker_get_stats(psp_me_sound_worker_t *worker,
	psp_me_sound_worker_stats_t *stats);

#endif /* PSP_ME_SOUND_WORKER_H */
