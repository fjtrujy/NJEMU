#include <malloc.h>
#include <limits.h>
#include <string.h>
#include <pspkernel.h>
#include <me-core-mapper/me-core-mapper.h>
#include "psp/psp_me_sound_worker.h"
#include "sound/ym2610.h"

#define PSP_ME_SOUND_WORKER_CACHE_LINE 64u
#define PSP_ME_SOUND_WORKER_HEARTBEAT_SPINS 4096u
#define PSP_ME_SOUND_WORKER_SHARED_SIZE 128u
#define PSP_ME_SOUND_WORKER_CACHE_SIZE(size) \
	(((uint32_t)(size) + PSP_ME_SOUND_WORKER_CACHE_LINE - 1u) & \
		~(PSP_ME_SOUND_WORKER_CACHE_LINE - 1u))
#define PSP_ME_SOUND_WORKER_SHARED_POINTERS 12u
#define PSP_ME_SOUND_YM_IRQ_QUEUE_CAPACITY 8u
#define PSP_ME_SOUND_Z80_CYCLES_PER_USEC 4u
#define PSP_ME_SOUND_WORKER_SHARED_RESERVED_WORDS \
	((PSP_ME_SOUND_WORKER_SHARED_SIZE - \
			PSP_ME_SOUND_WORKER_SHARED_POINTERS * sizeof(void *)) / sizeof(uint32_t))

typedef struct __attribute__((aligned(PSP_ME_SOUND_WORKER_CACHE_LINE)))
	psp_me_sound_worker_main_control
{
	uint32_t abort_requested;
	uint32_t reserved[15];
} psp_me_sound_worker_main_control_t;

typedef struct __attribute__((aligned(PSP_ME_SOUND_WORKER_CACHE_LINE)))
	psp_me_sound_worker_progress
{
	uint32_t generation;
	uint32_t commands_processed;
	uint32_t resets;
	uint32_t syncs;
	uint32_t shutdowns;
	uint32_t shadow_commands;
	uint32_t heartbeat;
	uint32_t fatal_error;
	uint32_t running;
	uint64_t emulated_time;
	uint32_t last_token;
	uint32_t last_command_type;
	uint64_t fatal_emulated_time;
} psp_me_sound_worker_progress_t;

typedef struct __attribute__((aligned(PSP_ME_SOUND_WORKER_CACHE_LINE)))
	psp_me_sound_z80_progress
{
	uint32_t snapshots;
	uint32_t irqs;
	uint32_t slices;
	uint32_t io_events;
	uint32_t state_mismatches;
	uint32_t ram_mismatches;
	uint32_t bank_mismatches;
	uint32_t io_mismatches;
	uint32_t last_sequence;
	uint32_t last_mismatch;
	uint32_t initialized;
	uint32_t ym_renders;
	uint32_t ym_render_samples;
	uint32_t ym_render_errors;
	uint32_t autonomous_slices;
	uint32_t checkpoints;
} psp_me_sound_z80_progress_t;

typedef struct __attribute__((aligned(PSP_ME_SOUND_WORKER_CACHE_LINE)))
	psp_me_sound_ym_render_job
{
	uint32_t generation;
	uint32_t token;
	uint32_t samples;
	uint32_t error;
	uint32_t status_b;
	uint32_t reserved[3];
	ym2610_pcm_window_t window;
	int32_t left[PSP_ME_SOUND_YM_RENDER_MAX_SAMPLES];
	int32_t right[PSP_ME_SOUND_YM_RENDER_MAX_SAMPLES];
} psp_me_sound_ym_render_job_t;

typedef struct __attribute__((aligned(PSP_ME_SOUND_WORKER_CACHE_LINE)))
	psp_me_sound_worker_shared_context
{
	psp_me_spsc_ring_t *commands;
	psp_me_spsc_ring_t *events;
	psp_me_spsc_ring_t *z80_batches;
	psp_me_sound_worker_main_control_t *main_control;
	psp_me_sound_worker_progress_t *progress;
	psp_me_sound_z80_progress_t *z80_progress;
	ym2610_context_t *ym_context;
	psp_me_sound_z80_snapshot_t *z80_snapshot;
	psp_me_sound_status_snapshot_t *status_snapshot;
	psp_me_sound_recovery_snapshot_t *recovery_snapshot;
	uint8_t *z80_memory;
	psp_me_sound_ym_render_job_t *ym_render_job;
	uint32_t reserved[PSP_ME_SOUND_WORKER_SHARED_RESERVED_WORDS];
} psp_me_sound_worker_shared_context_t;

typedef struct psp_me_sound_z80_runtime
{
	cz80_struc cpu;
	uint8_t *memory;
	const uint8_t *source_rom;
	uint32_t source_length;
	uint32_t banks[4];
	const psp_me_sound_z80_slice_t *slice;
	uint32_t io_cursor;
	uint32_t mismatch;
	uint8_t sound_code;
	uint8_t pending_command;
	uint8_t result_code;
	uint8_t initialized;
	uint8_t mode;
	uint32_t scheduler_time_left;
	uint32_t advance_cycles;
	uint32_t advance_elapsed_us;
	uint64_t z80_time;
	uint64_t ym_timer_remaining[2];
	uint32_t ym_timer_arm_elapsed[2];
	uint8_t ym_timer_enabled[2];
	uint8_t in_z80_execute;
	uint8_t advance_preempted;
	ym2610_context_t *ym_context;
	uint8_t ym_irq_queue[PSP_ME_SOUND_YM_IRQ_QUEUE_CAPACITY];
	uint8_t ym_irq_head;
	uint8_t ym_irq_count;
	uint8_t ym_irq_state;
	uint32_t status_sequence;
	uint32_t ym_timer_callbacks;
	uint32_t ym_timer_overflows;
} psp_me_sound_z80_runtime_t;

static psp_me_sound_z80_runtime_t *me_z80_runtime;

_Static_assert(sizeof(psp_me_sound_worker_main_control_t) == PSP_ME_SOUND_WORKER_CACHE_LINE,
	"worker main control must occupy exactly one cache line");
_Static_assert(sizeof(psp_me_sound_worker_progress_t) == PSP_ME_SOUND_WORKER_CACHE_LINE,
	"worker progress must occupy exactly one cache line");
_Static_assert(sizeof(psp_me_sound_z80_progress_t) == PSP_ME_SOUND_WORKER_CACHE_LINE,
	"Z80 progress must occupy exactly one cache line");
_Static_assert(sizeof(psp_me_sound_worker_shared_context_t) == PSP_ME_SOUND_WORKER_SHARED_SIZE,
	"worker shared context size must remain cache-line exact");

static void allegrex_publish(void *address, uint32_t size, void *opaque)
{
	(void)opaque;
	sceKernelDcacheWritebackInvalidateRange(address, size);
}

static void allegrex_acquire(void *address, uint32_t size, void *opaque)
{
	(void)opaque;
	sceKernelDcacheInvalidateRange(address, size);
}

static void me_publish(void *address, uint32_t size, void *opaque)
{
	(void)opaque;
	meCoreDcacheWritebackRange(address, size);
}

static void me_acquire(void *address, uint32_t size, void *opaque)
{
	(void)opaque;
	meCoreDcacheInvalidateRange(address, size);
}

static const psp_me_spsc_ring_cache_ops_t allegrex_cache_ops = {
	allegrex_publish,
	allegrex_acquire,
	NULL,
};

static const psp_me_spsc_ring_cache_ops_t me_cache_ops = {
	me_publish,
	me_acquire,
	NULL,
};

static void me_zero(void *address, uint32_t size)
{
	uint8_t *bytes = (uint8_t *)address;
	uint32_t i;

	for (i = 0; i < size; i++)
		bytes[i] = 0;
}

static void me_copy(void *destination, const void *source, uint32_t size)
{
	uint8_t *dst = (uint8_t *)destination;
	const uint8_t *src = (const uint8_t *)source;
	uint32_t i;

	for (i = 0; i < size; i++)
		dst[i] = src[i];
}

static bool me_equal(const void *left, const void *right, uint32_t size)
{
	const uint8_t *a = (const uint8_t *)left;
	const uint8_t *b = (const uint8_t *)right;
	uint32_t i;

	for (i = 0; i < size; i++)
	{
		if (a[i] != b[i])
			return false;
	}
	return true;
}

static void me_z80_publish_status(psp_me_sound_worker_shared_context_t *context,
	psp_me_sound_z80_runtime_t *runtime, uint64_t emulated_time)
{
	psp_me_sound_status_snapshot_t *status;

	if (!context || !runtime || !context->status_snapshot)
		return;
	status = context->status_snapshot;
	runtime->status_sequence++;
	if (runtime->status_sequence == 0)
		runtime->status_sequence = 1;
	status->generation = context->progress->generation;
	status->sequence = runtime->status_sequence;
	status->emulated_time = emulated_time;
	status->sound_code = runtime->sound_code;
	status->pending_command = runtime->pending_command;
	status->result_code = runtime->result_code;
	status->irq_state = runtime->ym_irq_state;
	status->initialized = runtime->initialized ? 1u : 0u;
	status->ym_timer_callbacks = runtime->ym_timer_callbacks;
	status->ym_timer_overflows = runtime->ym_timer_overflows;
	status->last_advance_elapsed_us = runtime->advance_elapsed_us;
	status->z80_time = runtime->z80_time;
	meCoreDcacheWritebackRange(status, sizeof(*status));
}

static bool me_z80_publish_recovery(
	psp_me_sound_worker_shared_context_t *context,
	psp_me_sound_z80_runtime_t *runtime)
{
	psp_me_sound_recovery_snapshot_t *snapshot;

	if (!context || !runtime || !runtime->initialized ||
		!context->recovery_snapshot || !context->z80_memory ||
		!runtime->ym_context)
		return false;
	snapshot = context->recovery_snapshot;
	me_zero(snapshot, sizeof(*snapshot));
	Cz80_Get_State(&runtime->cpu, &snapshot->state);
	snapshot->emulated_time = context->progress->emulated_time;
	snapshot->z80_time = runtime->z80_time;
	snapshot->ym_timer_remaining[0] = runtime->ym_timer_remaining[0];
	snapshot->ym_timer_remaining[1] = runtime->ym_timer_remaining[1];
	me_copy(snapshot->banks, runtime->banks, sizeof(snapshot->banks));
	snapshot->generation = context->progress->generation;
	snapshot->sequence = runtime->status_sequence;
	snapshot->ym_timer_arm_elapsed[0] = runtime->ym_timer_arm_elapsed[0];
	snapshot->ym_timer_arm_elapsed[1] = runtime->ym_timer_arm_elapsed[1];
	snapshot->sound_code = runtime->sound_code;
	snapshot->pending_command = runtime->pending_command;
	snapshot->result_code = runtime->result_code;
	snapshot->irq_state = runtime->ym_irq_state;
	snapshot->ym_timer_enabled[0] = runtime->ym_timer_enabled[0];
	snapshot->ym_timer_enabled[1] = runtime->ym_timer_enabled[1];
	snapshot->initialized = runtime->initialized;
	snapshot->mode = runtime->mode;
	meCoreDcacheWritebackRange(context->z80_memory,
		PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE);
	meCoreDcacheWritebackRange(runtime->ym_context,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(YM2610ContextSize()));
	meCoreDcacheWritebackRange(snapshot, sizeof(*snapshot));
	return true;
}

enum
{
	PSP_ME_SOUND_Z80_MISMATCH_NONE = 0,
	PSP_ME_SOUND_Z80_MISMATCH_IO_SEQUENCE,
	PSP_ME_SOUND_Z80_MISMATCH_IO_VALUE,
	PSP_ME_SOUND_Z80_MISMATCH_BANK_RANGE,
	PSP_ME_SOUND_Z80_MISMATCH_IO_REMAINDER,
	PSP_ME_SOUND_Z80_MISMATCH_STATE,
	PSP_ME_SOUND_Z80_MISMATCH_BANK,
	PSP_ME_SOUND_Z80_MISMATCH_RAM,
	PSP_ME_SOUND_Z80_MISMATCH_SNAPSHOT_GENERATION,
	PSP_ME_SOUND_Z80_MISMATCH_SNAPSHOT_SOURCE,
	PSP_ME_SOUND_Z80_MISMATCH_SNAPSHOT_LENGTH,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_BC,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_DE,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_HL,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_FA,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_IX,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_IY,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_SP,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_BC2,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_DE2,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_HL2,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_FA2,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_R,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_IFF,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_PC,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_IRQ_LINE,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_IRQ_STATE,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_I,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_IM,
	PSP_ME_SOUND_Z80_MISMATCH_STATE_STATUS
};

static void me_ym_timer_callback(void *opaque, int channel, int count,
	double step_time)
{
	psp_me_sound_z80_runtime_t *runtime = me_z80_runtime;
	const psp_me_sound_z80_io_t *event;
	uint32_t elapsed_us = 0;
	(void)opaque;
	if (!runtime)
		return;
	runtime->ym_timer_callbacks++;
	if (runtime->mode == PSP_ME_SOUND_Z80_MODE_AUTONOMOUS)
	{
		if (channel < 0 || channel > 1)
		{
			runtime->mismatch = PSP_ME_SOUND_Z80_MISMATCH_IO_VALUE;
			return;
		}
		if (count == 0)
		{
			runtime->ym_timer_enabled[channel] = 0;
			runtime->ym_timer_remaining[channel] = 0;
			runtime->ym_timer_arm_elapsed[channel] = 0;
			return;
		}
		if (!runtime->ym_timer_enabled[channel])
		{
			int duration = count * (int)((float)step_time * 1000000.0);

			if (runtime->in_z80_execute)
			{
				int64_t elapsed_cycles = (int64_t)runtime->advance_cycles -
					(int64_t)runtime->cpu.ICount;

				if (elapsed_cycles > 0)
					elapsed_us = (uint32_t)(elapsed_cycles /
						PSP_ME_SOUND_Z80_CYCLES_PER_USEC);
			}
			runtime->ym_timer_enabled[channel] = 1;
			runtime->ym_timer_remaining[channel] = (uint64_t)(uint32_t)duration;
			runtime->ym_timer_arm_elapsed[channel] = elapsed_us;
			/* Match MVS timer_adjust(): it compares a newly armed timer against
			 * the scheduler's current timer_left, not the Z80 ICount remainder. */
			if (runtime->in_z80_execute &&
				duration < (int)runtime->scheduler_time_left)
			{
				if (!runtime->advance_preempted)
				{
					runtime->advance_elapsed_us = elapsed_us;
					runtime->advance_preempted = 1;
				}
				runtime->cpu.ICount = 0;
			}
		}
		return;
	}
	if (!runtime->slice || runtime->io_cursor >= runtime->slice->io_count)
		return;
	event = &runtime->slice->io[runtime->io_cursor];
	if (event->type != PSP_ME_SOUND_Z80_IO_PREEMPT)
		return;
	if (event->port != (uint16_t)channel || event->value != 0)
	{
		runtime->mismatch = PSP_ME_SOUND_Z80_MISMATCH_IO_VALUE;
		return;
	}
	runtime->io_cursor++;
	/* Allegrex's timer_adjust() forces CZ80 ICount to zero inside this exact
	 * YM timer callback, before the current OUT instruction charges its cycles.
	 * Mirror that boundary so the current instruction completes and the ME exits
	 * the slice at the same semantic point. */
	runtime->cpu.ICount = 0;
}

static bool me_ym_process_due_timers(psp_me_sound_z80_runtime_t *runtime)
{
	uint32_t channel;

	if (!runtime || runtime->mode != PSP_ME_SOUND_Z80_MODE_AUTONOMOUS ||
		!runtime->ym_context)
		return true;
	for (channel = 0; channel < 2u; channel++)
	{
		if (!runtime->ym_timer_enabled[channel] ||
			runtime->ym_timer_remaining[channel] != 0)
			continue;
		runtime->ym_timer_enabled[channel] = 0;
		runtime->ym_timer_arm_elapsed[channel] = 0;
		runtime->ym_timer_overflows++;
		(void)YM2610ContextTimerOver(runtime->ym_context, (int)channel);
		if (runtime->mismatch != PSP_ME_SOUND_Z80_MISMATCH_NONE)
			return false;
	}
	return true;
}

static bool me_ym_consume_elapsed(psp_me_sound_z80_runtime_t *runtime,
	uint32_t elapsed_us)
{
	uint32_t channel;

	if (!runtime || runtime->mode != PSP_ME_SOUND_Z80_MODE_AUTONOMOUS)
		return true;
	for (channel = 0; channel < 2u; channel++)
	{
		uint64_t active_elapsed = elapsed_us;

		if (!runtime->ym_timer_enabled[channel])
			continue;
		if (runtime->ym_timer_arm_elapsed[channel] != 0)
		{
			if (active_elapsed <= runtime->ym_timer_arm_elapsed[channel])
				active_elapsed = 0;
			else
				active_elapsed -= runtime->ym_timer_arm_elapsed[channel];
		}
		if (active_elapsed >= runtime->ym_timer_remaining[channel])
			runtime->ym_timer_remaining[channel] = 0;
		else
			runtime->ym_timer_remaining[channel] -= active_elapsed;
	}
	runtime->ym_timer_arm_elapsed[0] = 0;
	runtime->ym_timer_arm_elapsed[1] = 0;
	return true;
}

static void me_ym_irq_callback(void *opaque, int irq)
{
	psp_me_sound_z80_runtime_t *runtime = me_z80_runtime;
	uint32_t tail;
	(void)opaque;

	if (!runtime)
		return;
	runtime->ym_irq_state = irq ? ASSERT_LINE : CLEAR_LINE;
	if (runtime->initialized)
		Cz80_Set_IRQ(&runtime->cpu, 0, runtime->ym_irq_state);
	if (runtime->mode == PSP_ME_SOUND_Z80_MODE_AUTONOMOUS)
		return;
	if (runtime->ym_irq_count >= PSP_ME_SOUND_YM_IRQ_QUEUE_CAPACITY)
	{
		runtime->mismatch = PSP_ME_SOUND_Z80_MISMATCH_IO_SEQUENCE;
		return;
	}
	tail = (runtime->ym_irq_head + runtime->ym_irq_count) &
		(PSP_ME_SOUND_YM_IRQ_QUEUE_CAPACITY - 1u);
	runtime->ym_irq_queue[tail] = irq ? ASSERT_LINE : CLEAR_LINE;
	runtime->ym_irq_count++;
}

static bool me_ym_validate_irq(psp_me_sound_z80_runtime_t *runtime,
	uint8_t expected)
{
	uint8_t actual;

	if (!runtime || runtime->ym_irq_count == 0)
		return false;
	actual = runtime->ym_irq_queue[runtime->ym_irq_head];
	runtime->ym_irq_head = (runtime->ym_irq_head + 1u) &
		(PSP_ME_SOUND_YM_IRQ_QUEUE_CAPACITY - 1u);
	runtime->ym_irq_count--;
	return actual == expected;
}

static uint32_t z80_ram_hash(const uint8_t *memory)
{
	uint32_t hash = 2166136261u;
	uint32_t i;

	for (i = PSP_ME_SOUND_Z80_RAM_OFFSET; i < PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE; i++)
	{
		hash ^= memory[i];
		hash *= 16777619u;
	}
	return hash;
}

static bool me_z80_consume_io(uint16_t port, uint8_t type, uint8_t *value)
{
	psp_me_sound_z80_runtime_t *runtime = me_z80_runtime;
	const psp_me_sound_z80_io_t *expected;

	if (!runtime || !runtime->slice || runtime->io_cursor >= runtime->slice->io_count)
	{
		if (runtime)
			runtime->mismatch = PSP_ME_SOUND_Z80_MISMATCH_IO_SEQUENCE;
		return false;
	}

	expected = &runtime->slice->io[runtime->io_cursor++];
	if (expected->port != port || expected->type != type)
	{
		runtime->mismatch = PSP_ME_SOUND_Z80_MISMATCH_IO_SEQUENCE;
		return false;
	}
	if (type == PSP_ME_SOUND_Z80_IO_WRITE && expected->value != *value)
	{
		runtime->mismatch = PSP_ME_SOUND_Z80_MISMATCH_IO_VALUE;
		return false;
	}
	if (type == PSP_ME_SOUND_Z80_IO_READ)
		*value = expected->value;
	return true;
}

static void me_z80_apply_inline_irqs(psp_me_sound_z80_runtime_t *runtime)
{
	while (runtime && runtime->slice && runtime->io_cursor < runtime->slice->io_count)
	{
		const psp_me_sound_z80_io_t *event =
			&runtime->slice->io[runtime->io_cursor];

		if (event->type != PSP_ME_SOUND_Z80_IO_IRQ)
			break;
		if (event->port != 0 ||
			(event->value != CLEAR_LINE && event->value != ASSERT_LINE))
		{
			runtime->mismatch = PSP_ME_SOUND_Z80_MISMATCH_IO_VALUE;
			return;
		}
		runtime->io_cursor++;
		if (runtime->ym_context)
		{
			if (!me_ym_validate_irq(runtime, event->value))
			{
				runtime->mismatch = PSP_ME_SOUND_Z80_MISMATCH_IO_VALUE;
				return;
			}
		}
		else
		{
			Cz80_Set_IRQ(&runtime->cpu, 0, event->value);
		}
	}
}

static bool me_z80_set_bank(psp_me_sound_z80_runtime_t *runtime, uint32_t bank,
	uint32_t offset)
{
	static const uint32_t destination[4] = { 0x8000u, 0xc000u, 0xe000u, 0xf000u };
	static const uint32_t size[4] = { 0x4000u, 0x2000u, 0x1000u, 0x0800u };
	uint32_t source;

	if (bank >= 4u)
		return false;
	source = 0x10000u + offset;
	if (source > runtime->source_length || size[bank] > runtime->source_length - source)
	{
		runtime->mismatch = PSP_ME_SOUND_Z80_MISMATCH_BANK_RANGE;
		return false;
	}

	runtime->banks[bank] = offset;
	me_copy(runtime->memory + destination[bank], runtime->source_rom + source, size[bank]);
	return true;
}

static uint8_t me_z80_read_memory(uint32_t address)
{
	return me_z80_runtime->memory[address & 0xffffu];
}

static void me_z80_write_memory(uint32_t address, uint8_t value)
{
	address &= 0xffffu;
	if (address >= PSP_ME_SOUND_Z80_RAM_OFFSET)
		me_z80_runtime->memory[address] = value;
}

static uint8_t me_z80_port_read(uint16_t port)
{
	psp_me_sound_z80_runtime_t *runtime = me_z80_runtime;
	uint8_t expected = 0;
	uint8_t value = 0;
	uint8_t low = (uint8_t)port;
	bool oracle;

	if (!runtime)
		return 0;
	oracle = runtime->mode == PSP_ME_SOUND_Z80_MODE_ORACLE;
	if (oracle)
	{
		if (!me_z80_consume_io(port, PSP_ME_SOUND_Z80_IO_READ, &expected))
			return 0;
		value = expected;
	}

	switch (low)
	{
	case 0x00:
		value = runtime->sound_code;
		runtime->pending_command = 0;
		break;
	case 0x04:
		if (runtime->ym_context)
			value = YM2610ContextRead(runtime->ym_context, 0);
		break;
	case 0x05:
		if (runtime->ym_context)
			value = YM2610ContextRead(runtime->ym_context, 1);
		break;
	case 0x06:
		if (runtime->ym_context)
			value = YM2610ContextRead(runtime->ym_context, 2);
		break;
	case 0x08:
		(void)me_z80_set_bank(runtime, 3u, (uint32_t)(port & 0x7f00u) << 3);
		break;
	case 0x09:
		(void)me_z80_set_bank(runtime, 2u, (uint32_t)(port & 0x3f00u) << 4);
		break;
	case 0x0a:
		(void)me_z80_set_bank(runtime, 1u, (uint32_t)(port & 0x1f00u) << 5);
		break;
	case 0x0b:
		(void)me_z80_set_bank(runtime, 0u, (uint32_t)(port & 0x0f00u) << 6);
		break;
	default:
		break;
	}
	if (oracle && value != expected)
		runtime->mismatch = PSP_ME_SOUND_Z80_MISMATCH_IO_VALUE;
	return value;
}

static void me_z80_port_write(uint16_t port, uint8_t value)
{
	psp_me_sound_z80_runtime_t *runtime = me_z80_runtime;
	uint8_t expected_value = value;
	bool oracle;

	if (!runtime)
		return;
	oracle = runtime->mode == PSP_ME_SOUND_Z80_MODE_ORACLE;
	if (oracle && !me_z80_consume_io(port, PSP_ME_SOUND_Z80_IO_WRITE, &expected_value))
		return;
	switch ((uint8_t)port)
	{
	case 0x04:
		if (runtime->ym_context)
			(void)YM2610ContextWrite(runtime->ym_context, 0, value);
		break;
	case 0x05:
		if (runtime->ym_context)
			(void)YM2610ContextWrite(runtime->ym_context, 1, value);
		break;
	case 0x06:
		if (runtime->ym_context)
			(void)YM2610ContextWrite(runtime->ym_context, 2, value);
		break;
	case 0x07:
		if (runtime->ym_context)
			(void)YM2610ContextWrite(runtime->ym_context, 3, value);
		break;
	case 0x0c:
		runtime->result_code = value;
		break;
	default:
		break;
	}
	if (oracle)
		me_z80_apply_inline_irqs(runtime);
}

static uint32_t me_z80_state_mismatch(cz80_struc *cpu,
	const cz80_state_t *expected)
{
	cz80_state_t actual;

	Cz80_Get_State(cpu, &actual);
	if (actual.BC != expected->BC) return PSP_ME_SOUND_Z80_MISMATCH_STATE_BC;
	if (actual.DE != expected->DE) return PSP_ME_SOUND_Z80_MISMATCH_STATE_DE;
	if (actual.HL != expected->HL) return PSP_ME_SOUND_Z80_MISMATCH_STATE_HL;
	if (actual.FA != expected->FA) return PSP_ME_SOUND_Z80_MISMATCH_STATE_FA;
	if (actual.IX != expected->IX) return PSP_ME_SOUND_Z80_MISMATCH_STATE_IX;
	if (actual.IY != expected->IY) return PSP_ME_SOUND_Z80_MISMATCH_STATE_IY;
	if (actual.SP != expected->SP) return PSP_ME_SOUND_Z80_MISMATCH_STATE_SP;
	if (actual.BC2 != expected->BC2) return PSP_ME_SOUND_Z80_MISMATCH_STATE_BC2;
	if (actual.DE2 != expected->DE2) return PSP_ME_SOUND_Z80_MISMATCH_STATE_DE2;
	if (actual.HL2 != expected->HL2) return PSP_ME_SOUND_Z80_MISMATCH_STATE_HL2;
	if (actual.FA2 != expected->FA2) return PSP_ME_SOUND_Z80_MISMATCH_STATE_FA2;
	if (actual.R != expected->R) return PSP_ME_SOUND_Z80_MISMATCH_STATE_R;
	if (actual.IFF != expected->IFF) return PSP_ME_SOUND_Z80_MISMATCH_STATE_IFF;
	if (actual.PC != expected->PC) return PSP_ME_SOUND_Z80_MISMATCH_STATE_PC;
	if (actual.IRQLine != expected->IRQLine)
		return PSP_ME_SOUND_Z80_MISMATCH_STATE_IRQ_LINE;
	if (actual.IRQState != expected->IRQState)
		return PSP_ME_SOUND_Z80_MISMATCH_STATE_IRQ_STATE;
	if (actual.I != expected->I) return PSP_ME_SOUND_Z80_MISMATCH_STATE_I;
	if (actual.IM != expected->IM) return PSP_ME_SOUND_Z80_MISMATCH_STATE_IM;
	if (actual.Status != expected->Status)
		return PSP_ME_SOUND_Z80_MISMATCH_STATE_STATUS;
	return PSP_ME_SOUND_Z80_MISMATCH_NONE;
}

static void me_z80_apply_snapshot(psp_me_sound_worker_shared_context_t *context,
	psp_me_sound_z80_runtime_t *runtime)
{
	psp_me_sound_z80_snapshot_t *snapshot = context->z80_snapshot;

	meCoreDcacheInvalidateRange(snapshot,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*snapshot)));
	meCoreDcacheInvalidateRange(context->z80_memory, PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE);
	meCoreDcacheInvalidateRange(context->ym_context,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(YM2610ContextSize()));
	if (snapshot->source_rom && snapshot->source_length != 0)
		meCoreDcacheInvalidateRange((void *)snapshot->source_rom, snapshot->source_length);

	me_zero(runtime, sizeof(*runtime));
	runtime->memory = context->z80_memory;
	runtime->source_rom = snapshot->source_rom;
	runtime->source_length = snapshot->source_length;
	me_copy(runtime->banks, snapshot->banks, sizeof(runtime->banks));
	runtime->sound_code = snapshot->sound_code;
	runtime->pending_command = snapshot->pending_command;
	runtime->result_code = snapshot->result_code;
	runtime->mode = snapshot->mode;
	runtime->z80_time = context->progress->emulated_time;
	runtime->ym_context = context->ym_context;
	me_z80_runtime = runtime;
	YM2610ContextSetCallbacks(runtime->ym_context, me_ym_timer_callback,
		me_ym_irq_callback, NULL);
	Cz80_Init_Instance(&runtime->cpu);
	Cz80_Set_Fetch(&runtime->cpu, 0x0000u, 0xffffu, (uintptr_t)runtime->memory);
	Cz80_Set_ReadBase(&runtime->cpu, (uintptr_t)runtime->memory);
	Cz80_Set_ReadB(&runtime->cpu, me_z80_read_memory);
	Cz80_Set_WriteB(&runtime->cpu, me_z80_write_memory);
	Cz80_Set_INPort(&runtime->cpu, me_z80_port_read);
	Cz80_Set_OUTPort(&runtime->cpu, me_z80_port_write);
	Cz80_Set_State(&runtime->cpu, &snapshot->state);
	runtime->initialized = 1;
	me_z80_publish_status(context, runtime, context->progress->emulated_time);
}

static bool me_z80_execute_slice(psp_me_sound_worker_shared_context_t *context,
	psp_me_sound_z80_runtime_t *runtime, psp_me_sound_z80_progress_t *progress,
	uint32_t expected_sequence)
{
	psp_me_sound_z80_slice_t slice;
	psp_me_spsc_ring_result_t result;

	if (runtime->mode != PSP_ME_SOUND_Z80_MODE_ORACLE)
	{
		progress->last_mismatch = PSP_ME_SOUND_Z80_MISMATCH_IO_SEQUENCE;
		return false;
	}

	result = psp_me_spsc_ring_try_pop(context->z80_batches, &me_cache_ops, &slice,
		NULL);
	if (result != PSP_ME_SPSC_RING_OK || slice.generation != context->progress->generation ||
		slice.sequence != expected_sequence ||
		slice.io_count > PSP_ME_SOUND_Z80_IO_CAPACITY)
	{
		progress->io_mismatches++;
		progress->last_mismatch = PSP_ME_SOUND_Z80_MISMATCH_IO_SEQUENCE;
		return false;
	}

	runtime->slice = &slice;
	runtime->io_cursor = 0;
	runtime->mismatch = PSP_ME_SOUND_Z80_MISMATCH_NONE;
	me_z80_runtime = runtime;
	(void)Cz80_Exec(&runtime->cpu, (int32_t)slice.cycles);
	progress->slices++;
	progress->io_events += slice.io_count;
	progress->last_sequence = slice.sequence;

	if (runtime->mismatch != PSP_ME_SOUND_Z80_MISMATCH_NONE ||
		runtime->io_cursor != slice.io_count)
	{
		progress->io_mismatches++;
		progress->last_mismatch = runtime->mismatch != PSP_ME_SOUND_Z80_MISMATCH_NONE ?
			runtime->mismatch : PSP_ME_SOUND_Z80_MISMATCH_IO_REMAINDER;
		return false;
	}
	if ((runtime->mismatch = me_z80_state_mismatch(&runtime->cpu,
		&slice.expected_state)) != PSP_ME_SOUND_Z80_MISMATCH_NONE)
	{
		progress->state_mismatches++;
		progress->last_mismatch = runtime->mismatch;
		return false;
	}
	if (!me_equal(runtime->banks, slice.banks, sizeof(runtime->banks)))
	{
		progress->bank_mismatches++;
		progress->last_mismatch = PSP_ME_SOUND_Z80_MISMATCH_BANK;
		return false;
	}
	if ((slice.flags & PSP_ME_SOUND_Z80_SLICE_CHECK_RAM) != 0 &&
		z80_ram_hash(runtime->memory) != slice.ram_hash)
	{
		progress->ram_mismatches++;
		progress->last_mismatch = PSP_ME_SOUND_Z80_MISMATCH_RAM;
		return false;
	}

	runtime->slice = NULL;
	return true;
}

static bool me_z80_advance_autonomous(psp_me_sound_z80_runtime_t *runtime,
	psp_me_sound_z80_progress_t *progress, uint32_t cycles,
	uint32_t scheduler_time_left)
{
	if (!runtime || !progress || runtime->mode != PSP_ME_SOUND_Z80_MODE_AUTONOMOUS ||
		cycles > (uint32_t)INT32_MAX)
		return false;
	if (!me_ym_process_due_timers(runtime))
		return false;

	runtime->slice = NULL;
	runtime->io_cursor = 0;
	runtime->mismatch = PSP_ME_SOUND_Z80_MISMATCH_NONE;
	runtime->scheduler_time_left = scheduler_time_left;
	runtime->advance_cycles = cycles;
	runtime->advance_elapsed_us = cycles / PSP_ME_SOUND_Z80_CYCLES_PER_USEC;
	runtime->advance_preempted = 0;
	runtime->in_z80_execute = 1;
	me_z80_runtime = runtime;
	(void)Cz80_Exec(&runtime->cpu, (int32_t)cycles);
	runtime->in_z80_execute = 0;
	if (!me_ym_consume_elapsed(runtime, runtime->advance_elapsed_us))
		return false;
	progress->slices++;
	progress->autonomous_slices++;
	if (runtime->mismatch != PSP_ME_SOUND_Z80_MISMATCH_NONE)
	{
		progress->io_mismatches++;
		progress->last_mismatch = runtime->mismatch;
		return false;
	}
	return true;
}

static bool me_z80_advance_horizon(psp_me_sound_z80_runtime_t *runtime,
	psp_me_sound_z80_progress_t *progress, uint64_t horizon_time,
	uint32_t scheduler_time_left)
{
	uint64_t total_elapsed = 0;
	uint32_t iterations = 0;

	if (!runtime || !progress || runtime->mode != PSP_ME_SOUND_Z80_MODE_AUTONOMOUS ||
		horizon_time < runtime->z80_time)
		return false;

	while (runtime->z80_time < horizon_time)
	{
		uint64_t requested_us = horizon_time - runtime->z80_time;
		uint64_t next_timer = UINT64_MAX;
		uint32_t channel;
		uint32_t chunk_us;
		uint32_t cycles;
		uint32_t local_scheduler_left;
		uint32_t elapsed_us;

		if (++iterations > 65536u || !me_ym_process_due_timers(runtime))
			return false;
		for (channel = 0; channel < 2u; channel++)
		{
			if (runtime->ym_timer_enabled[channel] &&
				runtime->ym_timer_remaining[channel] < next_timer)
				next_timer = runtime->ym_timer_remaining[channel];
		}
		if (next_timer < requested_us)
			requested_us = next_timer;
		if (requested_us == 0)
			continue;
		if (requested_us > (uint64_t)INT32_MAX / PSP_ME_SOUND_Z80_CYCLES_PER_USEC)
			return false;
		chunk_us = (uint32_t)requested_us;
		cycles = chunk_us * PSP_ME_SOUND_Z80_CYCLES_PER_USEC;
		local_scheduler_left = total_elapsed < scheduler_time_left ?
			scheduler_time_left - (uint32_t)total_elapsed : 0u;
		if (!me_z80_advance_autonomous(runtime, progress, cycles,
				local_scheduler_left))
			return false;
		elapsed_us = runtime->advance_elapsed_us;
		if (elapsed_us > chunk_us)
			return false;
		runtime->z80_time += elapsed_us;
		total_elapsed += elapsed_us;
	}
	return me_ym_process_due_timers(runtime);
}

static bool me_z80_check_checkpoint(psp_me_sound_worker_shared_context_t *context,
	psp_me_sound_z80_runtime_t *runtime, psp_me_sound_z80_progress_t *progress,
	uint32_t expected_sequence)
{
	psp_me_sound_z80_slice_t checkpoint;
	psp_me_spsc_ring_result_t result;

	if (runtime->mode != PSP_ME_SOUND_Z80_MODE_AUTONOMOUS)
	{
		progress->last_mismatch = PSP_ME_SOUND_Z80_MISMATCH_IO_SEQUENCE;
		return false;
	}

	result = psp_me_spsc_ring_try_pop(context->z80_batches, &me_cache_ops,
		&checkpoint, NULL);
	if (result != PSP_ME_SPSC_RING_OK ||
		checkpoint.generation != context->progress->generation ||
		checkpoint.sequence != expected_sequence || checkpoint.cycles != 0 ||
		checkpoint.io_count != 0)
	{
		progress->last_mismatch = PSP_ME_SOUND_Z80_MISMATCH_IO_SEQUENCE;
		return false;
	}

	progress->checkpoints++;
	progress->last_sequence = checkpoint.sequence;
	if ((runtime->mismatch = me_z80_state_mismatch(&runtime->cpu,
		&checkpoint.expected_state)) != PSP_ME_SOUND_Z80_MISMATCH_NONE)
	{
		progress->state_mismatches++;
		progress->last_mismatch = runtime->mismatch;
		return false;
	}
	if (!me_equal(runtime->banks, checkpoint.banks, sizeof(runtime->banks)))
	{
		progress->bank_mismatches++;
		progress->last_mismatch = PSP_ME_SOUND_Z80_MISMATCH_BANK;
		return false;
	}
	if ((checkpoint.flags & PSP_ME_SOUND_Z80_SLICE_CHECK_RAM) != 0 &&
		z80_ram_hash(runtime->memory) != checkpoint.ram_hash)
	{
		progress->ram_mismatches++;
		progress->last_mismatch = PSP_ME_SOUND_Z80_MISMATCH_RAM;
		return false;
	}
	return true;
}

static void me_z80_discard_pending_batches(psp_me_sound_worker_shared_context_t *context)
{
	psp_me_spsc_ring_t *ring = context->z80_batches;

	me_acquire(&ring->producer, PSP_ME_SPSC_RING_CACHE_LINE, NULL);
	ring->consumer.read_sequence = ring->producer.write_sequence;
	me_publish(&ring->consumer, PSP_ME_SPSC_RING_CACHE_LINE, NULL);
}

static bool me_abort_requested(psp_me_sound_worker_main_control_t *control)
{
	meCoreDcacheInvalidateRange(control, sizeof(*control));
	return control->abort_requested != 0;
}

static void me_publish_progress(psp_me_sound_worker_progress_t *progress)
{
	meCoreDcacheWritebackRange(progress, sizeof(*progress));
}

static bool me_send_event(psp_me_sound_worker_shared_context_t *context,
	const psp_me_sound_worker_message_t *event)
{
	uint32_t spin_count = 0;

	for (;;)
	{
		psp_me_spsc_ring_result_t result = psp_me_spsc_ring_try_push(
			context->events, &me_cache_ops, event, NULL);
		if (result == PSP_ME_SPSC_RING_OK)
			return true;
		if (result != PSP_ME_SPSC_RING_FULL)
			return false;
		spin_count++;
		if ((spin_count & (PSP_ME_SOUND_WORKER_HEARTBEAT_SPINS - 1u)) == 0 &&
			me_abort_requested(context->main_control))
			return false;
	}
}

static void me_fail(psp_me_sound_worker_shared_context_t *context,
	uint32_t generation, uint32_t token, uint32_t error)
{
	psp_me_sound_worker_message_t event;

	context->progress->fatal_error = error;
	context->progress->running = 0;
	context->progress->last_token = token;
	me_publish_progress(context->progress);

	me_zero(&event, sizeof(event));
	event.type = PSP_ME_SOUND_WORKER_EVENT_ERROR;
	event.generation = generation;
	event.token = token;
	event.emulated_time = context->progress->emulated_time;
	event.value = error;
	(void)me_send_event(context, &event);
}

static void me_fail_time_regression(psp_me_sound_worker_shared_context_t *context,
	const psp_me_sound_worker_message_t *command)
{
	context->progress->fatal_emulated_time = command->emulated_time;
	me_fail(context, context->progress->generation, command->token,
		PSP_ME_SOUND_WORKER_ERROR_TIME_REGRESSION);
}

static void psp_me_sound_worker_entry(void *param)
{
	psp_me_sound_worker_shared_context_t *context =
		(psp_me_sound_worker_shared_context_t *)param;
	psp_me_sound_z80_runtime_t z80_runtime;
	psp_me_sound_worker_message_t event;
	uint32_t idle_spins = 0;

	meCoreDcacheInvalidateRange(context, sizeof(*context));
	psp_me_spsc_ring_acquire_initial(context->commands, &me_cache_ops);
	psp_me_spsc_ring_acquire_initial(context->events, &me_cache_ops);
	psp_me_spsc_ring_acquire_initial(context->z80_batches, &me_cache_ops);
	meCoreDcacheInvalidateRange(context->main_control,
		sizeof(*context->main_control));
	meCoreDcacheInvalidateRange(context->progress, sizeof(*context->progress));
	meCoreDcacheInvalidateRange(context->z80_progress, sizeof(*context->z80_progress));

	if (!psp_me_spsc_ring_is_valid(context->commands) ||
		!psp_me_spsc_ring_is_valid(context->events) ||
		!psp_me_spsc_ring_is_valid(context->z80_batches))
	{
		me_fail(context, 0, 0, PSP_ME_SOUND_WORKER_ERROR_RING);
		return;
	}

	me_zero(&z80_runtime, sizeof(z80_runtime));
	me_z80_runtime = &z80_runtime;
	me_zero(context->progress, sizeof(*context->progress));
	me_zero(context->z80_progress, sizeof(*context->z80_progress));
	context->progress->running = 1;
	context->progress->heartbeat = 1;
	me_publish_progress(context->progress);
	meCoreDcacheWritebackRange(context->z80_progress, sizeof(*context->z80_progress));

	me_zero(&event, sizeof(event));
	event.type = PSP_ME_SOUND_WORKER_EVENT_READY;
	if (!me_send_event(context, &event))
	{
		me_fail(context, 0, 0, PSP_ME_SOUND_WORKER_ERROR_RING);
		return;
	}

	for (;;)
	{
			psp_me_sound_worker_message_t command;
			bool send_response = true;
			psp_me_spsc_ring_result_t result = psp_me_spsc_ring_try_pop(
			context->commands, &me_cache_ops, &command, NULL);

		if (result == PSP_ME_SPSC_RING_EMPTY)
		{
			idle_spins++;
			if ((idle_spins & (PSP_ME_SOUND_WORKER_HEARTBEAT_SPINS - 1u)) == 0)
			{
				context->progress->heartbeat++;
				me_publish_progress(context->progress);
				if (me_abort_requested(context->main_control))
				{
					context->progress->fatal_error = PSP_ME_SOUND_WORKER_ERROR_ABORTED;
					context->progress->running = 0;
					me_publish_progress(context->progress);
					return;
				}
			}
			continue;
		}
		if (result != PSP_ME_SPSC_RING_OK)
		{
			me_fail(context, context->progress->generation,
				context->progress->last_token, PSP_ME_SOUND_WORKER_ERROR_RING);
			return;
		}

		idle_spins = 0;
		context->progress->commands_processed++;
		context->progress->last_token = command.token;
		context->progress->last_command_type = command.type;
		context->progress->heartbeat++;
		me_zero(&event, sizeof(event));
		event.generation = command.generation;
		event.token = command.token;

		switch (command.type)
		{
			case PSP_ME_SOUND_WORKER_COMMAND_RESET:
			if (command.generation == 0)
			{
				me_fail(context, context->progress->generation, command.token,
					PSP_ME_SOUND_WORKER_ERROR_GENERATION);
				return;
			}
				context->progress->generation = command.generation;
				context->progress->emulated_time = command.emulated_time;
				context->progress->resets++;
				me_z80_discard_pending_batches(context);
				me_zero(&z80_runtime, sizeof(z80_runtime));
				me_z80_runtime = &z80_runtime;
				me_zero(context->status_snapshot, sizeof(*context->status_snapshot));
				me_z80_publish_status(context, &z80_runtime,
					context->progress->emulated_time);
				me_zero(context->z80_progress, sizeof(*context->z80_progress));
				meCoreDcacheWritebackRange(context->z80_progress,
					sizeof(*context->z80_progress));
				event.type = PSP_ME_SOUND_WORKER_EVENT_RESET_ACK;
			event.emulated_time = context->progress->emulated_time;
			break;

		case PSP_ME_SOUND_WORKER_COMMAND_SYNC:
			if (command.generation != context->progress->generation)
			{
				me_fail(context, context->progress->generation, command.token,
					PSP_ME_SOUND_WORKER_ERROR_GENERATION);
				return;
			}
			if (command.emulated_time < context->progress->emulated_time)
			{
				me_fail_time_regression(context, &command);
				return;
			}
			if (z80_runtime.initialized && !me_ym_process_due_timers(&z80_runtime))
			{
				me_fail(context, context->progress->generation, command.token,
					PSP_ME_SOUND_WORKER_ERROR_Z80_STATE);
				return;
			}
			context->progress->emulated_time = command.emulated_time;
			context->progress->syncs++;
			if (z80_runtime.initialized)
				me_z80_publish_status(context, &z80_runtime, command.emulated_time);
			event.type = PSP_ME_SOUND_WORKER_EVENT_SYNC_ACK;
			event.emulated_time = context->progress->emulated_time;
			break;

		case PSP_ME_SOUND_WORKER_COMMAND_FENCE:
			if (command.generation != context->progress->generation)
			{
				me_fail(context, context->progress->generation, command.token,
					PSP_ME_SOUND_WORKER_ERROR_GENERATION);
				return;
			}
			if (z80_runtime.initialized && !me_ym_process_due_timers(&z80_runtime))
			{
				me_fail(context, context->progress->generation, command.token,
					PSP_ME_SOUND_WORKER_ERROR_Z80_STATE);
				return;
			}
			if (z80_runtime.initialized)
				me_z80_publish_status(context, &z80_runtime,
					context->progress->emulated_time);
			event.type = PSP_ME_SOUND_WORKER_EVENT_FENCE_ACK;
			event.emulated_time = context->progress->emulated_time;
			break;

		case PSP_ME_SOUND_WORKER_COMMAND_RECOVERY_SNAPSHOT:
			if (command.generation != context->progress->generation ||
				!z80_runtime.initialized)
			{
				me_fail(context, context->progress->generation, command.token,
					PSP_ME_SOUND_WORKER_ERROR_GENERATION);
				return;
			}
			if (!me_ym_process_due_timers(&z80_runtime) ||
				!me_z80_publish_recovery(context, &z80_runtime))
			{
				me_fail(context, context->progress->generation, command.token,
					PSP_ME_SOUND_WORKER_ERROR_Z80_STATE);
				return;
			}
			event.type = PSP_ME_SOUND_WORKER_EVENT_RECOVERY_SNAPSHOT_ACK;
			event.emulated_time = context->progress->emulated_time;
			break;

		case PSP_ME_SOUND_WORKER_COMMAND_SHUTDOWN:
			if (command.generation != context->progress->generation)
			{
				me_fail(context, context->progress->generation, command.token,
					PSP_ME_SOUND_WORKER_ERROR_GENERATION);
				return;
			}
			context->progress->shutdowns++;
			context->progress->running = 0;
			event.type = PSP_ME_SOUND_WORKER_EVENT_SHUTDOWN_ACK;
			event.emulated_time = context->progress->emulated_time;
			me_publish_progress(context->progress);
			if (!me_send_event(context, &event))
				me_fail(context, context->progress->generation, command.token,
					PSP_ME_SOUND_WORKER_ERROR_RING);
			return;

			case PSP_ME_SOUND_WORKER_COMMAND_SHADOW_SOUND:
			if (command.generation != context->progress->generation)
			{
				me_fail(context, context->progress->generation, command.token,
					PSP_ME_SOUND_WORKER_ERROR_GENERATION);
				return;
			}
			if (command.emulated_time < context->progress->emulated_time)
			{
				me_fail_time_regression(context, &command);
				return;
			}
			if (z80_runtime.initialized && !me_ym_process_due_timers(&z80_runtime))
			{
				me_fail(context, context->progress->generation, command.token,
					PSP_ME_SOUND_WORKER_ERROR_Z80_STATE);
				return;
			}
				context->progress->emulated_time = command.emulated_time;
				context->progress->shadow_commands++;
				if (z80_runtime.initialized)
				{
					z80_runtime.sound_code = (uint8_t)command.value;
					z80_runtime.pending_command = 1;
					Cz80_Set_IRQ(&z80_runtime.cpu, IRQ_LINE_NMI, PULSE_LINE);
					me_z80_publish_status(context, &z80_runtime,
						command.emulated_time);
				}
				event.type = PSP_ME_SOUND_WORKER_EVENT_SHADOW_SOUND_ECHO;
			event.emulated_time = command.emulated_time;
				event.value = command.value;
				break;

			case PSP_ME_SOUND_WORKER_COMMAND_Z80_SNAPSHOT:
				if (command.generation != context->progress->generation)
				{
					me_fail(context, context->progress->generation, command.token,
						PSP_ME_SOUND_WORKER_ERROR_GENERATION);
					return;
				}
					meCoreDcacheInvalidateRange(context->z80_snapshot,
						PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*context->z80_snapshot)));
					if (context->z80_snapshot->generation != command.generation)
					{
						context->z80_progress->last_mismatch =
							PSP_ME_SOUND_Z80_MISMATCH_SNAPSHOT_GENERATION;
						meCoreDcacheWritebackRange(context->z80_progress,
							sizeof(*context->z80_progress));
						me_fail(context, context->progress->generation, command.token,
							PSP_ME_SOUND_WORKER_ERROR_GENERATION);
						return;
					}
					if (!context->z80_snapshot->source_rom)
					{
						context->z80_progress->last_mismatch =
							PSP_ME_SOUND_Z80_MISMATCH_SNAPSHOT_SOURCE;
						meCoreDcacheWritebackRange(context->z80_progress,
							sizeof(*context->z80_progress));
						me_fail(context, context->progress->generation, command.token,
							PSP_ME_SOUND_WORKER_ERROR_Z80_STATE);
						return;
					}
					if (context->z80_snapshot->source_length <
						PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE)
					{
						context->z80_progress->last_mismatch =
							PSP_ME_SOUND_Z80_MISMATCH_SNAPSHOT_LENGTH;
						meCoreDcacheWritebackRange(context->z80_progress,
							sizeof(*context->z80_progress));
						me_fail(context, context->progress->generation, command.token,
							PSP_ME_SOUND_WORKER_ERROR_Z80_STATE);
						return;
					}
					if (context->z80_snapshot->mode != PSP_ME_SOUND_Z80_MODE_ORACLE &&
						context->z80_snapshot->mode != PSP_ME_SOUND_Z80_MODE_AUTONOMOUS)
					{
						me_fail(context, context->progress->generation, command.token,
							PSP_ME_SOUND_WORKER_ERROR_PROTOCOL);
						return;
					}
				me_z80_apply_snapshot(context, &z80_runtime);
				context->z80_progress->snapshots++;
				context->z80_progress->initialized = 1;
				meCoreDcacheWritebackRange(context->z80_progress,
					sizeof(*context->z80_progress));
				event.type = PSP_ME_SOUND_WORKER_EVENT_Z80_SNAPSHOT_ACK;
				event.emulated_time = context->progress->emulated_time;
				break;

			case PSP_ME_SOUND_WORKER_COMMAND_Z80_IRQ:
				if (command.generation != context->progress->generation ||
					!z80_runtime.initialized)
				{
					me_fail(context, context->progress->generation, command.token,
						PSP_ME_SOUND_WORKER_ERROR_GENERATION);
					return;
				}
				if (command.emulated_time < context->progress->emulated_time)
				{
					me_fail_time_regression(context, &command);
					return;
				}
				context->progress->emulated_time = command.emulated_time;
				if (command.value != CLEAR_LINE && command.value != ASSERT_LINE)
				{
					context->z80_progress->io_mismatches++;
					context->z80_progress->last_mismatch =
						PSP_ME_SOUND_Z80_MISMATCH_IO_VALUE;
					meCoreDcacheWritebackRange(context->z80_progress,
						sizeof(*context->z80_progress));
					me_fail(context, context->progress->generation, command.token,
						PSP_ME_SOUND_WORKER_ERROR_Z80_TRACE);
					return;
				}
				if (z80_runtime.ym_context)
				{
					if (!me_ym_validate_irq(&z80_runtime, (uint8_t)command.value))
					{
						context->z80_progress->io_mismatches++;
						context->z80_progress->last_mismatch =
							PSP_ME_SOUND_Z80_MISMATCH_IO_VALUE;
						meCoreDcacheWritebackRange(context->z80_progress,
							sizeof(*context->z80_progress));
						me_fail(context, context->progress->generation, command.token,
							PSP_ME_SOUND_WORKER_ERROR_Z80_TRACE);
						return;
					}
				}
				else
				{
					Cz80_Set_IRQ(&z80_runtime.cpu, 0, (int32_t)command.value);
				}
				context->z80_progress->irqs++;
				me_z80_publish_status(context, &z80_runtime,
					context->progress->emulated_time);
				meCoreDcacheWritebackRange(context->z80_progress,
					sizeof(*context->z80_progress));
				send_response = false;
				break;

			case PSP_ME_SOUND_WORKER_COMMAND_YM_TIMER:
				if (command.generation != context->progress->generation ||
					!z80_runtime.initialized || !z80_runtime.ym_context ||
					command.value > 1u ||
					z80_runtime.mode == PSP_ME_SOUND_Z80_MODE_AUTONOMOUS)
				{
					me_fail(context, context->progress->generation, command.token,
						PSP_ME_SOUND_WORKER_ERROR_PROTOCOL);
					return;
				}
				if (command.emulated_time < context->progress->emulated_time)
				{
					me_fail_time_regression(context, &command);
					return;
				}
				context->progress->emulated_time = command.emulated_time;
				z80_runtime.ym_timer_overflows++;
				(void)YM2610ContextTimerOver(z80_runtime.ym_context,
					(int)command.value);
				if (z80_runtime.mismatch != PSP_ME_SOUND_Z80_MISMATCH_NONE)
				{
					context->z80_progress->io_mismatches++;
					context->z80_progress->last_mismatch = z80_runtime.mismatch;
					meCoreDcacheWritebackRange(context->z80_progress,
						sizeof(*context->z80_progress));
					me_fail(context, context->progress->generation, command.token,
						PSP_ME_SOUND_WORKER_ERROR_Z80_TRACE);
					return;
				}
				me_z80_publish_status(context, &z80_runtime,
					context->progress->emulated_time);
				send_response = false;
				break;

			case PSP_ME_SOUND_WORKER_COMMAND_Z80_SLICE:
				if (command.generation != context->progress->generation ||
					!z80_runtime.initialized)
				{
					me_fail(context, context->progress->generation, command.token,
						PSP_ME_SOUND_WORKER_ERROR_GENERATION);
					return;
				}
				if (command.emulated_time < context->progress->emulated_time)
				{
					me_fail_time_regression(context, &command);
					return;
				}
				if (!me_z80_execute_slice(context, &z80_runtime,
					context->z80_progress, command.value))
				{
					meCoreDcacheWritebackRange(context->z80_progress,
						sizeof(*context->z80_progress));
					me_fail(context, context->progress->generation, command.token,
						context->z80_progress->last_mismatch <=
							PSP_ME_SOUND_Z80_MISMATCH_IO_REMAINDER ?
							PSP_ME_SOUND_WORKER_ERROR_Z80_TRACE :
							PSP_ME_SOUND_WORKER_ERROR_Z80_STATE);
					return;
				}
				context->progress->emulated_time = command.emulated_time;
				me_z80_publish_status(context, &z80_runtime,
					context->progress->emulated_time);
				meCoreDcacheWritebackRange(context->z80_progress,
					sizeof(*context->z80_progress));
				send_response = false;
				break;

			case PSP_ME_SOUND_WORKER_COMMAND_Z80_ADVANCE:
				if (command.generation != context->progress->generation ||
					!z80_runtime.initialized)
				{
					me_fail(context, context->progress->generation, command.token,
						PSP_ME_SOUND_WORKER_ERROR_GENERATION);
					return;
				}
				if (command.emulated_time < context->progress->emulated_time)
				{
					me_fail_time_regression(context, &command);
					return;
				}
				if (!me_z80_advance_autonomous(&z80_runtime,
						context->z80_progress, command.value, command.reserved))
				{
					meCoreDcacheWritebackRange(context->z80_progress,
						sizeof(*context->z80_progress));
					me_fail(context, context->progress->generation, command.token,
						PSP_ME_SOUND_WORKER_ERROR_Z80_STATE);
					return;
				}
				context->progress->emulated_time = command.emulated_time;
				if (z80_runtime.mode == PSP_ME_SOUND_Z80_MODE_AUTONOMOUS)
					z80_runtime.z80_time = command.emulated_time;
				me_z80_publish_status(context, &z80_runtime,
					context->progress->emulated_time);
				meCoreDcacheWritebackRange(context->z80_progress,
					sizeof(*context->z80_progress));
				send_response = false;
				break;

			case PSP_ME_SOUND_WORKER_COMMAND_Z80_ADVANCE_HORIZON:
				if (command.generation != context->progress->generation ||
					!z80_runtime.initialized)
				{
					me_fail(context, context->progress->generation, command.token,
						PSP_ME_SOUND_WORKER_ERROR_GENERATION);
					return;
				}
				if (command.emulated_time < context->progress->emulated_time)
				{
					me_fail_time_regression(context, &command);
					return;
				}
				if (!me_z80_advance_horizon(&z80_runtime,
						context->z80_progress, command.emulated_time,
						command.reserved))
				{
					meCoreDcacheWritebackRange(context->z80_progress,
						sizeof(*context->z80_progress));
					me_fail(context, context->progress->generation, command.token,
						PSP_ME_SOUND_WORKER_ERROR_Z80_STATE);
					return;
				}
				if (z80_runtime.z80_time > context->progress->emulated_time)
					context->progress->emulated_time = z80_runtime.z80_time;
				me_z80_publish_status(context, &z80_runtime,
					context->progress->emulated_time);
				meCoreDcacheWritebackRange(context->z80_progress,
					sizeof(*context->z80_progress));
				send_response = false;
				break;

			case PSP_ME_SOUND_WORKER_COMMAND_Z80_CHECKPOINT:
				if (command.generation != context->progress->generation ||
					!z80_runtime.initialized)
				{
					me_fail(context, context->progress->generation, command.token,
						PSP_ME_SOUND_WORKER_ERROR_GENERATION);
					return;
				}
				if (command.emulated_time < context->progress->emulated_time)
				{
					me_fail_time_regression(context, &command);
					return;
				}
				if (!me_ym_process_due_timers(&z80_runtime))
				{
					me_fail(context, context->progress->generation, command.token,
						PSP_ME_SOUND_WORKER_ERROR_Z80_STATE);
					return;
				}
				if (!me_z80_check_checkpoint(context, &z80_runtime,
						context->z80_progress, command.value))
				{
					meCoreDcacheWritebackRange(context->z80_progress,
						sizeof(*context->z80_progress));
					me_fail(context, context->progress->generation, command.token,
						PSP_ME_SOUND_WORKER_ERROR_Z80_STATE);
					return;
				}
				context->progress->emulated_time = command.emulated_time;
				me_z80_publish_status(context, &z80_runtime,
					context->progress->emulated_time);
				meCoreDcacheWritebackRange(context->z80_progress,
					sizeof(*context->z80_progress));
				event.type = PSP_ME_SOUND_WORKER_EVENT_Z80_CHECKPOINT_ACK;
				event.emulated_time = context->progress->emulated_time;
				break;

			case PSP_ME_SOUND_WORKER_COMMAND_YM_RENDER_PREPARE:
				{
					psp_me_sound_ym_render_job_t *job = context->ym_render_job;

					if (command.generation != context->progress->generation ||
						!z80_runtime.initialized || !z80_runtime.ym_context || !job)
					{
						me_fail(context, context->progress->generation, command.token,
							PSP_ME_SOUND_WORKER_ERROR_PROTOCOL);
						return;
					}
					meCoreDcacheInvalidateRange(job,
						PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*job)));
					if (job->generation != command.generation ||
						job->token != command.token || job->samples == 0 ||
						job->samples > PSP_ME_SOUND_YM_RENDER_MAX_SAMPLES)
					{
						context->z80_progress->ym_render_errors++;
						job->error = 1;
					}
					else if (!me_ym_process_due_timers(&z80_runtime))
					{
						context->z80_progress->ym_render_errors++;
						job->error = 4u;
					}
					else
					{
						me_z80_publish_status(context, &z80_runtime,
							command.emulated_time);
						job->error = YM2610ContextPreparePcmWindow(
							z80_runtime.ym_context, job->samples, &job->window) ? 0u : 3u;
						if (job->error != 0)
							context->z80_progress->ym_render_errors++;
					}
					meCoreDcacheWritebackRange(job,
						PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*job)));
					meCoreDcacheWritebackRange(context->z80_progress,
						sizeof(*context->z80_progress));
					event.type = PSP_ME_SOUND_WORKER_EVENT_YM_RENDER_PREPARE_ACK;
					event.emulated_time = command.emulated_time;
					event.value = job->error;
				}
				break;

			case PSP_ME_SOUND_WORKER_COMMAND_YM_RENDER:
				{
					psp_me_sound_ym_render_job_t *job = context->ym_render_job;
					int32_t *buffers[2];

					if (command.generation != context->progress->generation ||
						!z80_runtime.initialized || !z80_runtime.ym_context || !job)
					{
						me_fail(context, context->progress->generation, command.token,
							PSP_ME_SOUND_WORKER_ERROR_PROTOCOL);
						return;
					}
					meCoreDcacheInvalidateRange(job,
						PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*job)));
					if (job->generation != command.generation ||
						job->token != command.token || job->samples == 0 ||
						job->samples > PSP_ME_SOUND_YM_RENDER_MAX_SAMPLES ||
						job->window.samples != job->samples)
					{
						context->z80_progress->ym_render_errors++;
						job->error = 1;
					}
					else
					{
						buffers[0] = job->left;
						buffers[1] = job->right;
						job->error = YM2610ContextUpdatePcmWindow(z80_runtime.ym_context,
							buffers, (int)job->samples, &job->window) ? 0u : 2u;
						job->status_b = YM2610ContextRead(z80_runtime.ym_context, 2);
						if (job->error != 0)
							context->z80_progress->ym_render_errors++;
						else
						{
							context->z80_progress->ym_renders++;
							context->z80_progress->ym_render_samples += job->samples;
							meCoreDcacheWritebackRange(z80_runtime.ym_context,
								PSP_ME_SOUND_WORKER_CACHE_SIZE(YM2610ContextSize()));
						}
					}
					meCoreDcacheWritebackRange(job,
						PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*job)));
					meCoreDcacheWritebackRange(context->z80_progress,
						sizeof(*context->z80_progress));
					event.type = PSP_ME_SOUND_WORKER_EVENT_YM_RENDER_ACK;
					event.emulated_time = command.emulated_time;
					event.value = job->error;
				}
				break;

		default:
			me_fail(context, context->progress->generation, command.token,
				PSP_ME_SOUND_WORKER_ERROR_PROTOCOL);
			return;
		}

			me_publish_progress(context->progress);
			if (send_response && !me_send_event(context, &event))
		{
			me_fail(context, context->progress->generation, command.token,
				PSP_ME_SOUND_WORKER_ERROR_RING);
			return;
		}
	}
}

static void free_shared_state(psp_me_sound_worker_t *worker)
{
	free(worker->commands);
	free(worker->events);
	free(worker->z80_batches);
	free(worker->shared_context);
	free(worker->main_control);
	free(worker->progress);
	free(worker->z80_progress);
	free(worker->ym_context);
	free(worker->z80_snapshot);
	free(worker->status_snapshot);
	free(worker->recovery_snapshot);
	free(worker->z80_memory);
	free(worker->ym_render_job);
	worker->commands = NULL;
	worker->events = NULL;
	worker->z80_batches = NULL;
	worker->shared_context = NULL;
	worker->main_control = NULL;
	worker->progress = NULL;
	worker->z80_progress = NULL;
	worker->ym_context = NULL;
	worker->z80_snapshot = NULL;
	worker->status_snapshot = NULL;
	worker->recovery_snapshot = NULL;
	worker->z80_memory = NULL;
	worker->ym_render_job = NULL;
	worker->ring_size = 0;
	worker->z80_batch_ring_size = 0;
	worker->capacity = 0;
}

static void snapshot_stats(psp_me_sound_worker_t *worker)
{
	psp_me_sound_worker_progress_t *progress;
	psp_me_sound_z80_progress_t *z80_progress;
	psp_me_sound_status_snapshot_t *status;

	memset(&worker->last_stats, 0, sizeof(worker->last_stats));
	if (!worker->progress || !worker->z80_progress || !worker->commands ||
		!worker->events || !worker->z80_batches)
		return;

	progress = (psp_me_sound_worker_progress_t *)worker->progress;
	z80_progress = (psp_me_sound_z80_progress_t *)worker->z80_progress;
	status = worker->status_snapshot;
	sceKernelDcacheInvalidateRange(progress, sizeof(*progress));
	sceKernelDcacheInvalidateRange(z80_progress, sizeof(*z80_progress));
	if (status)
		sceKernelDcacheInvalidateRange(status, sizeof(*status));
	psp_me_spsc_ring_acquire_initial(worker->commands, &allegrex_cache_ops);
	psp_me_spsc_ring_acquire_initial(worker->events, &allegrex_cache_ops);
	psp_me_spsc_ring_acquire_initial(worker->z80_batches, &allegrex_cache_ops);

	worker->last_stats.generation = progress->generation;
	worker->last_stats.commands_processed = progress->commands_processed;
	worker->last_stats.resets = progress->resets;
	worker->last_stats.syncs = progress->syncs;
	worker->last_stats.shutdowns = progress->shutdowns;
	worker->last_stats.shadow_commands = progress->shadow_commands;
	worker->last_stats.heartbeat = progress->heartbeat;
	worker->last_stats.fatal_error = progress->fatal_error;
	worker->last_stats.last_command_type = progress->last_command_type;
	worker->last_stats.emulated_time = progress->emulated_time;
	worker->last_stats.fatal_emulated_time = progress->fatal_emulated_time;
	worker->last_stats.command_high_water = worker->commands->producer.high_water;
	worker->last_stats.command_overflow = worker->commands->producer.overflow_count;
	worker->last_stats.command_underflow = worker->commands->consumer.underflow_count;
	worker->last_stats.event_high_water = worker->events->producer.high_water;
	worker->last_stats.event_overflow = worker->events->producer.overflow_count;
	worker->last_stats.event_underflow = worker->events->consumer.underflow_count;
	worker->last_stats.shadow_sent = worker->shadow_sent;
	worker->last_stats.shadow_matched = worker->shadow_matched;
	worker->last_stats.shadow_mismatches = worker->shadow_mismatches;
	worker->last_stats.shadow_send_failures = worker->shadow_send_failures;
	worker->last_stats.shadow_pending = worker->shadow_expected_count;
	worker->last_stats.shadow_pending_high_water = worker->shadow_pending_high_water;
	worker->last_stats.z80_snapshots = z80_progress->snapshots;
	worker->last_stats.z80_irqs = z80_progress->irqs;
	worker->last_stats.z80_slices = z80_progress->slices;
	worker->last_stats.z80_io_events = z80_progress->io_events;
	worker->last_stats.z80_state_mismatches = z80_progress->state_mismatches;
	worker->last_stats.z80_ram_mismatches = z80_progress->ram_mismatches;
	worker->last_stats.z80_bank_mismatches = z80_progress->bank_mismatches;
	worker->last_stats.z80_io_mismatches = z80_progress->io_mismatches;
	worker->last_stats.z80_send_failures = worker->z80_send_failures;
	worker->last_stats.z80_last_mismatch = z80_progress->last_mismatch;
	worker->last_stats.z80_batch_high_water = worker->z80_batches->producer.high_water;
	worker->last_stats.z80_batch_overflow = worker->z80_batches->producer.overflow_count;
	worker->last_stats.z80_batch_underflow = worker->z80_batches->consumer.underflow_count;
	worker->last_stats.z80_autonomous_slices = z80_progress->autonomous_slices;
	worker->last_stats.z80_checkpoints = z80_progress->checkpoints;
	if (status && status->generation == progress->generation)
	{
		worker->last_stats.ym_timer_callbacks = status->ym_timer_callbacks;
		worker->last_stats.ym_timer_overflows = status->ym_timer_overflows;
	}
	worker->last_stats.ym_renders = z80_progress->ym_renders;
	worker->last_stats.ym_render_samples = z80_progress->ym_render_samples;
	worker->last_stats.ym_render_errors = z80_progress->ym_render_errors;
	worker->last_stats.ym_presented_renders = worker->ym_presented_renders;
	worker->last_stats.ym_presented_samples = worker->ym_presented_samples;
	worker->last_stats.ym_authoritative_renders = worker->ym_authoritative_renders;
	worker->last_stats.ym_context_sync_failures = worker->ym_context_sync_failures;
	worker->last_stats.ym_pcm_mismatches = worker->ym_pcm_mismatches;
	worker->last_stats.ym_status_mismatches = worker->ym_status_mismatches;
	worker->last_stats.ym_send_failures = worker->ym_send_failures;
	worker->last_stats.ym_first_pcm_mismatch_sample =
		worker->ym_first_pcm_mismatch_sample;
	worker->last_stats.ym_first_pcm_mismatch_channel =
		worker->ym_first_pcm_mismatch_channel;
	worker->last_stats.ym_first_pcm_expected = worker->ym_first_pcm_expected;
	worker->last_stats.ym_first_pcm_actual = worker->ym_first_pcm_actual;
}

static bool consume_shadow_echo(psp_me_sound_worker_t *worker,
	const psp_me_sound_worker_message_t *event)
{
	psp_me_sound_worker_shadow_expected_t *expected;
	bool matched;

	if (worker->shadow_expected_count == 0)
	{
		worker->shadow_mismatches++;
		return false;
	}

	expected = &worker->shadow_expected[worker->shadow_expected_head];
	matched = event->type == PSP_ME_SOUND_WORKER_EVENT_SHADOW_SOUND_ECHO &&
		event->token == expected->token &&
		event->generation == expected->generation &&
		event->emulated_time == expected->emulated_time &&
		event->value == expected->command;
	worker->shadow_expected_head = (worker->shadow_expected_head + 1u) &
		(PSP_ME_SOUND_WORKER_SHADOW_EXPECTED_CAPACITY - 1u);
	worker->shadow_expected_count--;
	if (matched)
		worker->shadow_matched++;
	else
		worker->shadow_mismatches++;
	return matched;
}

static bool timed_out(uint64_t start_us, uint64_t timeout_us)
{
	return timeout_us != 0 && sceKernelGetSystemTimeWide() - start_us >= timeout_us;
}

static bool wait_event(psp_me_sound_worker_t *worker, uint32_t expected_type,
	uint32_t expected_generation, uint32_t expected_token, uint64_t timeout_us,
	psp_me_sound_worker_message_t *event_out)
{
	uint64_t start_us = sceKernelGetSystemTimeWide();

	for (;;)
	{
		psp_me_sound_worker_message_t event;
		psp_me_spsc_ring_result_t result = psp_me_spsc_ring_try_pop(worker->events,
			&allegrex_cache_ops, &event, NULL);

		if (result == PSP_ME_SPSC_RING_OK)
		{
			if (event.type == PSP_ME_SOUND_WORKER_EVENT_ERROR)
				return false;
			if (event.type == PSP_ME_SOUND_WORKER_EVENT_FENCE_ACK &&
				worker->fence_in_flight &&
				event.generation == worker->generation &&
				event.token == worker->fence_token)
			{
				worker->fence_in_flight = false;
				worker->fence_token = 0;
				continue;
			}
			if (event.type == PSP_ME_SOUND_WORKER_EVENT_SHADOW_SOUND_ECHO)
			{
				if (!consume_shadow_echo(worker, &event))
					return false;
				continue;
			}
			if (event.type != expected_type || event.generation != expected_generation ||
				event.token != expected_token)
				return false;
			if (event_out)
				*event_out = event;
			return true;
		}
		if (result != PSP_ME_SPSC_RING_EMPTY)
			return false;
		if (timed_out(start_us, timeout_us))
			return false;
	}
}

static bool send_command(psp_me_sound_worker_t *worker,
	const psp_me_sound_worker_message_t *command, uint64_t timeout_us)
{
	uint64_t start_us = sceKernelGetSystemTimeWide();

	for (;;)
	{
		psp_me_spsc_ring_result_t result = psp_me_spsc_ring_try_push(worker->commands,
			&allegrex_cache_ops, command, NULL);
		if (result == PSP_ME_SPSC_RING_OK)
			return true;
		if (result != PSP_ME_SPSC_RING_FULL)
			return false;
		if (timed_out(start_us, timeout_us))
			return false;
	}
}

bool psp_me_sound_worker_start(psp_me_sound_worker_t *worker,
	const psp_me_sound_worker_dispatch_t *dispatch, uint32_t capacity,
	uint64_t timeout_us)
{
	psp_me_sound_worker_shared_context_t *context;
	psp_me_sound_worker_main_control_t *control;
	psp_me_sound_worker_progress_t *progress;
	psp_me_sound_z80_progress_t *z80_progress;

	if (!worker || !dispatch || !dispatch->start || !dispatch->wait ||
		capacity == 0 || (capacity & (capacity - 1u)) != 0 || worker->running)
		return false;

	memset(worker, 0, sizeof(*worker));
	worker->dispatch = *dispatch;
	worker->capacity = capacity;
	worker->ring_size = psp_me_spsc_ring_storage_size(capacity,
		sizeof(psp_me_sound_worker_message_t));
	worker->z80_batch_ring_size = psp_me_spsc_ring_storage_size(
		PSP_ME_SOUND_Z80_BATCH_CAPACITY, sizeof(psp_me_sound_z80_slice_t));
	if (worker->ring_size == 0 || worker->z80_batch_ring_size == 0)
		return false;

	worker->commands = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE, worker->ring_size);
	worker->events = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE, worker->ring_size);
	worker->z80_batches = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE,
		worker->z80_batch_ring_size);
	worker->shared_context = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE,
		sizeof(psp_me_sound_worker_shared_context_t));
	worker->main_control = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE,
		sizeof(psp_me_sound_worker_main_control_t));
	worker->progress = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE,
		sizeof(psp_me_sound_worker_progress_t));
	worker->z80_progress = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE,
		sizeof(psp_me_sound_z80_progress_t));
	worker->ym_context = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(YM2610ContextSize()));
	worker->z80_snapshot = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(psp_me_sound_z80_snapshot_t)));
	worker->status_snapshot = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(psp_me_sound_status_snapshot_t)));
	worker->recovery_snapshot = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(psp_me_sound_recovery_snapshot_t)));
	worker->z80_memory = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE,
		PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE);
	worker->ym_render_job = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(psp_me_sound_ym_render_job_t)));
	if (!worker->commands || !worker->events || !worker->z80_batches ||
		!worker->shared_context || !worker->main_control || !worker->progress ||
		!worker->z80_progress || !worker->ym_context || !worker->z80_snapshot ||
		!worker->status_snapshot || !worker->recovery_snapshot ||
		!worker->z80_memory || !worker->ym_render_job)
	{
		worker->last_stats.fatal_error = PSP_ME_SOUND_WORKER_ERROR_ALLOCATION;
		free_shared_state(worker);
		return false;
	}

	if (!psp_me_spsc_ring_init(worker->commands, worker->ring_size, capacity,
			sizeof(psp_me_sound_worker_message_t)) ||
		!psp_me_spsc_ring_init(worker->events, worker->ring_size, capacity,
			sizeof(psp_me_sound_worker_message_t)) ||
		!psp_me_spsc_ring_init(worker->z80_batches, worker->z80_batch_ring_size,
			PSP_ME_SOUND_Z80_BATCH_CAPACITY, sizeof(psp_me_sound_z80_slice_t)))
	{
		worker->last_stats.fatal_error = PSP_ME_SOUND_WORKER_ERROR_RING;
		free_shared_state(worker);
		return false;
	}

	control = (psp_me_sound_worker_main_control_t *)worker->main_control;
	progress = (psp_me_sound_worker_progress_t *)worker->progress;
	z80_progress = (psp_me_sound_z80_progress_t *)worker->z80_progress;
	context = (psp_me_sound_worker_shared_context_t *)worker->shared_context;
	memset(control, 0, sizeof(*control));
	memset(progress, 0, sizeof(*progress));
	memset(z80_progress, 0, sizeof(*z80_progress));
	memset(worker->ym_context, 0,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(YM2610ContextSize()));
	memset(worker->z80_snapshot, 0,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*worker->z80_snapshot)));
	memset(worker->status_snapshot, 0,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*worker->status_snapshot)));
	memset(worker->recovery_snapshot, 0,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*worker->recovery_snapshot)));
	memset(worker->z80_memory, 0, PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE);
	memset(worker->ym_render_job, 0,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(psp_me_sound_ym_render_job_t)));
	memset(context, 0, sizeof(*context));
	context->commands = worker->commands;
	context->events = worker->events;
	context->z80_batches = worker->z80_batches;
	context->main_control = control;
	context->progress = progress;
	context->z80_progress = z80_progress;
	context->ym_context = worker->ym_context;
	context->z80_snapshot = worker->z80_snapshot;
	context->status_snapshot = worker->status_snapshot;
	context->recovery_snapshot = worker->recovery_snapshot;
	context->z80_memory = worker->z80_memory;
	context->ym_render_job = worker->ym_render_job;

	psp_me_spsc_ring_publish_initial(worker->commands, &allegrex_cache_ops);
	psp_me_spsc_ring_publish_initial(worker->events, &allegrex_cache_ops);
	psp_me_spsc_ring_publish_initial(worker->z80_batches, &allegrex_cache_ops);
	sceKernelDcacheWritebackInvalidateRange(control, sizeof(*control));
	sceKernelDcacheWritebackInvalidateRange(progress, sizeof(*progress));
	sceKernelDcacheWritebackInvalidateRange(z80_progress, sizeof(*z80_progress));
	sceKernelDcacheWritebackInvalidateRange(worker->ym_context,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(YM2610ContextSize()));
	sceKernelDcacheWritebackInvalidateRange(worker->z80_snapshot,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*worker->z80_snapshot)));
	sceKernelDcacheWritebackInvalidateRange(worker->status_snapshot,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*worker->status_snapshot)));
	sceKernelDcacheWritebackInvalidateRange(worker->recovery_snapshot,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*worker->recovery_snapshot)));
	sceKernelDcacheWritebackInvalidateRange(worker->z80_memory,
		PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE);
	sceKernelDcacheWritebackInvalidateRange(worker->ym_render_job,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(psp_me_sound_ym_render_job_t)));
	sceKernelDcacheWritebackInvalidateRange(context, sizeof(*context));

	if (!worker->dispatch.start(psp_me_sound_worker_entry, context, sizeof(*context),
		worker->dispatch.opaque))
	{
		worker->last_stats.fatal_error = PSP_ME_SOUND_WORKER_ERROR_DISPATCH;
		free_shared_state(worker);
		return false;
	}
	worker->running = true;
	worker->next_token = 1;

	if (!wait_event(worker, PSP_ME_SOUND_WORKER_EVENT_READY, 0, 0, timeout_us, NULL))
	{
		psp_me_sound_worker_abort(worker);
		worker->last_stats.fatal_error = PSP_ME_SOUND_WORKER_ERROR_TIMEOUT;
		return false;
	}
	return true;
}

bool psp_me_sound_worker_reset(psp_me_sound_worker_t *worker,
	uint32_t generation, uint64_t timeout_us)
{
	psp_me_sound_worker_message_t command;
	psp_me_sound_worker_message_t event;

	if (!worker || !worker->running || generation == 0)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_RESET;
	command.generation = generation;
	command.token = worker->next_token++;
	if (!send_command(worker, &command, timeout_us) ||
		!wait_event(worker, PSP_ME_SOUND_WORKER_EVENT_RESET_ACK, generation,
			command.token, timeout_us, &event))
		return false;
	worker->generation = generation;
	worker->z80_next_sequence = 0;
	worker->ym_render_in_flight = false;
	worker->ym_render_token = 0;
	worker->fence_in_flight = false;
	worker->fence_token = 0;
	return event.emulated_time == 0;
}

bool psp_me_sound_worker_sync(psp_me_sound_worker_t *worker,
	uint64_t emulated_time, uint64_t timeout_us)
{
	psp_me_sound_worker_message_t command;
	psp_me_sound_worker_message_t event;

	if (!worker || !worker->running || worker->generation == 0)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_SYNC;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	command.emulated_time = emulated_time;
	if (!send_command(worker, &command, timeout_us) ||
		!wait_event(worker, PSP_ME_SOUND_WORKER_EVENT_SYNC_ACK, worker->generation,
			command.token, timeout_us, &event))
		return false;
	return event.emulated_time == emulated_time;
}

bool psp_me_sound_worker_fence_begin(psp_me_sound_worker_t *worker)
{
	psp_me_sound_worker_message_t command;
	psp_me_spsc_ring_result_t result;

	if (!worker || !worker->running || worker->generation == 0)
		return false;
	if (worker->fence_in_flight)
		return true;
	if (worker->ym_render_in_flight)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_FENCE;
	command.generation = worker->generation;
	command.token = worker->next_token;
	result = psp_me_spsc_ring_try_push(worker->commands, &allegrex_cache_ops,
		&command, NULL);
	if (result != PSP_ME_SPSC_RING_OK)
		return false;
	worker->next_token++;
	worker->fence_token = command.token;
	worker->fence_in_flight = true;
	return true;
}

psp_me_sound_fence_result_t psp_me_sound_worker_fence_poll(
	psp_me_sound_worker_t *worker, uint64_t *emulated_time)
{
	if (!worker || !worker->running || worker->generation == 0 ||
		!worker->fence_in_flight)
		return PSP_ME_SOUND_FENCE_FAILED;

	for (;;)
	{
		psp_me_sound_worker_message_t event;
		psp_me_spsc_ring_result_t result = psp_me_spsc_ring_try_pop(worker->events,
			&allegrex_cache_ops, &event, NULL);

		if (result == PSP_ME_SPSC_RING_EMPTY)
			return PSP_ME_SOUND_FENCE_PENDING;
		if (result != PSP_ME_SPSC_RING_OK ||
			event.type == PSP_ME_SOUND_WORKER_EVENT_ERROR)
		{
			worker->fence_in_flight = false;
			worker->fence_token = 0;
			return PSP_ME_SOUND_FENCE_FAILED;
		}
		if (event.type == PSP_ME_SOUND_WORKER_EVENT_SHADOW_SOUND_ECHO)
		{
			if (!consume_shadow_echo(worker, &event))
				return PSP_ME_SOUND_FENCE_FAILED;
			continue;
		}
		if (event.type != PSP_ME_SOUND_WORKER_EVENT_FENCE_ACK ||
			event.generation != worker->generation ||
			event.token != worker->fence_token)
		{
			worker->fence_in_flight = false;
			worker->fence_token = 0;
			return PSP_ME_SOUND_FENCE_FAILED;
		}
		worker->fence_in_flight = false;
		worker->fence_token = 0;
		if (emulated_time)
			*emulated_time = event.emulated_time;
		return PSP_ME_SOUND_FENCE_COMPLETE;
	}
}

bool psp_me_sound_worker_fence(psp_me_sound_worker_t *worker,
	uint64_t timeout_us)
{
	uint64_t start_us;

	if (!psp_me_sound_worker_fence_begin(worker))
		return false;
	start_us = sceKernelGetSystemTimeWide();
	for (;;)
	{
		psp_me_sound_fence_result_t result = psp_me_sound_worker_fence_poll(worker, NULL);

		if (result == PSP_ME_SOUND_FENCE_COMPLETE)
			return true;
		if (result == PSP_ME_SOUND_FENCE_FAILED)
			return false;
		if (timed_out(start_us, timeout_us))
			return false;
	}
}

bool psp_me_sound_worker_read_status(psp_me_sound_worker_t *worker,
	psp_me_sound_status_snapshot_t *status)
{
	psp_me_sound_status_snapshot_t *shared;

	if (!worker || !worker->running || worker->generation == 0 || !status ||
		!worker->status_snapshot)
		return false;
	shared = worker->status_snapshot;
	sceKernelDcacheInvalidateRange(shared,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*shared)));
	*status = *shared;
	return status->generation == worker->generation && status->initialized != 0;
}

bool psp_me_sound_worker_read_recovery_snapshot(psp_me_sound_worker_t *worker,
	psp_me_sound_recovery_snapshot_t *snapshot, uint8_t *ram,
	ym2610_context_t *ym_context, uint64_t timeout_us)
{
	psp_me_sound_worker_message_t command;
	psp_me_sound_worker_message_t event;
	psp_me_sound_recovery_snapshot_t *shared;

	if (!worker || !worker->running || worker->generation == 0 || !snapshot ||
		!ram || !worker->recovery_snapshot ||
		!worker->z80_memory || !worker->ym_context || worker->ym_render_in_flight)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_RECOVERY_SNAPSHOT;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	if (!send_command(worker, &command, timeout_us) ||
		!wait_event(worker, PSP_ME_SOUND_WORKER_EVENT_RECOVERY_SNAPSHOT_ACK,
			worker->generation, command.token, timeout_us, &event))
		return false;

	shared = worker->recovery_snapshot;
	sceKernelDcacheInvalidateRange(shared,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*shared)));
	sceKernelDcacheInvalidateRange(worker->z80_memory,
		PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE);
	sceKernelDcacheInvalidateRange(worker->ym_context,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(YM2610ContextSize()));
	if (shared->generation != worker->generation || shared->initialized == 0 ||
		shared->mode != PSP_ME_SOUND_Z80_MODE_AUTONOMOUS ||
		shared->ym_timer_arm_elapsed[0] != 0 ||
		shared->ym_timer_arm_elapsed[1] != 0 ||
		shared->irq_state != (uint8_t)shared->state.IRQState ||
		event.emulated_time != shared->emulated_time ||
		(ym_context &&
			!YM2610ContextCloneForPcmWindow(ym_context, worker->ym_context)))
		return false;
	*snapshot = *shared;
	memcpy(ram, worker->z80_memory + PSP_ME_SOUND_Z80_RAM_OFFSET,
		PSP_ME_SOUND_Z80_RAM_SIZE);
	return true;
}

psp_me_sound_status_validation_t psp_me_sound_worker_validate_status(
	const psp_me_sound_status_snapshot_t *status, uint32_t generation,
	uint64_t required_time, uint8_t sound_code, uint8_t pending_command,
	uint8_t result_code)
{
	if (!status || generation == 0 || status->generation != generation ||
		status->initialized == 0)
		return PSP_ME_SOUND_STATUS_UNAVAILABLE;
	if (status->emulated_time < required_time)
		return PSP_ME_SOUND_STATUS_STALE;
	if (status->sound_code != sound_code ||
		status->pending_command != pending_command ||
		status->result_code != result_code)
		return PSP_ME_SOUND_STATUS_MISMATCH;
	return PSP_ME_SOUND_STATUS_MATCH;
}

psp_me_sound_status_validation_t psp_me_sound_worker_present_status(
	const psp_me_sound_status_snapshot_t *status, uint32_t generation,
	uint64_t required_time, uint8_t sound_code, uint8_t pending_command,
	uint8_t result_code, uint8_t *presented_pending,
	uint8_t *presented_result)
{
	psp_me_sound_status_validation_t validation;

	if (!presented_pending || !presented_result)
		return PSP_ME_SOUND_STATUS_UNAVAILABLE;
	validation = psp_me_sound_worker_validate_status(status, generation,
		required_time, sound_code, pending_command, result_code);
	if (validation == PSP_ME_SOUND_STATUS_MATCH)
	{
		*presented_pending = status->pending_command;
		*presented_result = status->result_code;
	}
	return validation;
}

psp_me_sound_status_validation_t psp_me_sound_worker_present_authoritative_status(
	const psp_me_sound_status_snapshot_t *status, uint32_t generation,
	uint64_t required_time, uint8_t *presented_pending,
	uint8_t *presented_result)
{
	if (!status || generation == 0 || status->generation != generation ||
		status->initialized == 0 || !presented_pending || !presented_result)
		return PSP_ME_SOUND_STATUS_UNAVAILABLE;
	if (status->emulated_time < required_time)
		return PSP_ME_SOUND_STATUS_STALE;
	*presented_pending = status->pending_command;
	*presented_result = status->result_code;
	return PSP_ME_SOUND_STATUS_MATCH;
}

bool psp_me_sound_worker_shadow_sound(psp_me_sound_worker_t *worker,
	uint8_t command_value, uint64_t emulated_time)
{
	psp_me_sound_worker_message_t command;
	psp_me_sound_worker_shadow_expected_t *expected;
	psp_me_spsc_ring_result_t result;
	uint32_t tail;

	if (!worker || !worker->running || worker->generation == 0)
		return false;
	if (!psp_me_sound_worker_poll(worker))
		return false;
	if (worker->shadow_expected_count >= PSP_ME_SOUND_WORKER_SHADOW_EXPECTED_CAPACITY)
	{
		worker->shadow_send_failures++;
		return false;
	}

	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_SHADOW_SOUND;
	command.generation = worker->generation;
	command.token = worker->next_token;
	command.emulated_time = emulated_time;
	command.value = command_value;
	result = psp_me_spsc_ring_try_push(worker->commands, &allegrex_cache_ops,
		&command, NULL);
	if (result != PSP_ME_SPSC_RING_OK)
	{
		worker->shadow_send_failures++;
		return false;
	}

	worker->next_token++;
	tail = (worker->shadow_expected_head + worker->shadow_expected_count) &
		(PSP_ME_SOUND_WORKER_SHADOW_EXPECTED_CAPACITY - 1u);
	expected = &worker->shadow_expected[tail];
	expected->token = command.token;
	expected->generation = command.generation;
	expected->emulated_time = command.emulated_time;
	expected->command = command.value;
	worker->shadow_expected_count++;
	worker->shadow_sent++;
	if (worker->shadow_expected_count > worker->shadow_pending_high_water)
		worker->shadow_pending_high_water = worker->shadow_expected_count;
	return true;
}

bool psp_me_sound_worker_z80_snapshot(psp_me_sound_worker_t *worker,
	const cz80_state_t *state, const uint8_t *visible_memory,
	const uint8_t *source_rom, uint32_t source_length, const uint32_t banks[4],
	uint8_t sound_code, uint8_t pending_command, uint8_t result_code,
	uint32_t ym_sample_rate, uint32_t ym_pcm_a_size, uint32_t ym_pcm_b_size,
	bool clone_default_ym, psp_me_sound_z80_mode_t mode, uint64_t timeout_us)
{
	psp_me_sound_worker_message_t command;
	psp_me_sound_z80_snapshot_t *snapshot;

	if (!worker || !worker->running || worker->generation == 0 || !state ||
		!visible_memory || !source_rom || source_length < PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE ||
		!banks || !worker->ym_context || !worker->z80_snapshot || !worker->z80_memory ||
		ym_sample_rate == 0 ||
		(mode != PSP_ME_SOUND_Z80_MODE_ORACLE &&
			mode != PSP_ME_SOUND_Z80_MODE_AUTONOMOUS))
		return false;

	snapshot = worker->z80_snapshot;
	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->state = *state;
	snapshot->source_rom = source_rom;
	snapshot->source_length = source_length;
	memcpy(snapshot->banks, banks, sizeof(snapshot->banks));
	snapshot->generation = worker->generation;
	snapshot->sound_code = sound_code;
	snapshot->pending_command = pending_command;
	snapshot->result_code = result_code;
	snapshot->mode = (uint8_t)mode;
	snapshot->ym_sample_rate = ym_sample_rate;
	snapshot->ym_pcm_a_size = ym_pcm_a_size;
	snapshot->ym_pcm_b_size = ym_pcm_b_size;
	memcpy(worker->z80_memory, visible_memory, PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE);

	/* Production C5 snapshots the live authoritative YM so render phase,
	 * envelopes and ADPCM decoder state start at the exact same boundary as
	 * Allegrex. Synthetic worker tests can still request a fresh isolated YM. */
	if (clone_default_ym)
	{
		if (!YM2610DefaultCloneForPcmWindow((ym2610_context_t *)worker->ym_context))
			return false;
	}
	else
	{
		YM2610ContextInit((ym2610_context_t *)worker->ym_context, 8000000,
			(int)ym_sample_rate, NULL, (int)ym_pcm_a_size,
#if (EMU_SYSTEM == MVS)
			NULL, (int)ym_pcm_b_size,
#endif
			NULL, NULL, NULL);
#if (EMU_SYSTEM == MVS)
		YM2610ContextEnablePcmWindowSource((ym2610_context_t *)worker->ym_context,
			ym_pcm_a_size, ym_pcm_b_size);
#endif
	}

	/* CZ80's immutable flag tables were initialized by Allegrex. Publish the
	 * cache once per shadow generation before ME starts executing the core. */
	sceKernelDcacheWritebackAll();
	sceKernelDcacheWritebackInvalidateRange(snapshot,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*snapshot)));
	sceKernelDcacheWritebackInvalidateRange(worker->z80_memory,
		PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE);
	sceKernelDcacheWritebackInvalidateRange(worker->ym_context,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(YM2610ContextSize()));
	sceKernelDcacheWritebackInvalidateRange((void *)source_rom, source_length);

	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_Z80_SNAPSHOT;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	if (!send_command(worker, &command, timeout_us) ||
		!wait_event(worker, PSP_ME_SOUND_WORKER_EVENT_Z80_SNAPSHOT_ACK,
			worker->generation, command.token, timeout_us, NULL))
	{
		worker->z80_send_failures++;
		return false;
	}
	return true;
}

bool psp_me_sound_worker_ym_timer(psp_me_sound_worker_t *worker,
	uint32_t channel, uint64_t emulated_time)
{
	psp_me_sound_worker_message_t command;
	psp_me_spsc_ring_result_t result;

	if (!worker || !worker->running || worker->generation == 0 || channel > 1u)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_YM_TIMER;
	command.generation = worker->generation;
	command.token = worker->next_token;
	command.emulated_time = emulated_time;
	command.value = channel;
	result = psp_me_spsc_ring_try_push(worker->commands, &allegrex_cache_ops,
		&command, NULL);
	if (result != PSP_ME_SPSC_RING_OK)
	{
		worker->z80_send_failures++;
		return false;
	}
	worker->next_token++;
	return true;
}

bool psp_me_sound_worker_ym_render_prepare(psp_me_sound_worker_t *worker,
	uint32_t samples, uint64_t emulated_time, ym2610_pcm_window_t *window,
	uint64_t timeout_us)
{
	psp_me_sound_ym_render_job_t *job;
	psp_me_sound_worker_message_t command;
	psp_me_sound_worker_message_t event;

	if (!worker || !worker->running || worker->generation == 0 || !window ||
		!worker->ym_render_job || worker->ym_render_in_flight || samples == 0 ||
		samples > PSP_ME_SOUND_YM_RENDER_MAX_SAMPLES)
		return false;

	job = (psp_me_sound_ym_render_job_t *)worker->ym_render_job;
	memset(job, 0, sizeof(*job));
	job->generation = worker->generation;
	job->token = worker->next_token;
	job->samples = samples;
	sceKernelDcacheWritebackInvalidateRange(job,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*job)));

	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_YM_RENDER_PREPARE;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	command.emulated_time = emulated_time;
	if (!send_command(worker, &command, timeout_us) ||
		!wait_event(worker, PSP_ME_SOUND_WORKER_EVENT_YM_RENDER_PREPARE_ACK,
			worker->generation, command.token, timeout_us, &event))
	{
		worker->ym_send_failures++;
		return false;
	}
	sceKernelDcacheInvalidateRange(job,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*job)));
	if (event.value != 0 || job->error != 0 ||
		job->generation != worker->generation || job->token != command.token ||
		job->samples != samples || job->window.samples != samples)
	{
		worker->ym_send_failures++;
		return false;
	}
	*window = job->window;
	return true;
}

bool psp_me_sound_worker_ym_render_begin(psp_me_sound_worker_t *worker,
	const ym2610_pcm_window_t *window, uint64_t emulated_time,
	uint64_t timeout_us)
{
	psp_me_sound_ym_render_job_t *job;
	psp_me_sound_worker_message_t command;

	if (!worker || !worker->running || worker->generation == 0 || !window ||
		!worker->ym_render_job || worker->ym_render_in_flight ||
		window->samples == 0 || window->samples > PSP_ME_SOUND_YM_RENDER_MAX_SAMPLES)
		return false;

	job = (psp_me_sound_ym_render_job_t *)worker->ym_render_job;
	memset(job, 0, sizeof(*job));
	job->generation = worker->generation;
	job->token = worker->next_token;
	job->samples = window->samples;
	job->window = *window;
	sceKernelDcacheWritebackInvalidateRange(job,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*job)));

	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_YM_RENDER;
	command.generation = worker->generation;
	command.token = worker->next_token;
	command.emulated_time = emulated_time;
	if (!send_command(worker, &command, timeout_us))
	{
		worker->ym_send_failures++;
		return false;
	}
	worker->next_token++;
	worker->ym_render_token = command.token;
	worker->ym_render_in_flight = true;
	return true;
}

static bool psp_me_sound_worker_ym_render_finish_internal(psp_me_sound_worker_t *worker,
	const int32_t *expected_left, const int32_t *expected_right,
	int32_t *present_left, int32_t *present_right,
	uint32_t samples, uint8_t expected_status_b, uint64_t timeout_us)
{
	psp_me_sound_ym_render_job_t *job;
	psp_me_sound_worker_message_t event;
	bool pcm_matches;
	bool status_matches;

	if (!worker || !worker->running || !worker->ym_render_in_flight ||
		!worker->ym_render_job || !expected_left || !expected_right ||
		samples == 0 || samples > PSP_ME_SOUND_YM_RENDER_MAX_SAMPLES)
		return false;

	if (!wait_event(worker, PSP_ME_SOUND_WORKER_EVENT_YM_RENDER_ACK,
		worker->generation, worker->ym_render_token, timeout_us, &event))
	{
		worker->ym_send_failures++;
		worker->ym_render_in_flight = false;
		return false;
	}
	job = (psp_me_sound_ym_render_job_t *)worker->ym_render_job;
	sceKernelDcacheInvalidateRange(job,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*job)));
	worker->ym_render_in_flight = false;
	if (event.value != 0 || job->error != 0 || job->generation != worker->generation ||
		job->token != worker->ym_render_token || job->samples != samples)
	{
		worker->ym_send_failures++;
		return false;
	}

	pcm_matches = memcmp(job->left, expected_left, samples * sizeof(*expected_left)) == 0 &&
		memcmp(job->right, expected_right, samples * sizeof(*expected_right)) == 0;
	status_matches = (uint8_t)job->status_b == expected_status_b;
	if (!pcm_matches)
	{
		if (worker->ym_pcm_mismatches == 0)
		{
			uint32_t i;

			for (i = 0; i < samples; i++)
			{
				if (job->left[i] != expected_left[i])
				{
					worker->ym_first_pcm_mismatch_sample = i;
					worker->ym_first_pcm_mismatch_channel = 0;
					worker->ym_first_pcm_expected = expected_left[i];
					worker->ym_first_pcm_actual = job->left[i];
					break;
				}
				if (job->right[i] != expected_right[i])
				{
					worker->ym_first_pcm_mismatch_sample = i;
					worker->ym_first_pcm_mismatch_channel = 1;
					worker->ym_first_pcm_expected = expected_right[i];
					worker->ym_first_pcm_actual = job->right[i];
					break;
				}
			}
		}
		worker->ym_pcm_mismatches++;
	}
	if (!status_matches)
		worker->ym_status_mismatches++;
	if (pcm_matches && status_matches && present_left && present_right)
	{
		memcpy(present_left, job->left, samples * sizeof(*present_left));
		memcpy(present_right, job->right, samples * sizeof(*present_right));
		worker->ym_presented_renders++;
		worker->ym_presented_samples += samples;
	}
	return pcm_matches && status_matches;
}

bool psp_me_sound_worker_ym_render_finish(psp_me_sound_worker_t *worker,
	const int32_t *expected_left, const int32_t *expected_right,
	uint32_t samples, uint8_t expected_status_b, uint64_t timeout_us)
{
	return psp_me_sound_worker_ym_render_finish_internal(worker,
		expected_left, expected_right, NULL, NULL, samples,
		expected_status_b, timeout_us);
}

bool psp_me_sound_worker_ym_render_finish_present(psp_me_sound_worker_t *worker,
	const int32_t *expected_left, const int32_t *expected_right,
	int32_t *present_left, int32_t *present_right,
	uint32_t samples, uint8_t expected_status_b, uint64_t timeout_us)
{
	if (!present_left || !present_right)
		return false;
	return psp_me_sound_worker_ym_render_finish_internal(worker,
		expected_left, expected_right, present_left, present_right, samples,
		expected_status_b, timeout_us);
}

bool psp_me_sound_worker_ym_render_finish_authoritative(
	psp_me_sound_worker_t *worker, int32_t *present_left,
	int32_t *present_right, uint32_t samples, uint64_t timeout_us)
{
	psp_me_sound_ym_render_job_t *job;
	psp_me_sound_worker_message_t event;

	if (!worker || !worker->running || !worker->ym_render_in_flight ||
		!worker->ym_render_job || !worker->ym_context || !present_left ||
		!present_right || samples == 0 ||
		samples > PSP_ME_SOUND_YM_RENDER_MAX_SAMPLES)
		return false;
	if (!wait_event(worker, PSP_ME_SOUND_WORKER_EVENT_YM_RENDER_ACK,
			worker->generation, worker->ym_render_token, timeout_us, &event))
	{
		worker->ym_send_failures++;
		worker->ym_render_in_flight = false;
		return false;
	}
	job = (psp_me_sound_ym_render_job_t *)worker->ym_render_job;
	sceKernelDcacheInvalidateRange(job,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(sizeof(*job)));
	sceKernelDcacheInvalidateRange(worker->ym_context,
		PSP_ME_SOUND_WORKER_CACHE_SIZE(YM2610ContextSize()));
	worker->ym_render_in_flight = false;
	if (event.value != 0 || job->error != 0 ||
		job->generation != worker->generation ||
		job->token != worker->ym_render_token || job->samples != samples)
	{
		worker->ym_send_failures++;
		return false;
	}
	if (YM2610ContextRead((ym2610_context_t *)worker->ym_context, 2) !=
		(uint8_t)job->status_b ||
		!YM2610DefaultRestoreFromPcmWindow(
			(const ym2610_context_t *)worker->ym_context))
	{
		worker->ym_context_sync_failures++;
		return false;
	}
	memcpy(present_left, job->left, samples * sizeof(*present_left));
	memcpy(present_right, job->right, samples * sizeof(*present_right));
	worker->ym_presented_renders++;
	worker->ym_presented_samples += samples;
	worker->ym_authoritative_renders++;
	return true;
}

bool psp_me_sound_worker_z80_irq(psp_me_sound_worker_t *worker,
	int32_t state, uint64_t emulated_time)
{
	psp_me_sound_worker_message_t command;
	psp_me_spsc_ring_result_t result;

	if (!worker || !worker->running || worker->generation == 0)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_Z80_IRQ;
	command.generation = worker->generation;
	command.token = worker->next_token;
	command.emulated_time = emulated_time;
	command.value = (uint32_t)state;
	result = psp_me_spsc_ring_try_push(worker->commands, &allegrex_cache_ops,
		&command, NULL);
	if (result != PSP_ME_SPSC_RING_OK)
	{
		worker->z80_send_failures++;
		return false;
	}
	worker->next_token++;
	return true;
}

bool psp_me_sound_worker_z80_slice(psp_me_sound_worker_t *worker,
	const psp_me_sound_z80_io_t *io, uint32_t io_count, uint32_t cycles,
	uint64_t emulated_time, const cz80_state_t *expected_state,
	const uint32_t banks[4], uint32_t ram_hash, bool check_ram)
{
	psp_me_sound_z80_slice_t slice;
	psp_me_sound_worker_message_t command;
	psp_me_spsc_ring_result_t result;
	uint32_t sequence;

	if (!worker || !worker->running || worker->generation == 0 ||
		(io_count != 0 && !io) || io_count > PSP_ME_SOUND_Z80_IO_CAPACITY ||
		!expected_state || !banks)
		return false;

	sequence = worker->z80_next_sequence + 1u;
	if (sequence == 0)
		sequence = 1u;
	memset(&slice, 0, sizeof(slice));
	slice.expected_state = *expected_state;
	slice.emulated_time = emulated_time;
	slice.generation = worker->generation;
	slice.sequence = sequence;
	slice.cycles = cycles;
	slice.io_count = io_count;
	memcpy(slice.banks, banks, sizeof(slice.banks));
	slice.ram_hash = ram_hash;
	if (check_ram)
		slice.flags |= PSP_ME_SOUND_Z80_SLICE_CHECK_RAM;
	if (io_count != 0)
		memcpy(slice.io, io, io_count * sizeof(*io));

	result = psp_me_spsc_ring_try_push(worker->z80_batches, &allegrex_cache_ops,
		&slice, NULL);
	if (result != PSP_ME_SPSC_RING_OK)
	{
		worker->z80_send_failures++;
		return false;
	}

	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_Z80_SLICE;
	command.generation = worker->generation;
	command.token = worker->next_token;
	command.emulated_time = emulated_time;
	command.value = sequence;
	result = psp_me_spsc_ring_try_push(worker->commands, &allegrex_cache_ops,
		&command, NULL);
	if (result != PSP_ME_SPSC_RING_OK)
	{
		worker->z80_send_failures++;
		return false;
	}
	worker->next_token++;
	worker->z80_next_sequence = sequence;
	return true;
}

bool psp_me_sound_worker_z80_advance(psp_me_sound_worker_t *worker,
	uint32_t cycles, uint32_t scheduler_time_left, uint64_t emulated_time)
{
	psp_me_sound_worker_message_t command;
	psp_me_spsc_ring_result_t result;

	if (!worker || !worker->running || worker->generation == 0 ||
		cycles > (uint32_t)INT32_MAX)
		return false;

	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_Z80_ADVANCE;
	command.generation = worker->generation;
	command.token = worker->next_token;
	command.emulated_time = emulated_time;
	command.value = cycles;
	command.reserved = scheduler_time_left;
	result = psp_me_spsc_ring_try_push(worker->commands, &allegrex_cache_ops,
		&command, NULL);
	if (result != PSP_ME_SPSC_RING_OK)
	{
		worker->z80_send_failures++;
		return false;
	}
	worker->next_token++;
	return true;
}

bool psp_me_sound_worker_z80_advance_horizon(psp_me_sound_worker_t *worker,
	uint64_t horizon_time, uint32_t scheduler_time_left)
{
	psp_me_sound_worker_message_t command;
	psp_me_spsc_ring_result_t result;

	if (!worker || !worker->running || worker->generation == 0)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_Z80_ADVANCE_HORIZON;
	command.generation = worker->generation;
	command.token = worker->next_token;
	command.emulated_time = horizon_time;
	command.reserved = scheduler_time_left;
	result = psp_me_spsc_ring_try_push(worker->commands, &allegrex_cache_ops,
		&command, NULL);
	if (result != PSP_ME_SPSC_RING_OK)
	{
		worker->z80_send_failures++;
		return false;
	}
	worker->next_token++;
	return true;
}

bool psp_me_sound_worker_z80_checkpoint(psp_me_sound_worker_t *worker,
	const cz80_state_t *expected_state, const uint32_t banks[4],
	uint32_t ram_hash, uint64_t emulated_time, uint64_t timeout_us)
{
	psp_me_sound_z80_slice_t checkpoint;
	psp_me_sound_worker_message_t command;
	psp_me_sound_worker_message_t event;
	psp_me_spsc_ring_result_t result;
	uint32_t sequence;

	if (!worker || !worker->running || worker->generation == 0 ||
		!expected_state || !banks)
		return false;

	sequence = worker->z80_next_sequence + 1u;
	if (sequence == 0)
		sequence = 1u;
	memset(&checkpoint, 0, sizeof(checkpoint));
	checkpoint.expected_state = *expected_state;
	checkpoint.emulated_time = emulated_time;
	checkpoint.generation = worker->generation;
	checkpoint.sequence = sequence;
	memcpy(checkpoint.banks, banks, sizeof(checkpoint.banks));
	checkpoint.ram_hash = ram_hash;
	checkpoint.flags = PSP_ME_SOUND_Z80_SLICE_CHECK_RAM;
	result = psp_me_spsc_ring_try_push(worker->z80_batches, &allegrex_cache_ops,
		&checkpoint, NULL);
	if (result != PSP_ME_SPSC_RING_OK)
	{
		worker->z80_send_failures++;
		return false;
	}

	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_Z80_CHECKPOINT;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	command.emulated_time = emulated_time;
	command.value = sequence;
	if (!send_command(worker, &command, timeout_us) ||
		!wait_event(worker, PSP_ME_SOUND_WORKER_EVENT_Z80_CHECKPOINT_ACK,
			worker->generation, command.token, timeout_us, &event))
	{
		worker->z80_send_failures++;
		return false;
	}
	worker->z80_next_sequence = sequence;
	return event.emulated_time == emulated_time;
}

bool psp_me_sound_worker_poll(psp_me_sound_worker_t *worker)
{
	if (!worker || !worker->running)
		return false;

	while (worker->shadow_expected_count != 0)
	{
		psp_me_sound_worker_message_t event;
		psp_me_spsc_ring_result_t result = psp_me_spsc_ring_try_pop(worker->events,
			&allegrex_cache_ops, &event, NULL);

		if (result == PSP_ME_SPSC_RING_EMPTY)
			return true;
		if (result != PSP_ME_SPSC_RING_OK ||
			event.type == PSP_ME_SOUND_WORKER_EVENT_ERROR ||
			event.type != PSP_ME_SOUND_WORKER_EVENT_SHADOW_SOUND_ECHO ||
			!consume_shadow_echo(worker, &event))
			return false;
	}
	return true;
}

bool psp_me_sound_worker_shutdown(psp_me_sound_worker_t *worker,
	uint64_t timeout_us)
{
	psp_me_sound_worker_message_t command;
	bool acknowledged;

	if (!worker || !worker->running)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_SOUND_WORKER_COMMAND_SHUTDOWN;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	acknowledged = send_command(worker, &command, timeout_us) &&
		wait_event(worker, PSP_ME_SOUND_WORKER_EVENT_SHUTDOWN_ACK,
			worker->generation, command.token, timeout_us, NULL);
	if (!acknowledged)
	{
		psp_me_sound_worker_abort(worker);
		return false;
	}

	worker->dispatch.wait(worker->dispatch.opaque);
	snapshot_stats(worker);
	if (worker->shadow_expected_count != 0)
	{
		worker->shadow_mismatches += worker->shadow_expected_count;
		worker->shadow_expected_count = 0;
		snapshot_stats(worker);
	}
	worker->running = false;
	free_shared_state(worker);
	return worker->last_stats.fatal_error == PSP_ME_SOUND_WORKER_ERROR_NONE;
}

void psp_me_sound_worker_abort(psp_me_sound_worker_t *worker)
{
	psp_me_sound_worker_main_control_t *control;

	if (!worker || !worker->running)
		return;
	control = (psp_me_sound_worker_main_control_t *)worker->main_control;
	control->abort_requested = 1;
	sceKernelDcacheWritebackInvalidateRange(control, sizeof(*control));
	worker->dispatch.wait(worker->dispatch.opaque);
	snapshot_stats(worker);
	if (worker->shadow_expected_count != 0)
	{
		worker->shadow_mismatches += worker->shadow_expected_count;
		worker->shadow_expected_count = 0;
		snapshot_stats(worker);
	}
	worker->running = false;
	free_shared_state(worker);
}

void psp_me_sound_worker_get_stats(psp_me_sound_worker_t *worker,
	psp_me_sound_worker_stats_t *stats)
{
	if (!stats)
		return;
	if (!worker)
	{
		memset(stats, 0, sizeof(*stats));
		return;
	}
	if (worker->running)
		snapshot_stats(worker);
	*stats = worker->last_stats;
}
