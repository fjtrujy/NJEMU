#include <malloc.h>
#include <string.h>
#include <pspkernel.h>
#include <me-core-mapper/me-core-mapper.h>
#include "psp/psp_me_sound_worker.h"

#define PSP_ME_SOUND_WORKER_CACHE_LINE 64u
#define PSP_ME_SOUND_WORKER_HEARTBEAT_SPINS 4096u

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
	uint32_t reserved[3];
} psp_me_sound_worker_progress_t;

typedef struct __attribute__((aligned(PSP_ME_SOUND_WORKER_CACHE_LINE)))
	psp_me_sound_worker_shared_context
{
	psp_me_spsc_ring_t *commands;
	psp_me_spsc_ring_t *events;
	psp_me_sound_worker_main_control_t *main_control;
	psp_me_sound_worker_progress_t *progress;
	uint32_t reserved[12];
} psp_me_sound_worker_shared_context_t;

_Static_assert(sizeof(psp_me_sound_worker_main_control_t) == PSP_ME_SOUND_WORKER_CACHE_LINE,
	"worker main control must occupy exactly one cache line");
_Static_assert(sizeof(psp_me_sound_worker_progress_t) == PSP_ME_SOUND_WORKER_CACHE_LINE,
	"worker progress must occupy exactly one cache line");
_Static_assert(sizeof(psp_me_sound_worker_shared_context_t) == PSP_ME_SOUND_WORKER_CACHE_LINE,
	"worker shared context must occupy exactly one cache line");

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

static void psp_me_sound_worker_entry(void *param)
{
	psp_me_sound_worker_shared_context_t *context =
		(psp_me_sound_worker_shared_context_t *)param;
	psp_me_sound_worker_message_t event;
	uint32_t idle_spins = 0;

	meCoreDcacheInvalidateRange(context, sizeof(*context));
	psp_me_spsc_ring_acquire_initial(context->commands, &me_cache_ops);
	psp_me_spsc_ring_acquire_initial(context->events, &me_cache_ops);
	meCoreDcacheInvalidateRange(context->main_control,
		sizeof(*context->main_control));
	meCoreDcacheInvalidateRange(context->progress, sizeof(*context->progress));

	if (!psp_me_spsc_ring_is_valid(context->commands) ||
		!psp_me_spsc_ring_is_valid(context->events))
	{
		me_fail(context, 0, 0, PSP_ME_SOUND_WORKER_ERROR_RING);
		return;
	}

	me_zero(context->progress, sizeof(*context->progress));
	context->progress->running = 1;
	context->progress->heartbeat = 1;
	me_publish_progress(context->progress);

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
				me_fail(context, context->progress->generation, command.token,
					PSP_ME_SOUND_WORKER_ERROR_TIME_REGRESSION);
				return;
			}
			context->progress->emulated_time = command.emulated_time;
			context->progress->syncs++;
			event.type = PSP_ME_SOUND_WORKER_EVENT_SYNC_ACK;
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
				me_fail(context, context->progress->generation, command.token,
					PSP_ME_SOUND_WORKER_ERROR_TIME_REGRESSION);
				return;
			}
			context->progress->emulated_time = command.emulated_time;
			context->progress->shadow_commands++;
			event.type = PSP_ME_SOUND_WORKER_EVENT_SHADOW_SOUND_ECHO;
			event.emulated_time = command.emulated_time;
			event.value = command.value;
			break;

		default:
			me_fail(context, context->progress->generation, command.token,
				PSP_ME_SOUND_WORKER_ERROR_PROTOCOL);
			return;
		}

		me_publish_progress(context->progress);
		if (!me_send_event(context, &event))
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
	free(worker->shared_context);
	free(worker->main_control);
	free(worker->progress);
	worker->commands = NULL;
	worker->events = NULL;
	worker->shared_context = NULL;
	worker->main_control = NULL;
	worker->progress = NULL;
	worker->ring_size = 0;
	worker->capacity = 0;
}

static void snapshot_stats(psp_me_sound_worker_t *worker)
{
	psp_me_sound_worker_progress_t *progress;

	memset(&worker->last_stats, 0, sizeof(worker->last_stats));
	if (!worker->progress || !worker->commands || !worker->events)
		return;

	progress = (psp_me_sound_worker_progress_t *)worker->progress;
	sceKernelDcacheInvalidateRange(progress, sizeof(*progress));
	psp_me_spsc_ring_acquire_initial(worker->commands, &allegrex_cache_ops);
	psp_me_spsc_ring_acquire_initial(worker->events, &allegrex_cache_ops);

	worker->last_stats.generation = progress->generation;
	worker->last_stats.commands_processed = progress->commands_processed;
	worker->last_stats.resets = progress->resets;
	worker->last_stats.syncs = progress->syncs;
	worker->last_stats.shutdowns = progress->shutdowns;
	worker->last_stats.shadow_commands = progress->shadow_commands;
	worker->last_stats.heartbeat = progress->heartbeat;
	worker->last_stats.fatal_error = progress->fatal_error;
	worker->last_stats.emulated_time = progress->emulated_time;
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

	if (!worker || !dispatch || !dispatch->start || !dispatch->wait ||
		capacity == 0 || (capacity & (capacity - 1u)) != 0 || worker->running)
		return false;

	memset(worker, 0, sizeof(*worker));
	worker->dispatch = *dispatch;
	worker->capacity = capacity;
	worker->ring_size = psp_me_spsc_ring_storage_size(capacity,
		sizeof(psp_me_sound_worker_message_t));
	if (worker->ring_size == 0)
		return false;

	worker->commands = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE, worker->ring_size);
	worker->events = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE, worker->ring_size);
	worker->shared_context = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE,
		sizeof(psp_me_sound_worker_shared_context_t));
	worker->main_control = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE,
		sizeof(psp_me_sound_worker_main_control_t));
	worker->progress = memalign(PSP_ME_SOUND_WORKER_CACHE_LINE,
		sizeof(psp_me_sound_worker_progress_t));
	if (!worker->commands || !worker->events || !worker->shared_context ||
		!worker->main_control || !worker->progress)
	{
		worker->last_stats.fatal_error = PSP_ME_SOUND_WORKER_ERROR_ALLOCATION;
		free_shared_state(worker);
		return false;
	}

	if (!psp_me_spsc_ring_init(worker->commands, worker->ring_size, capacity,
			sizeof(psp_me_sound_worker_message_t)) ||
		!psp_me_spsc_ring_init(worker->events, worker->ring_size, capacity,
			sizeof(psp_me_sound_worker_message_t)))
	{
		worker->last_stats.fatal_error = PSP_ME_SOUND_WORKER_ERROR_RING;
		free_shared_state(worker);
		return false;
	}

	control = (psp_me_sound_worker_main_control_t *)worker->main_control;
	progress = (psp_me_sound_worker_progress_t *)worker->progress;
	context = (psp_me_sound_worker_shared_context_t *)worker->shared_context;
	memset(control, 0, sizeof(*control));
	memset(progress, 0, sizeof(*progress));
	memset(context, 0, sizeof(*context));
	context->commands = worker->commands;
	context->events = worker->events;
	context->main_control = control;
	context->progress = progress;

	psp_me_spsc_ring_publish_initial(worker->commands, &allegrex_cache_ops);
	psp_me_spsc_ring_publish_initial(worker->events, &allegrex_cache_ops);
	sceKernelDcacheWritebackInvalidateRange(control, sizeof(*control));
	sceKernelDcacheWritebackInvalidateRange(progress, sizeof(*progress));
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
