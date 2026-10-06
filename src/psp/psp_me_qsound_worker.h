#ifndef PSP_ME_QSOUND_WORKER_H
#define PSP_ME_QSOUND_WORKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cpu/z80/cz80.h"
#include "psp/psp_me_spsc_ring.h"
#include "sound/qsound.h"

#define PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE 0x10000u
#define PSP_ME_QSOUND_RENDER_MAX_SAMPLES 800u

typedef bool (*psp_me_qsound_worker_dispatch_start_fn)(void (*task)(void *),
	void *data, uint32_t size, void *opaque);
typedef void (*psp_me_qsound_worker_dispatch_wait_fn)(void *opaque);

typedef struct psp_me_qsound_worker_dispatch
{
	psp_me_qsound_worker_dispatch_start_fn start;
	psp_me_qsound_worker_dispatch_wait_fn wait;
	void *opaque;
} psp_me_qsound_worker_dispatch_t;

typedef struct psp_me_qsound_snapshot
{
	cz80_state_t state;
	const uint8_t *source_rom;
	uint32_t source_length;
	uint32_t sample_length;
	uint32_t bank;
	uint32_t generation;
	uint8_t suspended;
	uint8_t reserved[3];
} psp_me_qsound_snapshot_t;

typedef struct psp_me_qsound_recovery_snapshot
{
	cz80_state_t state;
	uint64_t emulated_time;
	uint32_t bank;
	uint32_t generation;
	uint8_t suspended;
	uint8_t initialized;
	uint8_t reserved[2];
} psp_me_qsound_recovery_snapshot_t;

typedef struct psp_me_qsound_worker_stats
{
	uint32_t generation;
	uint32_t commands_processed;
	uint32_t resets;
	uint32_t snapshots;
	uint32_t advances;
	uint32_t irqs;
	uint32_t z80_resets;
	uint32_t bank_switches;
	uint32_t memory_reads;
	uint32_t memory_writes;
	uint32_t renders;
	uint32_t render_samples;
	uint32_t recoveries;
	uint32_t shutdowns;
	uint32_t fatal_error;
	uint32_t last_command_type;
	uint32_t command_high_water;
	uint32_t command_overflow;
	uint32_t event_high_water;
	uint32_t event_overflow;
	uint64_t emulated_time;
	uint64_t fatal_emulated_time;
} psp_me_qsound_worker_stats_t;

typedef enum psp_me_qsound_render_result
{
	PSP_ME_QSOUND_RENDER_PENDING = 0,
	PSP_ME_QSOUND_RENDER_COMPLETE,
	PSP_ME_QSOUND_RENDER_FAILED
} psp_me_qsound_render_result_t;

typedef struct psp_me_qsound_worker
{
	psp_me_qsound_worker_dispatch_t dispatch;
	psp_me_spsc_ring_t *commands;
	psp_me_spsc_ring_t *events;
	void *shared_context;
	void *main_control;
	void *progress;
	psp_me_qsound_snapshot_t *snapshot;
	psp_me_qsound_recovery_snapshot_t *recovery;
	uint8_t *z80_memory;
	qsound_context_t *qsound_context;
	void *render_job;
	size_t ring_size;
	uint32_t capacity;
	uint32_t next_token;
	uint32_t generation;
	uint32_t render_token;
	uint64_t emulated_time;
	bool render_ack_received;
	bool render_in_flight;
	bool running;
	psp_me_qsound_worker_stats_t last_stats;
} psp_me_qsound_worker_t;

bool psp_me_qsound_worker_start(psp_me_qsound_worker_t *worker,
	const psp_me_qsound_worker_dispatch_t *dispatch, uint32_t capacity,
	uint64_t timeout_us);
bool psp_me_qsound_worker_reset(psp_me_qsound_worker_t *worker,
	uint32_t generation, uint64_t timeout_us);
bool psp_me_qsound_worker_snapshot(psp_me_qsound_worker_t *worker,
	const cz80_state_t *state, const uint8_t *memory, const uint8_t *source_rom,
	uint32_t source_length, uint32_t sample_length, uint32_t bank, bool suspended,
	uint64_t timeout_us);
bool psp_me_qsound_worker_advance(psp_me_qsound_worker_t *worker,
	uint32_t cycles, uint64_t emulated_time);
bool psp_me_qsound_worker_sync(psp_me_qsound_worker_t *worker,
	uint64_t timeout_us);
bool psp_me_qsound_worker_shared_ram_read(psp_me_qsound_worker_t *worker,
	uint32_t offset, uint8_t *data, uint32_t size);
bool psp_me_qsound_worker_shared_ram_write(psp_me_qsound_worker_t *worker,
	uint32_t offset, const uint8_t *data, uint32_t size);
bool psp_me_qsound_worker_irq(psp_me_qsound_worker_t *worker,
	int32_t state, uint64_t emulated_time);
bool psp_me_qsound_worker_z80_reset(psp_me_qsound_worker_t *worker,
	bool asserted, uint64_t emulated_time);
bool psp_me_qsound_worker_memory_read(psp_me_qsound_worker_t *worker,
	uint32_t offset, uint8_t *data, uint32_t size, uint64_t timeout_us);
bool psp_me_qsound_worker_memory_write_byte(psp_me_qsound_worker_t *worker,
	uint32_t offset, uint8_t data);
bool psp_me_qsound_worker_render_begin(psp_me_qsound_worker_t *worker,
	uint32_t samples, uint64_t emulated_time);
bool psp_me_qsound_worker_render_finish(psp_me_qsound_worker_t *worker,
	int32_t *left, int32_t *right, uint32_t samples, uint64_t timeout_us);
psp_me_qsound_render_result_t psp_me_qsound_worker_render_poll(
	psp_me_qsound_worker_t *worker, int32_t *left, int32_t *right,
	uint32_t samples);
bool psp_me_qsound_worker_recover(psp_me_qsound_worker_t *worker,
	psp_me_qsound_recovery_snapshot_t *snapshot, uint8_t *memory,
	qsound_context_t *qsound, uint64_t timeout_us);
bool psp_me_qsound_worker_read_published_recovery(psp_me_qsound_worker_t *worker,
	psp_me_qsound_recovery_snapshot_t *snapshot, uint8_t *memory,
	qsound_context_t *qsound);
bool psp_me_qsound_worker_shutdown(psp_me_qsound_worker_t *worker,
	uint64_t timeout_us);
void psp_me_qsound_worker_abort(psp_me_qsound_worker_t *worker);
void psp_me_qsound_worker_get_stats(psp_me_qsound_worker_t *worker,
	psp_me_qsound_worker_stats_t *stats);

#endif /* PSP_ME_QSOUND_WORKER_H */
