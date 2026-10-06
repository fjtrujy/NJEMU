#include <malloc.h>
#include <string.h>
#include <pspkernel.h>
#include <me-core-mapper/me-core-mapper.h>
#include "psp/psp_me_qsound_worker.h"

#define PSP_ME_QSOUND_CACHE_LINE 64u
#define PSP_ME_QSOUND_CACHE_SIZE(size) \
	(((uint32_t)(size) + PSP_ME_QSOUND_CACHE_LINE - 1u) & ~(PSP_ME_QSOUND_CACHE_LINE - 1u))
#define PSP_ME_QSOUND_HEARTBEAT_SPINS 4096u
#define PSP_ME_QSOUND_SHARED_RAM_OFFSET 0xc000u
#define PSP_ME_QSOUND_SHARED_RAM_SIZE 0x1000u

typedef enum psp_me_qsound_command_type
{
	PSP_ME_QSOUND_COMMAND_RESET = 1,
	PSP_ME_QSOUND_COMMAND_SNAPSHOT,
	PSP_ME_QSOUND_COMMAND_ADVANCE,
	PSP_ME_QSOUND_COMMAND_SYNC,
	PSP_ME_QSOUND_COMMAND_IRQ,
	PSP_ME_QSOUND_COMMAND_Z80_RESET,
	PSP_ME_QSOUND_COMMAND_MEMORY_READ,
	PSP_ME_QSOUND_COMMAND_MEMORY_WRITE_BYTE,
	PSP_ME_QSOUND_COMMAND_RENDER,
	PSP_ME_QSOUND_COMMAND_RECOVERY,
	PSP_ME_QSOUND_COMMAND_SHUTDOWN
} psp_me_qsound_command_type_t;

typedef enum psp_me_qsound_event_type
{
	PSP_ME_QSOUND_EVENT_READY = 1,
	PSP_ME_QSOUND_EVENT_RESET_ACK,
	PSP_ME_QSOUND_EVENT_SNAPSHOT_ACK,
	PSP_ME_QSOUND_EVENT_SYNC_ACK,
	PSP_ME_QSOUND_EVENT_MEMORY_READ_ACK,
	PSP_ME_QSOUND_EVENT_RENDER_ACK,
	PSP_ME_QSOUND_EVENT_RECOVERY_ACK,
	PSP_ME_QSOUND_EVENT_SHUTDOWN_ACK,
	PSP_ME_QSOUND_EVENT_ERROR
} psp_me_qsound_event_type_t;

typedef enum psp_me_qsound_error
{
	PSP_ME_QSOUND_ERROR_NONE = 0,
	PSP_ME_QSOUND_ERROR_RING,
	PSP_ME_QSOUND_ERROR_GENERATION,
	PSP_ME_QSOUND_ERROR_PROTOCOL,
	PSP_ME_QSOUND_ERROR_TIME_REGRESSION,
	PSP_ME_QSOUND_ERROR_ALLOCATION,
	PSP_ME_QSOUND_ERROR_DISPATCH,
	PSP_ME_QSOUND_ERROR_TIMEOUT,
	PSP_ME_QSOUND_ERROR_ABORTED
} psp_me_qsound_error_t;

typedef struct __attribute__((aligned(PSP_ME_QSOUND_CACHE_LINE)))
	psp_me_qsound_control
{
	uint32_t abort_requested;
	uint32_t reserved[15];
} psp_me_qsound_control_t;

typedef struct __attribute__((aligned(PSP_ME_QSOUND_CACHE_LINE)))
	psp_me_qsound_progress
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
	uint32_t running;
	uint32_t last_command_type;
	uint64_t emulated_time;
	uint64_t fatal_emulated_time;
} psp_me_qsound_progress_t;

typedef struct psp_me_qsound_message
{
	uint32_t type;
	uint32_t generation;
	uint32_t token;
	uint32_t value;
	uint32_t reserved;
	uint32_t flags;
	uint64_t emulated_time;
} psp_me_qsound_message_t;

typedef struct __attribute__((aligned(PSP_ME_QSOUND_CACHE_LINE)))
	psp_me_qsound_render_job
{
	uint32_t generation;
	uint32_t token;
	uint32_t samples;
	uint32_t error;
	uint32_t reserved[12];
	int32_t left[PSP_ME_QSOUND_RENDER_MAX_SAMPLES];
	int32_t right[PSP_ME_QSOUND_RENDER_MAX_SAMPLES];
} psp_me_qsound_render_job_t;

typedef struct __attribute__((aligned(PSP_ME_QSOUND_CACHE_LINE)))
	psp_me_qsound_shared_context
{
	psp_me_spsc_ring_t *commands;
	psp_me_spsc_ring_t *events;
	psp_me_qsound_control_t *control;
	psp_me_qsound_progress_t *progress;
	psp_me_qsound_snapshot_t *snapshot;
	psp_me_qsound_recovery_snapshot_t *recovery;
	uint8_t *z80_memory;
	qsound_context_t *qsound_context;
	psp_me_qsound_render_job_t *render_job;
	uint32_t reserved[14];
} psp_me_qsound_shared_context_t;

typedef struct psp_me_qsound_runtime
{
	cz80_struc cpu;
	uint8_t *memory;
	const uint8_t *source_rom;
	uint32_t source_length;
	uint32_t bank;
	uint8_t suspended;
	uint8_t initialized;
	qsound_context_t *qsound;
	psp_me_qsound_progress_t *progress;
} psp_me_qsound_runtime_t;

_Static_assert(sizeof(psp_me_qsound_control_t) == PSP_ME_QSOUND_CACHE_LINE,
	"QSound worker control must occupy one cache line");
_Static_assert(sizeof(psp_me_qsound_message_t) == 32,
	"QSound worker messages must remain compact");

static psp_me_qsound_runtime_t *me_runtime;

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
	allegrex_publish, allegrex_acquire, NULL
};
static const psp_me_spsc_ring_cache_ops_t me_cache_ops = {
	me_publish, me_acquire, NULL
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

static void me_set_bank(psp_me_qsound_runtime_t *runtime, uint8_t data)
{
	uint32_t bank = 0x10000u + ((uint32_t)(data & 0x0fu) << 14);

	if (bank >= runtime->source_length || runtime->source_length - bank < 0x4000u)
		bank = 0x10000u;
	if (bank >= runtime->source_length || runtime->source_length - bank < 0x4000u)
		return;
	if (bank == runtime->bank)
		return;
	runtime->bank = bank;
	me_copy(runtime->memory + 0x8000u, runtime->source_rom + bank, 0x4000u);
	if (runtime->progress)
		runtime->progress->bank_switches++;
}

static uint8_t me_z80_read(uint32_t address)
{
	return me_runtime ? me_runtime->memory[address & 0xffffu] : 0xffu;
}

static void me_z80_write(uint32_t address, uint8_t data)
{
	psp_me_qsound_runtime_t *runtime = me_runtime;
	uint32_t offset = address & 0xffffu;

	if (!runtime)
		return;
	switch (offset & 0xf000u)
	{
	case 0xc000u:
	case 0xf000u:
		runtime->memory[offset] = data;
		break;
	case 0xd000u:
		switch (offset)
		{
		case 0xd000u: qsound_context_data_h_w(runtime->qsound, data); break;
		case 0xd001u: qsound_context_data_l_w(runtime->qsound, data); break;
		case 0xd002u: qsound_context_cmd_w(runtime->qsound, data); break;
		case 0xd003u: me_set_bank(runtime, data); break;
		default: break;
		}
		break;
	default:
		break;
	}
}

static void me_apply_snapshot(psp_me_qsound_shared_context_t *context,
	psp_me_qsound_runtime_t *runtime)
{
	psp_me_qsound_snapshot_t *snapshot = context->snapshot;

	meCoreDcacheInvalidateRange(snapshot,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(*snapshot)));
	meCoreDcacheInvalidateRange(context->z80_memory,
		PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE);
	meCoreDcacheInvalidateRange(context->qsound_context,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(*context->qsound_context)));
	if (snapshot->source_rom && snapshot->source_length)
		meCoreDcacheInvalidateRange((void *)snapshot->source_rom, snapshot->source_length);
	if (context->qsound_context->sample_rom && snapshot->sample_length)
		meCoreDcacheInvalidateRange((void *)context->qsound_context->sample_rom,
			snapshot->sample_length);

	me_zero(runtime, sizeof(*runtime));
	runtime->memory = context->z80_memory;
	runtime->source_rom = snapshot->source_rom;
	runtime->source_length = snapshot->source_length;
	runtime->bank = snapshot->bank;
	runtime->suspended = snapshot->suspended;
	runtime->qsound = context->qsound_context;
	runtime->progress = context->progress;
	me_runtime = runtime;
	Cz80_Init_Instance(&runtime->cpu);
	Cz80_Set_Fetch(&runtime->cpu, 0x0000u, 0xffffu, (uintptr_t)runtime->memory);
	Cz80_Set_ReadBase(&runtime->cpu, (uintptr_t)runtime->memory);
	Cz80_Set_ReadB(&runtime->cpu, me_z80_read);
	Cz80_Set_WriteB(&runtime->cpu, me_z80_write);
	Cz80_Set_State(&runtime->cpu, &snapshot->state);
	runtime->initialized = 1;
}

static bool me_publish_recovery(psp_me_qsound_shared_context_t *context,
	psp_me_qsound_runtime_t *runtime)
{
	psp_me_qsound_recovery_snapshot_t *recovery = context->recovery;

	if (!runtime || !runtime->initialized)
		return false;
	me_zero(recovery, sizeof(*recovery));
	Cz80_Get_State(&runtime->cpu, &recovery->state);
	recovery->generation = context->progress->generation;
	recovery->emulated_time = context->progress->emulated_time;
	recovery->bank = runtime->bank;
	recovery->suspended = runtime->suspended;
	recovery->initialized = 1;
	meCoreDcacheWritebackRange(context->z80_memory,
		PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE);
	meCoreDcacheWritebackRange(context->qsound_context,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(*context->qsound_context)));
	meCoreDcacheWritebackRange(recovery,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(*recovery)));
	return true;
}

static bool me_abort_requested(psp_me_qsound_control_t *control)
{
	meCoreDcacheInvalidateRange(control, sizeof(*control));
	return control->abort_requested != 0;
}

static bool me_send_event(psp_me_qsound_shared_context_t *context,
	const psp_me_qsound_message_t *event)
{
	uint32_t spins = 0;
	for (;;)
	{
		psp_me_spsc_ring_result_t result = psp_me_spsc_ring_try_push(
			context->events, &me_cache_ops, event, NULL);
		if (result == PSP_ME_SPSC_RING_OK)
			return true;
		if (result != PSP_ME_SPSC_RING_FULL)
			return false;
		if ((++spins & (PSP_ME_QSOUND_HEARTBEAT_SPINS - 1u)) == 0 &&
			me_abort_requested(context->control))
			return false;
	}
}

static void me_publish_progress(psp_me_qsound_progress_t *progress)
{
	meCoreDcacheWritebackRange(progress, sizeof(*progress));
}

static void me_fail(psp_me_qsound_shared_context_t *context,
	psp_me_qsound_runtime_t *runtime, const psp_me_qsound_message_t *command,
	uint32_t error)
{
	psp_me_qsound_message_t event;

	if (runtime && runtime->initialized)
		(void)me_publish_recovery(context, runtime);
	context->progress->fatal_error = error;
	context->progress->running = 0;
	context->progress->fatal_emulated_time = context->progress->emulated_time;
	me_publish_progress(context->progress);
	me_zero(&event, sizeof(event));
	event.type = PSP_ME_QSOUND_EVENT_ERROR;
	event.generation = context->progress->generation;
	if (command)
		event.token = command->token;
	event.value = error;
	event.emulated_time = context->progress->emulated_time;
	(void)me_send_event(context, &event);
}

static void psp_me_qsound_worker_entry(void *param)
{
	psp_me_qsound_shared_context_t *context =
		(psp_me_qsound_shared_context_t *)param;
	psp_me_qsound_runtime_t runtime;
	psp_me_qsound_message_t event;
	uint32_t idle_spins = 0;

	me_runtime = NULL;
	meCoreDcacheInvalidateRange(context, sizeof(*context));
	psp_me_spsc_ring_acquire_initial(context->commands, &me_cache_ops);
	psp_me_spsc_ring_acquire_initial(context->events, &me_cache_ops);
	meCoreDcacheInvalidateRange(context->control, sizeof(*context->control));
	meCoreDcacheInvalidateRange(context->progress, sizeof(*context->progress));
	if (!psp_me_spsc_ring_is_valid(context->commands) ||
		!psp_me_spsc_ring_is_valid(context->events))
	{
		me_fail(context, NULL, NULL, PSP_ME_QSOUND_ERROR_RING);
		return;
	}

	me_zero(&runtime, sizeof(runtime));
	me_zero(context->progress, sizeof(*context->progress));
	context->progress->running = 1;
	me_publish_progress(context->progress);
	me_zero(&event, sizeof(event));
	event.type = PSP_ME_QSOUND_EVENT_READY;
	if (!me_send_event(context, &event))
		return;

	for (;;)
	{
		psp_me_qsound_message_t command;
		bool send_response = true;
		psp_me_spsc_ring_result_t result = psp_me_spsc_ring_try_pop(
			context->commands, &me_cache_ops, &command, NULL);

		if (result == PSP_ME_SPSC_RING_EMPTY)
		{
			if ((++idle_spins & (PSP_ME_QSOUND_HEARTBEAT_SPINS - 1u)) == 0 &&
				me_abort_requested(context->control))
			{
				context->progress->fatal_error = PSP_ME_QSOUND_ERROR_ABORTED;
				context->progress->running = 0;
				me_publish_progress(context->progress);
				return;
			}
			continue;
		}
		if (result != PSP_ME_SPSC_RING_OK)
		{
			me_fail(context, &runtime, NULL, PSP_ME_QSOUND_ERROR_RING);
			return;
		}
		idle_spins = 0;
		context->progress->commands_processed++;
		context->progress->last_command_type = command.type;
		me_zero(&event, sizeof(event));
		event.generation = command.generation;
		event.token = command.token;

		if (command.type != PSP_ME_QSOUND_COMMAND_RESET &&
			command.generation != context->progress->generation)
		{
			me_fail(context, &runtime, &command, PSP_ME_QSOUND_ERROR_GENERATION);
			return;
		}
		if (command.type != PSP_ME_QSOUND_COMMAND_RESET &&
			command.emulated_time < context->progress->emulated_time)
		{
			context->progress->fatal_emulated_time = command.emulated_time;
			me_fail(context, &runtime, &command, PSP_ME_QSOUND_ERROR_TIME_REGRESSION);
			return;
		}
		if (command.type != PSP_ME_QSOUND_COMMAND_RESET)
			context->progress->emulated_time = command.emulated_time;

		switch (command.type)
		{
		case PSP_ME_QSOUND_COMMAND_RESET:
			if (!command.generation)
			{
				me_fail(context, &runtime, &command, PSP_ME_QSOUND_ERROR_GENERATION);
				return;
			}
			context->progress->generation = command.generation;
			context->progress->emulated_time = 0;
			context->progress->resets++;
			me_zero(&runtime, sizeof(runtime));
			me_runtime = &runtime;
			event.type = PSP_ME_QSOUND_EVENT_RESET_ACK;
			break;
		case PSP_ME_QSOUND_COMMAND_SNAPSHOT:
			if (!context->snapshot->source_rom ||
				context->snapshot->source_length < PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE ||
				context->snapshot->generation != command.generation)
			{
				me_fail(context, &runtime, &command, PSP_ME_QSOUND_ERROR_PROTOCOL);
				return;
			}
			me_apply_snapshot(context, &runtime);
			context->progress->snapshots++;
			event.type = PSP_ME_QSOUND_EVENT_SNAPSHOT_ACK;
			break;
			case PSP_ME_QSOUND_COMMAND_ADVANCE:
			if (!runtime.initialized)
			{
				me_fail(context, &runtime, &command, PSP_ME_QSOUND_ERROR_PROTOCOL);
				return;
			}
				meCoreDcacheInvalidateRange(
					runtime.memory + PSP_ME_QSOUND_SHARED_RAM_OFFSET,
					PSP_ME_QSOUND_SHARED_RAM_SIZE);
				if (!runtime.suspended && command.value)
					(void)Cz80_Exec(&runtime.cpu, (int32_t)command.value);
			context->progress->advances++;
				send_response = false;
				break;
			case PSP_ME_QSOUND_COMMAND_SYNC:
				if (!runtime.initialized)
				{
					me_fail(context, &runtime, &command, PSP_ME_QSOUND_ERROR_PROTOCOL);
					return;
				}
				meCoreDcacheWritebackRange(
					runtime.memory + PSP_ME_QSOUND_SHARED_RAM_OFFSET,
					PSP_ME_QSOUND_SHARED_RAM_SIZE);
				event.type = PSP_ME_QSOUND_EVENT_SYNC_ACK;
				break;
			case PSP_ME_QSOUND_COMMAND_IRQ:
			if (!runtime.initialized)
			{
				me_fail(context, &runtime, &command, PSP_ME_QSOUND_ERROR_PROTOCOL);
				return;
			}
			Cz80_Set_IRQ(&runtime.cpu, 0, (int32_t)command.value);
			context->progress->irqs++;
			send_response = false;
			break;
		case PSP_ME_QSOUND_COMMAND_Z80_RESET:
			if (!runtime.initialized)
			{
				me_fail(context, &runtime, &command, PSP_ME_QSOUND_ERROR_PROTOCOL);
				return;
			}
			if (command.value)
			{
				runtime.suspended = 1;
				Cz80_Reset(&runtime.cpu);
			}
			else
				runtime.suspended = 0;
			context->progress->z80_resets++;
			send_response = false;
			break;
		case PSP_ME_QSOUND_COMMAND_MEMORY_READ:
			if (!runtime.initialized || command.reserved == 0 ||
				command.reserved > sizeof(event.value) ||
				command.value >= PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE ||
				command.reserved > PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE - command.value)
			{
				me_fail(context, &runtime, &command, PSP_ME_QSOUND_ERROR_PROTOCOL);
				return;
			}
			{
				uint32_t i;
				for (i = 0; i < command.reserved; i++)
					event.value |= (uint32_t)runtime.memory[command.value + i] << (i * 8u);
			}
			context->progress->memory_reads++;
			event.type = PSP_ME_QSOUND_EVENT_MEMORY_READ_ACK;
			break;
		case PSP_ME_QSOUND_COMMAND_MEMORY_WRITE_BYTE:
			if (!runtime.initialized || command.value >= PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE)
			{
				me_fail(context, &runtime, &command, PSP_ME_QSOUND_ERROR_PROTOCOL);
				return;
			}
			runtime.memory[command.value] = (uint8_t)command.reserved;
			context->progress->memory_writes++;
			send_response = false;
			break;
		case PSP_ME_QSOUND_COMMAND_RENDER:
			if (!runtime.initialized || command.value == 0 ||
				command.value > PSP_ME_QSOUND_RENDER_MAX_SAMPLES)
			{
				me_fail(context, &runtime, &command, PSP_ME_QSOUND_ERROR_PROTOCOL);
				return;
			}
			me_zero(context->render_job->left,
				command.value * sizeof(context->render_job->left[0]));
			me_zero(context->render_job->right,
				command.value * sizeof(context->render_job->right[0]));
			{
				int32_t *buffers[2] = {
					context->render_job->left, context->render_job->right
				};
				qsound_context_update(runtime.qsound, buffers, (int)command.value);
			}
			context->render_job->generation = command.generation;
			context->render_job->token = command.token;
			context->render_job->samples = command.value;
			context->render_job->error = 0;
			meCoreDcacheWritebackRange(context->render_job,
				PSP_ME_QSOUND_CACHE_SIZE(sizeof(*context->render_job)));
			context->progress->renders++;
			context->progress->render_samples += command.value;
			event.type = PSP_ME_QSOUND_EVENT_RENDER_ACK;
			break;
		case PSP_ME_QSOUND_COMMAND_RECOVERY:
			if (!me_publish_recovery(context, &runtime))
			{
				me_fail(context, &runtime, &command, PSP_ME_QSOUND_ERROR_PROTOCOL);
				return;
			}
			context->progress->recoveries++;
			event.type = PSP_ME_QSOUND_EVENT_RECOVERY_ACK;
			break;
		case PSP_ME_QSOUND_COMMAND_SHUTDOWN:
			if (runtime.initialized)
				(void)me_publish_recovery(context, &runtime);
			context->progress->shutdowns++;
			context->progress->running = 0;
			event.type = PSP_ME_QSOUND_EVENT_SHUTDOWN_ACK;
			me_publish_progress(context->progress);
			(void)me_send_event(context, &event);
			return;
		default:
			me_fail(context, &runtime, &command, PSP_ME_QSOUND_ERROR_PROTOCOL);
			return;
		}

		me_publish_progress(context->progress);
		if (send_response)
		{
			event.emulated_time = context->progress->emulated_time;
			if (!me_send_event(context, &event))
			{
				me_fail(context, &runtime, &command, PSP_ME_QSOUND_ERROR_RING);
				return;
			}
		}
	}
}

static bool timed_out(uint64_t start, uint64_t timeout_us)
{
	return sceKernelGetSystemTimeWide() - start >= timeout_us;
}

static bool send_command(psp_me_qsound_worker_t *worker,
	const psp_me_qsound_message_t *command, uint64_t timeout_us)
{
	uint64_t start = sceKernelGetSystemTimeWide();
	for (;;)
	{
		psp_me_spsc_ring_result_t result = psp_me_spsc_ring_try_push(
			worker->commands, &allegrex_cache_ops, command, NULL);
		if (result == PSP_ME_SPSC_RING_OK)
			return true;
		if (result != PSP_ME_SPSC_RING_FULL || timed_out(start, timeout_us))
			return false;
	}
}

static bool send_command_nonblocking(psp_me_qsound_worker_t *worker,
	const psp_me_qsound_message_t *command)
{
	return psp_me_spsc_ring_try_push(worker->commands, &allegrex_cache_ops,
		command, NULL) == PSP_ME_SPSC_RING_OK;
}

static bool wait_event(psp_me_qsound_worker_t *worker, uint32_t type,
	uint32_t generation, uint32_t token, uint64_t timeout_us,
	psp_me_qsound_message_t *event_out)
{
	uint64_t start = sceKernelGetSystemTimeWide();
	for (;;)
	{
		psp_me_qsound_message_t event;
		psp_me_spsc_ring_result_t result = psp_me_spsc_ring_try_pop(
			worker->events, &allegrex_cache_ops, &event, NULL);
		if (result == PSP_ME_SPSC_RING_OK)
		{
			if (event.type == PSP_ME_QSOUND_EVENT_ERROR)
				return false;
			if (event.type == PSP_ME_QSOUND_EVENT_RENDER_ACK &&
				worker->render_in_flight && event.generation == worker->generation &&
				event.token == worker->render_token)
			{
				worker->render_ack_received = true;
				if (type == PSP_ME_QSOUND_EVENT_RENDER_ACK &&
					generation == event.generation && token == event.token)
				{
					if (event_out)
						*event_out = event;
					return true;
				}
				continue;
			}
			if (event.type != type || event.generation != generation || event.token != token)
				return false;
			if (event_out)
				*event_out = event;
			return true;
		}
		if (result != PSP_ME_SPSC_RING_EMPTY || timed_out(start, timeout_us))
			return false;
	}
}

static void free_shared_state(psp_me_qsound_worker_t *worker)
{
	free(worker->render_job);
	free(worker->qsound_context);
	free(worker->z80_memory);
	free(worker->recovery);
	free(worker->snapshot);
	free(worker->progress);
	free(worker->main_control);
	free(worker->shared_context);
	free(worker->events);
	free(worker->commands);
	worker->render_job = NULL;
	worker->qsound_context = NULL;
	worker->z80_memory = NULL;
	worker->recovery = NULL;
	worker->snapshot = NULL;
	worker->progress = NULL;
	worker->main_control = NULL;
	worker->shared_context = NULL;
	worker->events = NULL;
	worker->commands = NULL;
	worker->running = false;
}

bool psp_me_qsound_worker_start(psp_me_qsound_worker_t *worker,
	const psp_me_qsound_worker_dispatch_t *dispatch, uint32_t capacity,
	uint64_t timeout_us)
{
	psp_me_qsound_shared_context_t *context;

	if (!worker || !dispatch || !dispatch->start || !dispatch->wait ||
		capacity == 0 || (capacity & (capacity - 1u)) != 0 || worker->running)
		return false;
	memset(worker, 0, sizeof(*worker));
	worker->dispatch = *dispatch;
	worker->capacity = capacity;
	worker->ring_size = psp_me_spsc_ring_storage_size(capacity,
		sizeof(psp_me_qsound_message_t));
	if (!worker->ring_size)
		return false;
	worker->commands = memalign(PSP_ME_QSOUND_CACHE_LINE, worker->ring_size);
	worker->events = memalign(PSP_ME_QSOUND_CACHE_LINE, worker->ring_size);
	worker->shared_context = memalign(PSP_ME_QSOUND_CACHE_LINE,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(psp_me_qsound_shared_context_t)));
	worker->main_control = memalign(PSP_ME_QSOUND_CACHE_LINE,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(psp_me_qsound_control_t)));
	worker->progress = memalign(PSP_ME_QSOUND_CACHE_LINE,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(psp_me_qsound_progress_t)));
	worker->snapshot = memalign(PSP_ME_QSOUND_CACHE_LINE,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(*worker->snapshot)));
	worker->recovery = memalign(PSP_ME_QSOUND_CACHE_LINE,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(*worker->recovery)));
	worker->z80_memory = memalign(PSP_ME_QSOUND_CACHE_LINE,
		PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE);
	worker->qsound_context = memalign(PSP_ME_QSOUND_CACHE_LINE,
		PSP_ME_QSOUND_CACHE_SIZE(qsound_context_size()));
	worker->render_job = memalign(PSP_ME_QSOUND_CACHE_LINE,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(psp_me_qsound_render_job_t)));
	if (!worker->commands || !worker->events || !worker->shared_context ||
		!worker->main_control || !worker->progress || !worker->snapshot ||
		!worker->recovery || !worker->z80_memory || !worker->qsound_context ||
		!worker->render_job)
	{
		worker->last_stats.fatal_error = PSP_ME_QSOUND_ERROR_ALLOCATION;
		free_shared_state(worker);
		return false;
	}
	if (!psp_me_spsc_ring_init(worker->commands, worker->ring_size, capacity,
			sizeof(psp_me_qsound_message_t)) ||
		!psp_me_spsc_ring_init(worker->events, worker->ring_size, capacity,
			sizeof(psp_me_qsound_message_t)))
	{
		worker->last_stats.fatal_error = PSP_ME_QSOUND_ERROR_RING;
		free_shared_state(worker);
		return false;
	}
	memset(worker->main_control, 0, PSP_ME_QSOUND_CACHE_SIZE(sizeof(psp_me_qsound_control_t)));
	memset(worker->progress, 0, PSP_ME_QSOUND_CACHE_SIZE(sizeof(psp_me_qsound_progress_t)));
	memset(worker->snapshot, 0, PSP_ME_QSOUND_CACHE_SIZE(sizeof(*worker->snapshot)));
	memset(worker->recovery, 0, PSP_ME_QSOUND_CACHE_SIZE(sizeof(*worker->recovery)));
	memset(worker->z80_memory, 0, PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE);
	memset(worker->qsound_context, 0, PSP_ME_QSOUND_CACHE_SIZE(qsound_context_size()));
	memset(worker->render_job, 0,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(psp_me_qsound_render_job_t)));
	memset(worker->shared_context, 0,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(psp_me_qsound_shared_context_t)));
	context = (psp_me_qsound_shared_context_t *)worker->shared_context;
	context->commands = worker->commands;
	context->events = worker->events;
	context->control = (psp_me_qsound_control_t *)worker->main_control;
	context->progress = (psp_me_qsound_progress_t *)worker->progress;
	context->snapshot = worker->snapshot;
	context->recovery = worker->recovery;
	context->z80_memory = worker->z80_memory;
	context->qsound_context = worker->qsound_context;
	context->render_job = (psp_me_qsound_render_job_t *)worker->render_job;
	psp_me_spsc_ring_publish_initial(worker->commands, &allegrex_cache_ops);
	psp_me_spsc_ring_publish_initial(worker->events, &allegrex_cache_ops);
	sceKernelDcacheWritebackInvalidateRange(worker->main_control,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(psp_me_qsound_control_t)));
	sceKernelDcacheWritebackInvalidateRange(worker->progress,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(psp_me_qsound_progress_t)));
	sceKernelDcacheWritebackInvalidateRange(worker->shared_context,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(psp_me_qsound_shared_context_t)));
	if (!worker->dispatch.start(psp_me_qsound_worker_entry, context,
			sizeof(*context), worker->dispatch.opaque))
	{
		worker->last_stats.fatal_error = PSP_ME_QSOUND_ERROR_DISPATCH;
		free_shared_state(worker);
		return false;
	}
	worker->running = true;
	worker->next_token = 1;
	if (!wait_event(worker, PSP_ME_QSOUND_EVENT_READY, 0, 0, timeout_us, NULL))
	{
		psp_me_qsound_worker_abort(worker);
		worker->last_stats.fatal_error = PSP_ME_QSOUND_ERROR_TIMEOUT;
		return false;
	}
	return true;
}

bool psp_me_qsound_worker_reset(psp_me_qsound_worker_t *worker,
	uint32_t generation, uint64_t timeout_us)
{
	psp_me_qsound_message_t command;
	if (!worker || !worker->running || !generation)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_QSOUND_COMMAND_RESET;
	command.generation = generation;
	command.token = worker->next_token++;
	if (!send_command(worker, &command, timeout_us) ||
		!wait_event(worker, PSP_ME_QSOUND_EVENT_RESET_ACK, generation,
			command.token, timeout_us, NULL))
		return false;
	worker->generation = generation;
	worker->emulated_time = 0;
	worker->render_ack_received = false;
	worker->render_in_flight = false;
	worker->render_token = 0;
	return true;
}

bool psp_me_qsound_worker_snapshot(psp_me_qsound_worker_t *worker,
	const cz80_state_t *state, const uint8_t *memory, const uint8_t *source_rom,
	uint32_t source_length, uint32_t sample_length, uint32_t bank, bool suspended,
	uint64_t timeout_us)
{
	psp_me_qsound_message_t command;
	if (!worker || !worker->running || !worker->generation || !state || !memory ||
		!source_rom || source_length < PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE ||
		sample_length == 0 ||
		!qsound_default_clone_for_worker(worker->qsound_context))
		return false;
	memcpy(worker->z80_memory, memory, PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE);
	memset(worker->snapshot, 0, sizeof(*worker->snapshot));
	worker->snapshot->state = *state;
	worker->snapshot->source_rom = source_rom;
	worker->snapshot->source_length = source_length;
	worker->snapshot->sample_length = sample_length;
	worker->snapshot->bank = bank;
	worker->snapshot->generation = worker->generation;
	worker->snapshot->suspended = suspended ? 1u : 0u;
	sceKernelDcacheWritebackAll();
	sceKernelDcacheWritebackInvalidateRange(worker->snapshot,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(*worker->snapshot)));
	sceKernelDcacheWritebackInvalidateRange(worker->z80_memory,
		PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE);
	sceKernelDcacheWritebackInvalidateRange(worker->qsound_context,
		PSP_ME_QSOUND_CACHE_SIZE(qsound_context_size()));
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_QSOUND_COMMAND_SNAPSHOT;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	command.emulated_time = worker->emulated_time;
	if (!send_command(worker, &command, timeout_us) ||
		!wait_event(worker, PSP_ME_QSOUND_EVENT_SNAPSHOT_ACK, worker->generation,
			command.token, timeout_us, NULL))
		return false;
	return true;
}

bool psp_me_qsound_worker_advance(psp_me_qsound_worker_t *worker,
	uint32_t cycles, uint64_t emulated_time)
{
	psp_me_qsound_message_t command;
	if (!worker || !worker->running || !worker->generation)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_QSOUND_COMMAND_ADVANCE;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	command.value = cycles;
	command.emulated_time = emulated_time;
	if (!send_command_nonblocking(worker, &command))
		return false;
	worker->emulated_time = emulated_time;
	return true;
}

bool psp_me_qsound_worker_sync(psp_me_qsound_worker_t *worker,
	uint64_t timeout_us)
{
	psp_me_qsound_message_t command;

	if (!worker || !worker->running || !worker->generation)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_QSOUND_COMMAND_SYNC;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	command.emulated_time = worker->emulated_time;
	return send_command(worker, &command, timeout_us) &&
		wait_event(worker, PSP_ME_QSOUND_EVENT_SYNC_ACK, worker->generation,
			command.token, timeout_us, NULL);
}

bool psp_me_qsound_worker_shared_ram_read(psp_me_qsound_worker_t *worker,
	uint32_t offset, uint8_t *data, uint32_t size)
{
	if (!worker || !worker->running || !worker->z80_memory || !data || size == 0 ||
		offset < PSP_ME_QSOUND_SHARED_RAM_OFFSET ||
		offset >= PSP_ME_QSOUND_SHARED_RAM_OFFSET + PSP_ME_QSOUND_SHARED_RAM_SIZE ||
		size > PSP_ME_QSOUND_SHARED_RAM_OFFSET + PSP_ME_QSOUND_SHARED_RAM_SIZE - offset)
		return false;
	sceKernelDcacheInvalidateRange(worker->z80_memory + offset, size);
	memcpy(data, worker->z80_memory + offset, size);
	return true;
}

bool psp_me_qsound_worker_shared_ram_write(psp_me_qsound_worker_t *worker,
	uint32_t offset, const uint8_t *data, uint32_t size)
{
	if (!worker || !worker->running || !worker->z80_memory || !data || size == 0 ||
		offset < PSP_ME_QSOUND_SHARED_RAM_OFFSET ||
		offset >= PSP_ME_QSOUND_SHARED_RAM_OFFSET + PSP_ME_QSOUND_SHARED_RAM_SIZE ||
		size > PSP_ME_QSOUND_SHARED_RAM_OFFSET + PSP_ME_QSOUND_SHARED_RAM_SIZE - offset)
		return false;
	memcpy(worker->z80_memory + offset, data, size);
	sceKernelDcacheWritebackInvalidateRange(worker->z80_memory + offset, size);
	return true;
}

bool psp_me_qsound_worker_irq(psp_me_qsound_worker_t *worker,
	int32_t state, uint64_t emulated_time)
{
	psp_me_qsound_message_t command;
	if (!worker || !worker->running || !worker->generation)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_QSOUND_COMMAND_IRQ;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	command.value = (uint32_t)state;
	command.emulated_time = emulated_time;
	if (!send_command_nonblocking(worker, &command))
		return false;
	worker->emulated_time = emulated_time;
	return true;
}

bool psp_me_qsound_worker_z80_reset(psp_me_qsound_worker_t *worker,
	bool asserted, uint64_t emulated_time)
{
	psp_me_qsound_message_t command;
	if (!worker || !worker->running || !worker->generation)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_QSOUND_COMMAND_Z80_RESET;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	command.value = asserted ? 1u : 0u;
	command.emulated_time = emulated_time;
	if (!send_command_nonblocking(worker, &command))
		return false;
	worker->emulated_time = emulated_time;
	return true;
}

bool psp_me_qsound_worker_memory_read(psp_me_qsound_worker_t *worker,
	uint32_t offset, uint8_t *data, uint32_t size, uint64_t timeout_us)
{
	psp_me_qsound_message_t command;
	psp_me_qsound_message_t event;
	uint32_t i;
	if (!worker || !worker->running || !worker->generation || !data || size == 0 ||
		size > sizeof(event.value) || offset >= PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE ||
		size > PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE - offset)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_QSOUND_COMMAND_MEMORY_READ;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	command.value = offset;
	command.reserved = size;
	command.emulated_time = worker->emulated_time;
	if (!send_command(worker, &command, timeout_us) ||
		!wait_event(worker, PSP_ME_QSOUND_EVENT_MEMORY_READ_ACK, worker->generation,
			command.token, timeout_us, &event))
		return false;
	for (i = 0; i < size; i++)
		data[i] = (uint8_t)(event.value >> (i * 8u));
	return true;
}

bool psp_me_qsound_worker_memory_write_byte(psp_me_qsound_worker_t *worker,
	uint32_t offset, uint8_t data)
{
	psp_me_qsound_message_t command;
	if (!worker || !worker->running || !worker->generation ||
		offset >= PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_QSOUND_COMMAND_MEMORY_WRITE_BYTE;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	command.value = offset;
	command.reserved = data;
	command.emulated_time = worker->emulated_time;
	return send_command_nonblocking(worker, &command);
}

bool psp_me_qsound_worker_render_begin(psp_me_qsound_worker_t *worker,
	uint32_t samples, uint64_t emulated_time)
{
	psp_me_qsound_message_t command;
	if (!worker || !worker->running || !worker->generation || worker->render_in_flight ||
		samples == 0 || samples > PSP_ME_QSOUND_RENDER_MAX_SAMPLES)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_QSOUND_COMMAND_RENDER;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	command.value = samples;
	if (emulated_time < worker->emulated_time)
		emulated_time = worker->emulated_time;
	command.emulated_time = emulated_time;
	if (!send_command_nonblocking(worker, &command))
		return false;
	worker->emulated_time = emulated_time;
	worker->render_token = command.token;
	worker->render_ack_received = false;
	worker->render_in_flight = true;
	return true;
}

static bool render_copy_result(psp_me_qsound_worker_t *worker, int32_t *left,
	int32_t *right, uint32_t samples)
{
	psp_me_qsound_render_job_t *job =
		(psp_me_qsound_render_job_t *)worker->render_job;

	sceKernelDcacheInvalidateRange(job, PSP_ME_QSOUND_CACHE_SIZE(sizeof(*job)));
	if (job->generation != worker->generation || job->token != worker->render_token ||
		job->samples != samples || job->error)
		return false;
	memcpy(left, job->left, samples * sizeof(*left));
	memcpy(right, job->right, samples * sizeof(*right));
	return true;
}

bool psp_me_qsound_worker_render_finish(psp_me_qsound_worker_t *worker,
	int32_t *left, int32_t *right, uint32_t samples, uint64_t timeout_us)
{
	if (!worker || !worker->running || !worker->render_in_flight || !left || !right ||
		samples == 0 || samples > PSP_ME_QSOUND_RENDER_MAX_SAMPLES)
		return false;
	if (!worker->render_ack_received &&
		!wait_event(worker, PSP_ME_QSOUND_EVENT_RENDER_ACK, worker->generation,
			worker->render_token, timeout_us, NULL))
	{
		worker->render_ack_received = false;
		worker->render_in_flight = false;
		worker->render_token = 0;
		return false;
	}
	if (!render_copy_result(worker, left, right, samples))
	{
		worker->render_ack_received = false;
		worker->render_in_flight = false;
		worker->render_token = 0;
		return false;
	}
	worker->render_ack_received = false;
	worker->render_in_flight = false;
	worker->render_token = 0;
	return true;
}

psp_me_qsound_render_result_t psp_me_qsound_worker_render_poll(
	psp_me_qsound_worker_t *worker, int32_t *left, int32_t *right,
	uint32_t samples)
{
	psp_me_qsound_message_t event;
	psp_me_spsc_ring_result_t result;

	if (!worker || !worker->running || !worker->render_in_flight || !left || !right ||
		samples == 0 || samples > PSP_ME_QSOUND_RENDER_MAX_SAMPLES)
		return PSP_ME_QSOUND_RENDER_FAILED;
	if (!worker->render_ack_received)
	{
		result = psp_me_spsc_ring_try_pop(worker->events, &allegrex_cache_ops,
			&event, NULL);
		if (result == PSP_ME_SPSC_RING_EMPTY)
			return PSP_ME_QSOUND_RENDER_PENDING;
		if (result != PSP_ME_SPSC_RING_OK || event.type == PSP_ME_QSOUND_EVENT_ERROR)
			return PSP_ME_QSOUND_RENDER_FAILED;
		if (event.type != PSP_ME_QSOUND_EVENT_RENDER_ACK ||
			event.generation != worker->generation || event.token != worker->render_token)
			return PSP_ME_QSOUND_RENDER_FAILED;
		worker->render_ack_received = true;
	}
	if (!render_copy_result(worker, left, right, samples))
		return PSP_ME_QSOUND_RENDER_FAILED;
	worker->render_ack_received = false;
	worker->render_in_flight = false;
	worker->render_token = 0;
	return PSP_ME_QSOUND_RENDER_COMPLETE;
}

bool psp_me_qsound_worker_recover(psp_me_qsound_worker_t *worker,
	psp_me_qsound_recovery_snapshot_t *snapshot, uint8_t *memory,
	qsound_context_t *qsound, uint64_t timeout_us)
{
	psp_me_qsound_message_t command;
	if (!worker || !worker->running || !worker->generation || !snapshot || !memory || !qsound)
		return false;
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_QSOUND_COMMAND_RECOVERY;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	command.emulated_time = worker->emulated_time;
	if (!send_command(worker, &command, timeout_us) ||
		!wait_event(worker, PSP_ME_QSOUND_EVENT_RECOVERY_ACK, worker->generation,
			command.token, timeout_us, NULL))
		return false;
	if (!psp_me_qsound_worker_read_published_recovery(worker, snapshot, memory, qsound))
		return false;
	return true;
}

bool psp_me_qsound_worker_read_published_recovery(psp_me_qsound_worker_t *worker,
	psp_me_qsound_recovery_snapshot_t *snapshot, uint8_t *memory,
	qsound_context_t *qsound)
{
	if (!worker || !snapshot || !memory || !qsound || !worker->recovery ||
		!worker->z80_memory || !worker->qsound_context)
		return false;
	sceKernelDcacheInvalidateRange(worker->recovery,
		PSP_ME_QSOUND_CACHE_SIZE(sizeof(*worker->recovery)));
	if (!worker->recovery->initialized ||
		(worker->generation && worker->recovery->generation != worker->generation))
		return false;
	sceKernelDcacheInvalidateRange(worker->z80_memory,
		PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE);
	sceKernelDcacheInvalidateRange(worker->qsound_context,
		PSP_ME_QSOUND_CACHE_SIZE(qsound_context_size()));
	*snapshot = *worker->recovery;
	memcpy(memory, worker->z80_memory, PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE);
	*qsound = *worker->qsound_context;
	return true;
}

bool psp_me_qsound_worker_shutdown(psp_me_qsound_worker_t *worker,
	uint64_t timeout_us)
{
	psp_me_qsound_message_t command;
	bool ok = true;
	if (!worker)
		return false;
	if (!worker->running)
	{
		free_shared_state(worker);
		return true;
	}
	memset(&command, 0, sizeof(command));
	command.type = PSP_ME_QSOUND_COMMAND_SHUTDOWN;
	command.generation = worker->generation;
	command.token = worker->next_token++;
	command.emulated_time = worker->emulated_time;
	if (!send_command(worker, &command, timeout_us) ||
		!wait_event(worker, PSP_ME_QSOUND_EVENT_SHUTDOWN_ACK, worker->generation,
			command.token, timeout_us, NULL))
		ok = false;
	worker->dispatch.wait(worker->dispatch.opaque);
	worker->running = false;
	psp_me_qsound_worker_get_stats(worker, &worker->last_stats);
	free_shared_state(worker);
	return ok;
}

void psp_me_qsound_worker_abort(psp_me_qsound_worker_t *worker)
{
	psp_me_qsound_control_t *control;
	if (!worker)
		return;
	if (worker->running && worker->main_control)
	{
		control = (psp_me_qsound_control_t *)worker->main_control;
		control->abort_requested = 1;
		sceKernelDcacheWritebackInvalidateRange(control, sizeof(*control));
		worker->dispatch.wait(worker->dispatch.opaque);
	}
	worker->running = false;
	free_shared_state(worker);
}

void psp_me_qsound_worker_get_stats(psp_me_qsound_worker_t *worker,
	psp_me_qsound_worker_stats_t *stats)
{
	psp_me_qsound_progress_t *progress;
	if (!worker || !stats)
		return;
	*stats = worker->last_stats;
	if (!worker->progress)
		return;
	progress = (psp_me_qsound_progress_t *)worker->progress;
	sceKernelDcacheInvalidateRange(progress, sizeof(*progress));
	stats->generation = progress->generation;
	stats->commands_processed = progress->commands_processed;
	stats->resets = progress->resets;
	stats->snapshots = progress->snapshots;
	stats->advances = progress->advances;
	stats->irqs = progress->irqs;
	stats->z80_resets = progress->z80_resets;
	stats->bank_switches = progress->bank_switches;
	stats->memory_reads = progress->memory_reads;
	stats->memory_writes = progress->memory_writes;
	stats->renders = progress->renders;
	stats->render_samples = progress->render_samples;
	stats->recoveries = progress->recoveries;
	stats->shutdowns = progress->shutdowns;
	stats->fatal_error = progress->fatal_error;
	stats->last_command_type = progress->last_command_type;
	stats->emulated_time = progress->emulated_time;
	stats->fatal_emulated_time = progress->fatal_emulated_time;
	if (worker->commands)
	{
		stats->command_high_water = worker->commands->producer.high_water;
		stats->command_overflow = worker->commands->producer.overflow_count;
	}
	if (worker->events)
	{
		stats->event_high_water = worker->events->producer.high_water;
		stats->event_overflow = worker->events->producer.overflow_count;
	}
}
