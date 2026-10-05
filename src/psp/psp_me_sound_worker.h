#ifndef PSP_ME_SOUND_WORKER_H
#define PSP_ME_SOUND_WORKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cpu/z80/cz80.h"
#include "psp/psp_me_spsc_ring.h"
#include "sound/ym2610.h"

#define PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE 0x10000u
#define PSP_ME_SOUND_Z80_RAM_OFFSET 0xf800u
#define PSP_ME_SOUND_Z80_RAM_SIZE 0x0800u
#define PSP_ME_SOUND_Z80_IO_CAPACITY 512u
#define PSP_ME_SOUND_Z80_BATCH_CAPACITY 4u
#define PSP_ME_SOUND_YM_RENDER_MAX_SAMPLES 1472u

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
	PSP_ME_SOUND_WORKER_COMMAND_SHADOW_SOUND,
	PSP_ME_SOUND_WORKER_COMMAND_Z80_SNAPSHOT,
	PSP_ME_SOUND_WORKER_COMMAND_Z80_IRQ,
	PSP_ME_SOUND_WORKER_COMMAND_Z80_SLICE,
	PSP_ME_SOUND_WORKER_COMMAND_YM_RENDER,
	PSP_ME_SOUND_WORKER_COMMAND_YM_TIMER,
	PSP_ME_SOUND_WORKER_COMMAND_YM_RENDER_PREPARE,
	PSP_ME_SOUND_WORKER_COMMAND_Z80_ADVANCE,
	PSP_ME_SOUND_WORKER_COMMAND_Z80_CHECKPOINT,
	PSP_ME_SOUND_WORKER_COMMAND_Z80_ADVANCE_HORIZON,
	PSP_ME_SOUND_WORKER_COMMAND_FENCE,
	PSP_ME_SOUND_WORKER_COMMAND_RECOVERY_SNAPSHOT
} psp_me_sound_worker_command_type_t;

typedef enum psp_me_sound_worker_event_type
{
	PSP_ME_SOUND_WORKER_EVENT_READY = 1,
	PSP_ME_SOUND_WORKER_EVENT_RESET_ACK,
	PSP_ME_SOUND_WORKER_EVENT_SYNC_ACK,
	PSP_ME_SOUND_WORKER_EVENT_SHUTDOWN_ACK,
	PSP_ME_SOUND_WORKER_EVENT_SHADOW_SOUND_ECHO,
	PSP_ME_SOUND_WORKER_EVENT_Z80_SNAPSHOT_ACK,
	PSP_ME_SOUND_WORKER_EVENT_YM_RENDER_ACK,
	PSP_ME_SOUND_WORKER_EVENT_ERROR,
	PSP_ME_SOUND_WORKER_EVENT_YM_RENDER_PREPARE_ACK,
	PSP_ME_SOUND_WORKER_EVENT_Z80_CHECKPOINT_ACK,
	PSP_ME_SOUND_WORKER_EVENT_FENCE_ACK,
	PSP_ME_SOUND_WORKER_EVENT_RECOVERY_SNAPSHOT_ACK
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
	PSP_ME_SOUND_WORKER_ERROR_Z80_TRACE,
	PSP_ME_SOUND_WORKER_ERROR_Z80_STATE,
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

enum
{
	PSP_ME_SOUND_WORKER_MESSAGE_NO_ECHO = 1u << 0
};

typedef enum psp_me_sound_z80_io_type
{
	PSP_ME_SOUND_Z80_IO_READ = 1,
	PSP_ME_SOUND_Z80_IO_WRITE,
	PSP_ME_SOUND_Z80_IO_IRQ,
	PSP_ME_SOUND_Z80_IO_PREEMPT
} psp_me_sound_z80_io_type_t;

typedef struct psp_me_sound_z80_io
{
	uint16_t port;
	uint8_t type;
	uint8_t value;
} psp_me_sound_z80_io_t;

_Static_assert(sizeof(psp_me_sound_z80_io_t) == 4,
	"Z80 I/O oracle entry must remain compact");

typedef enum psp_me_sound_z80_mode
{
	PSP_ME_SOUND_Z80_MODE_ORACLE = 0,
	PSP_ME_SOUND_Z80_MODE_AUTONOMOUS = 1
} psp_me_sound_z80_mode_t;

typedef struct psp_me_sound_z80_snapshot
{
	cz80_state_t state;
	uint64_t ym_timer_remaining[2];
	const uint8_t *source_rom;
	uint32_t source_length;
	uint32_t banks[4];
	uint32_t generation;
	uint8_t sound_code;
	uint8_t pending_command;
	uint8_t result_code;
	uint8_t mode;
	uint8_t ym_timer_enabled[2];
	uint8_t reserved8[2];
	uint32_t ym_sample_rate;
	uint32_t ym_pcm_a_size;
	uint32_t ym_pcm_b_size;
} psp_me_sound_z80_snapshot_t;

typedef struct psp_me_sound_status_snapshot
{
	uint32_t generation;
	uint32_t sequence;
	uint64_t emulated_time;
	uint64_t z80_time;
	uint8_t sound_code;
	uint8_t pending_command;
	uint8_t result_code;
	uint8_t irq_state;
	uint8_t initialized;
	uint8_t reserved8[3];
	uint32_t ym_timer_callbacks;
	uint32_t ym_timer_overflows;
	uint32_t last_advance_elapsed_us;
	uint32_t reserved[5];
} psp_me_sound_status_snapshot_t;

_Static_assert(sizeof(psp_me_sound_status_snapshot_t) == 64,
	"sound status snapshot must occupy exactly one cache line");

typedef struct psp_me_sound_recovery_snapshot
{
	cz80_state_t state;
	uint64_t emulated_time;
	uint64_t z80_time;
	uint64_t ym_timer_remaining[2];
	uint32_t banks[4];
	uint32_t generation;
	uint32_t sequence;
	uint32_t ym_timer_arm_elapsed[2];
	uint8_t sound_code;
	uint8_t pending_command;
	uint8_t result_code;
	uint8_t irq_state;
	uint8_t ym_timer_enabled[2];
	uint8_t initialized;
	uint8_t mode;
	uint32_t reserved[2];
} psp_me_sound_recovery_snapshot_t;

_Static_assert(sizeof(psp_me_sound_recovery_snapshot_t) == 128,
	"sound recovery snapshot must occupy exactly two cache lines");

typedef enum psp_me_sound_status_validation
{
	PSP_ME_SOUND_STATUS_UNAVAILABLE = 0,
	PSP_ME_SOUND_STATUS_STALE,
	PSP_ME_SOUND_STATUS_MATCH,
	PSP_ME_SOUND_STATUS_MISMATCH
} psp_me_sound_status_validation_t;

typedef enum psp_me_sound_fence_result
{
	PSP_ME_SOUND_FENCE_FAILED = -1,
	PSP_ME_SOUND_FENCE_PENDING = 0,
	PSP_ME_SOUND_FENCE_COMPLETE = 1
} psp_me_sound_fence_result_t;

typedef enum psp_me_sound_render_result
{
	PSP_ME_SOUND_RENDER_FAILED = -1,
	PSP_ME_SOUND_RENDER_PENDING = 0,
	PSP_ME_SOUND_RENDER_COMPLETE = 1
} psp_me_sound_render_result_t;

typedef struct psp_me_sound_z80_slice
{
	cz80_state_t expected_state;
	uint64_t emulated_time;
	uint32_t generation;
	uint32_t sequence;
	uint32_t cycles;
	uint32_t io_count;
	uint32_t banks[4];
	uint32_t ram_hash;
	uint32_t flags;
	psp_me_sound_z80_io_t io[PSP_ME_SOUND_Z80_IO_CAPACITY];
} psp_me_sound_z80_slice_t;

enum
{
	PSP_ME_SOUND_Z80_SLICE_CHECK_RAM = 1u << 0
};

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
	uint32_t last_command_type;
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
	uint32_t z80_snapshots;
	uint32_t z80_irqs;
	uint32_t z80_slices;
	uint32_t z80_io_events;
	uint32_t z80_state_mismatches;
	uint32_t z80_ram_mismatches;
	uint32_t z80_bank_mismatches;
	uint32_t z80_io_mismatches;
	uint32_t z80_send_failures;
	uint32_t z80_last_mismatch;
	uint32_t z80_batch_high_water;
	uint32_t z80_batch_overflow;
	uint32_t z80_batch_underflow;
	uint32_t z80_autonomous_slices;
	uint32_t z80_checkpoints;
	uint32_t ym_timer_callbacks;
	uint32_t ym_timer_overflows;
	uint32_t ym_renders;
	uint32_t ym_render_samples;
	uint32_t ym_render_errors;
	uint32_t ym_presented_renders;
	uint32_t ym_presented_samples;
	uint32_t ym_authoritative_renders;
	uint32_t ym_context_sync_failures;
	uint32_t ym_pcm_mismatches;
	uint32_t ym_status_mismatches;
	uint32_t ym_send_failures;
	uint32_t ym_first_pcm_mismatch_sample;
	uint32_t ym_first_pcm_mismatch_channel;
	int32_t ym_first_pcm_expected;
	int32_t ym_first_pcm_actual;
	uint64_t emulated_time;
	uint64_t fatal_emulated_time;
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
	psp_me_spsc_ring_t *z80_batches;
	void *shared_context;
	void *main_control;
	void *progress;
	void *z80_progress;
	void *ym_context;
	psp_me_sound_z80_snapshot_t *z80_snapshot;
	psp_me_sound_status_snapshot_t *status_snapshot;
	psp_me_sound_recovery_snapshot_t *recovery_snapshot;
	uint8_t *z80_memory;
	void *ym_render_job;
	size_t ring_size;
	size_t z80_batch_ring_size;
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
	uint32_t z80_next_sequence;
	uint32_t z80_send_failures;
	uint32_t ym_render_token;
	uint32_t ym_presented_renders;
	uint32_t ym_presented_samples;
	uint32_t ym_authoritative_renders;
	uint32_t ym_context_sync_failures;
	uint32_t ym_pcm_mismatches;
	uint32_t ym_status_mismatches;
	uint32_t ym_send_failures;
	uint32_t ym_first_pcm_mismatch_sample;
	uint32_t ym_first_pcm_mismatch_channel;
	int32_t ym_first_pcm_expected;
	int32_t ym_first_pcm_actual;
	uint32_t fence_token;
	bool ym_render_in_flight;
	bool fence_in_flight;
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
bool psp_me_sound_worker_fence_begin(psp_me_sound_worker_t *worker);
psp_me_sound_fence_result_t psp_me_sound_worker_fence_poll(
	psp_me_sound_worker_t *worker, uint64_t *emulated_time);
bool psp_me_sound_worker_fence(psp_me_sound_worker_t *worker,
	uint64_t timeout_us);
bool psp_me_sound_worker_read_status(psp_me_sound_worker_t *worker,
	psp_me_sound_status_snapshot_t *status);
bool psp_me_sound_worker_read_recovery_snapshot(psp_me_sound_worker_t *worker,
	psp_me_sound_recovery_snapshot_t *snapshot, uint8_t *ram,
	ym2610_context_t *ym_context, uint64_t timeout_us);
bool psp_me_sound_worker_read_published_recovery_snapshot(
	psp_me_sound_worker_t *worker, psp_me_sound_recovery_snapshot_t *snapshot,
	uint8_t *ram, ym2610_context_t *ym_context);
psp_me_sound_status_validation_t psp_me_sound_worker_validate_status(
	const psp_me_sound_status_snapshot_t *status, uint32_t generation,
	uint64_t required_time, uint8_t sound_code, uint8_t pending_command,
	uint8_t result_code);
psp_me_sound_status_validation_t psp_me_sound_worker_present_status(
	const psp_me_sound_status_snapshot_t *status, uint32_t generation,
	uint64_t required_time, uint8_t sound_code, uint8_t pending_command,
	uint8_t result_code, uint8_t *presented_pending,
	uint8_t *presented_result);
psp_me_sound_status_validation_t psp_me_sound_worker_present_authoritative_status(
	const psp_me_sound_status_snapshot_t *status, uint32_t generation,
	uint64_t required_time, uint8_t *presented_pending,
	uint8_t *presented_result);
bool psp_me_sound_worker_shadow_sound(psp_me_sound_worker_t *worker,
	uint8_t command, uint64_t emulated_time);
bool psp_me_sound_worker_authoritative_sound(psp_me_sound_worker_t *worker,
	uint8_t command, uint64_t emulated_time);
bool psp_me_sound_worker_z80_snapshot(psp_me_sound_worker_t *worker,
	const cz80_state_t *state, const uint8_t *visible_memory,
	const uint8_t *source_rom, uint32_t source_length, const uint32_t banks[4],
	uint8_t sound_code, uint8_t pending_command, uint8_t result_code,
	uint32_t ym_sample_rate, uint32_t ym_pcm_a_size, uint32_t ym_pcm_b_size,
	bool clone_default_ym, psp_me_sound_z80_mode_t mode, uint64_t timeout_us);
bool psp_me_sound_worker_z80_snapshot_with_timers(psp_me_sound_worker_t *worker,
	const cz80_state_t *state, const uint8_t *visible_memory,
	const uint8_t *source_rom, uint32_t source_length, const uint32_t banks[4],
	uint8_t sound_code, uint8_t pending_command, uint8_t result_code,
	uint32_t ym_sample_rate, uint32_t ym_pcm_a_size, uint32_t ym_pcm_b_size,
	const uint8_t ym_timer_enabled[2], const uint64_t ym_timer_remaining[2],
	bool clone_default_ym, psp_me_sound_z80_mode_t mode, uint64_t timeout_us);
bool psp_me_sound_worker_z80_irq(psp_me_sound_worker_t *worker,
	int32_t state, uint64_t emulated_time);
bool psp_me_sound_worker_ym_timer(psp_me_sound_worker_t *worker,
	uint32_t channel, uint64_t emulated_time);
bool psp_me_sound_worker_ym_render_prepare(psp_me_sound_worker_t *worker,
	uint32_t samples, uint64_t emulated_time, ym2610_pcm_window_t *window,
	uint64_t timeout_us);
bool psp_me_sound_worker_ym_render_prepare_shared(psp_me_sound_worker_t *worker,
	uint32_t samples, uint64_t emulated_time, ym2610_pcm_window_t **window,
	uint64_t timeout_us);
bool psp_me_sound_worker_ym_render_begin(psp_me_sound_worker_t *worker,
	const ym2610_pcm_window_t *window, uint64_t emulated_time,
	uint64_t timeout_us);
bool psp_me_sound_worker_ym_render_begin_shared(psp_me_sound_worker_t *worker,
	uint64_t emulated_time, uint64_t timeout_us);
bool psp_me_sound_worker_ym_render_finish(psp_me_sound_worker_t *worker,
	const int32_t *expected_left, const int32_t *expected_right,
	uint32_t samples, uint8_t expected_status_b, uint64_t timeout_us);
bool psp_me_sound_worker_ym_render_finish_present(psp_me_sound_worker_t *worker,
	const int32_t *expected_left, const int32_t *expected_right,
	int32_t *present_left, int32_t *present_right,
	uint32_t samples, uint8_t expected_status_b, uint64_t timeout_us);
bool psp_me_sound_worker_ym_render_finish_authoritative(
	psp_me_sound_worker_t *worker, int32_t *present_left,
	int32_t *present_right, uint32_t samples, bool sync_cpu_context,
	uint64_t timeout_us);
psp_me_sound_render_result_t psp_me_sound_worker_ym_render_poll_authoritative(
	psp_me_sound_worker_t *worker, int32_t *present_left,
	int32_t *present_right, uint32_t samples, bool sync_cpu_context);
bool psp_me_sound_worker_z80_slice(psp_me_sound_worker_t *worker,
	const psp_me_sound_z80_io_t *io, uint32_t io_count, uint32_t cycles,
	uint64_t emulated_time, const cz80_state_t *expected_state,
	const uint32_t banks[4], uint32_t ram_hash, bool check_ram);
bool psp_me_sound_worker_z80_advance(psp_me_sound_worker_t *worker,
	uint32_t cycles, uint32_t scheduler_time_left, uint64_t emulated_time);
bool psp_me_sound_worker_z80_advance_horizon(psp_me_sound_worker_t *worker,
	uint64_t horizon_time, uint32_t scheduler_time_left);
bool psp_me_sound_worker_z80_checkpoint(psp_me_sound_worker_t *worker,
	const cz80_state_t *expected_state, const uint32_t banks[4],
	uint32_t ram_hash, uint64_t emulated_time, uint64_t timeout_us);
bool psp_me_sound_worker_poll(psp_me_sound_worker_t *worker);
bool psp_me_sound_worker_shutdown(psp_me_sound_worker_t *worker,
	uint64_t timeout_us);
void psp_me_sound_worker_abort(psp_me_sound_worker_t *worker);
void psp_me_sound_worker_get_stats(psp_me_sound_worker_t *worker,
	psp_me_sound_worker_stats_t *stats);

#endif /* PSP_ME_SOUND_WORKER_H */
