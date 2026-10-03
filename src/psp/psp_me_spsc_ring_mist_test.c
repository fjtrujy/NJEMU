#include <malloc.h>
#include <string.h>
#include <pspkernel.h>
#include <me-core-mapper/me-core-mapper.h>
#include <me-core-mapper/hw-registers.h>
#include "psp/psp_me_spsc_ring.h"
#include "psp/psp_me_spsc_ring_mist_test.h"

#define PSP_ME_SPSC_RING_MIST_CAPACITY 256u
#define PSP_ME_SPSC_RING_MIST_STALL_US 5000000ULL

typedef struct psp_me_spsc_ring_mist_message
{
	uint32_t sequence;
	uint32_t inverse;
	uint32_t pattern;
	uint32_t echo;
} psp_me_spsc_ring_mist_message_t;

typedef struct psp_me_spsc_ring_mist_control
{
	psp_me_spsc_ring_t *to_me;
	psp_me_spsc_ring_t *to_main;
	uint32_t message_count;
	uint32_t abort_requested;
	uint32_t completed;
	uint32_t error;
	uint32_t reserved[10];
} psp_me_spsc_ring_mist_control_t;

_Static_assert(sizeof(psp_me_spsc_ring_mist_control_t) == 64,
	"ME ring MIST test control must occupy exactly one cache line");

static psp_me_spsc_ring_mist_control_t mist_control __attribute__((aligned(64)));

static uint32_t message_pattern(uint32_t sequence)
{
	return sequence * 1664525u + 1013904223u;
}

static psp_me_spsc_ring_mist_message_t make_message(uint32_t sequence)
{
	psp_me_spsc_ring_mist_message_t message;

	message.sequence = sequence;
	message.inverse = ~sequence;
	message.pattern = message_pattern(sequence);
	message.echo = 0;
	return message;
}

static bool message_valid(const psp_me_spsc_ring_mist_message_t *message,
	uint32_t sequence, bool echoed)
{
	return message->sequence == sequence &&
		message->inverse == ~sequence &&
		message->pattern == message_pattern(sequence) &&
		message->echo == (echoed ? (sequence ^ 0x5aa5a55au) : 0u);
}

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

static bool abort_requested(psp_me_spsc_ring_mist_control_t *control)
{
	meCoreDcacheInvalidateRange(control, sizeof(*control));
	return control->abort_requested != 0;
}

static void mist_worker(void *param)
{
	psp_me_spsc_ring_mist_control_t *control =
		(psp_me_spsc_ring_mist_control_t *)param;
	uint32_t expected = 0;
	uint32_t spin_count = 0;

	psp_me_spsc_ring_acquire_initial(control->to_me, &me_cache_ops);
	psp_me_spsc_ring_acquire_initial(control->to_main, &me_cache_ops);
	if (!psp_me_spsc_ring_is_valid(control->to_me) ||
		!psp_me_spsc_ring_is_valid(control->to_main))
	{
		control->error = PSP_ME_SPSC_RING_MIST_ERROR_RING;
		return;
	}

	while (expected < control->message_count)
	{
		psp_me_spsc_ring_mist_message_t message;
		uint32_t sequence;
		psp_me_spsc_ring_result_t ring_result;

		ring_result = psp_me_spsc_ring_try_pop(control->to_me, &me_cache_ops,
			&message, &sequence);
		if (ring_result == PSP_ME_SPSC_RING_EMPTY)
		{
			spin_count++;
			if ((spin_count & 0x3ffu) == 0 && abort_requested(control))
			{
				control->error = PSP_ME_SPSC_RING_MIST_ERROR_ABORTED;
				break;
			}
			continue;
		}
		if (ring_result != PSP_ME_SPSC_RING_OK || sequence != expected)
		{
			control->error = PSP_ME_SPSC_RING_MIST_ERROR_RING;
			break;
		}
		if (!message_valid(&message, expected, false))
		{
			control->error = PSP_ME_SPSC_RING_MIST_ERROR_PAYLOAD;
			break;
		}

		message.echo = expected ^ 0x5aa5a55au;
		spin_count = 0;
		for (;;)
		{
			ring_result = psp_me_spsc_ring_try_push(control->to_main, &me_cache_ops,
				&message, &sequence);
			if (ring_result == PSP_ME_SPSC_RING_OK)
				break;
			if (ring_result != PSP_ME_SPSC_RING_FULL)
			{
				control->error = PSP_ME_SPSC_RING_MIST_ERROR_RING;
				break;
			}
			spin_count++;
			if ((spin_count & 0x3ffu) == 0 && abort_requested(control))
			{
				control->error = PSP_ME_SPSC_RING_MIST_ERROR_ABORTED;
				break;
			}
		}
		if (control->error != PSP_ME_SPSC_RING_MIST_ERROR_NONE)
			break;
		if (sequence != expected)
		{
			control->error = PSP_ME_SPSC_RING_MIST_ERROR_RING;
			break;
		}

		expected++;
		if ((expected & 0x3fffu) == 0 && abort_requested(control))
		{
			control->error = PSP_ME_SPSC_RING_MIST_ERROR_ABORTED;
			break;
		}
	}

	control->completed = expected;
}

static void request_abort(psp_me_spsc_ring_mist_control_t *control)
{
	control->abort_requested = 1;
	sceKernelDcacheWritebackInvalidateRange(control, sizeof(*control));
}

static void copy_result(psp_me_spsc_ring_mist_result_t *result,
	const psp_me_spsc_ring_t *to_me, const psp_me_spsc_ring_t *to_main)
{
	result->completed = mist_control.completed;
	result->to_me_high_water = to_me->producer.high_water;
	result->to_me_overflow = to_me->producer.overflow_count;
	result->to_me_underflow = to_me->consumer.underflow_count;
	result->to_me_sequence_errors = to_me->consumer.sequence_error_count;
	result->to_me_corrupt = to_me->producer.corrupt_count +
		to_me->consumer.corrupt_count;
	result->to_main_high_water = to_main->producer.high_water;
	result->to_main_overflow = to_main->producer.overflow_count;
	result->to_main_underflow = to_main->consumer.underflow_count;
	result->to_main_sequence_errors = to_main->consumer.sequence_error_count;
	result->to_main_corrupt = to_main->producer.corrupt_count +
		to_main->consumer.corrupt_count;
}

bool psp_me_spsc_ring_mist_test_run(
	const psp_me_spsc_ring_mist_dispatch_t *dispatch,
	psp_me_spsc_ring_mist_result_t *result)
{
	const uint32_t total_messages = PSP_ME_SPSC_RING_MIST_LATENCY_MESSAGES +
		PSP_ME_SPSC_RING_MIST_BULK_MESSAGES;
	const size_t ring_size = psp_me_spsc_ring_storage_size(
		PSP_ME_SPSC_RING_MIST_CAPACITY, sizeof(psp_me_spsc_ring_mist_message_t));
	psp_me_spsc_ring_t *to_me = NULL;
	psp_me_spsc_ring_t *to_main = NULL;
	uint32_t sent = 0;
	uint32_t received = 0;
	uint32_t progress_snapshot = 0;
	uint32_t check_count = 0;
	uint64_t start_us = 0;
	uint64_t latency_end_us = 0;
	uint64_t end_us = 0;
	uint64_t last_progress_us = 0;
	bool started = false;

	if (!result)
		return false;
	memset(result, 0, sizeof(*result));
	if (!dispatch || !dispatch->start || !dispatch->wait)
	{
		result->error = PSP_ME_SPSC_RING_MIST_ERROR_DISPATCH;
		return false;
	}

	to_me = memalign(PSP_ME_SPSC_RING_CACHE_LINE, ring_size);
	to_main = memalign(PSP_ME_SPSC_RING_CACHE_LINE, ring_size);
	if (!to_me || !to_main ||
		!psp_me_spsc_ring_init(to_me, ring_size, PSP_ME_SPSC_RING_MIST_CAPACITY,
			sizeof(psp_me_spsc_ring_mist_message_t)) ||
		!psp_me_spsc_ring_init(to_main, ring_size, PSP_ME_SPSC_RING_MIST_CAPACITY,
			sizeof(psp_me_spsc_ring_mist_message_t)))
	{
		result->error = PSP_ME_SPSC_RING_MIST_ERROR_ALLOCATION;
		goto done;
	}

	memset(&mist_control, 0, sizeof(mist_control));
	mist_control.to_me = to_me;
	mist_control.to_main = to_main;
	mist_control.message_count = total_messages;
	psp_me_spsc_ring_publish_initial(to_me, &allegrex_cache_ops);
	psp_me_spsc_ring_publish_initial(to_main, &allegrex_cache_ops);
	sceKernelDcacheWritebackInvalidateRange(&mist_control, sizeof(mist_control));

	if (!dispatch->start(mist_worker, &mist_control, sizeof(mist_control),
		dispatch->opaque))
	{
		result->error = PSP_ME_SPSC_RING_MIST_ERROR_DISPATCH;
		goto done;
	}
	started = true;

	start_us = sceKernelGetSystemTimeWide();
	last_progress_us = start_us;
	while (received < total_messages &&
		result->error == PSP_ME_SPSC_RING_MIST_ERROR_NONE)
	{
		if (sent < total_messages &&
			(sent >= PSP_ME_SPSC_RING_MIST_LATENCY_MESSAGES || sent == received))
		{
			psp_me_spsc_ring_mist_message_t message = make_message(sent);
			uint32_t sequence;
			psp_me_spsc_ring_result_t push_result = psp_me_spsc_ring_try_push(
				to_me, &allegrex_cache_ops, &message, &sequence);
			if (push_result == PSP_ME_SPSC_RING_OK)
			{
				if (sequence != sent)
					result->error = PSP_ME_SPSC_RING_MIST_ERROR_RING;
				else
					sent++;
			}
			else if (push_result != PSP_ME_SPSC_RING_FULL)
				result->error = PSP_ME_SPSC_RING_MIST_ERROR_RING;
		}

		if (received < sent && result->error == PSP_ME_SPSC_RING_MIST_ERROR_NONE)
		{
			psp_me_spsc_ring_mist_message_t message;
			uint32_t sequence;
			psp_me_spsc_ring_result_t pop_result = psp_me_spsc_ring_try_pop(
				to_main, &allegrex_cache_ops, &message, &sequence);
			if (pop_result == PSP_ME_SPSC_RING_OK)
			{
				if (sequence != received || !message_valid(&message, received, true))
					result->error = PSP_ME_SPSC_RING_MIST_ERROR_PAYLOAD;
				else
				{
					received++;
					if (received == PSP_ME_SPSC_RING_MIST_LATENCY_MESSAGES)
						latency_end_us = sceKernelGetSystemTimeWide();
				}
			}
			else if (pop_result != PSP_ME_SPSC_RING_EMPTY)
				result->error = PSP_ME_SPSC_RING_MIST_ERROR_RING;
		}

		check_count++;
		if ((check_count & 0x3ffu) == 0)
		{
			uint64_t now_us = sceKernelGetSystemTimeWide();
			if (sent + received != progress_snapshot)
			{
				progress_snapshot = sent + received;
				last_progress_us = now_us;
			}
			else if (now_us - last_progress_us > PSP_ME_SPSC_RING_MIST_STALL_US)
				result->error = PSP_ME_SPSC_RING_MIST_ERROR_TIMEOUT;
		}
	}

	if (result->error != PSP_ME_SPSC_RING_MIST_ERROR_NONE)
		request_abort(&mist_control);
	dispatch->wait(dispatch->opaque);
	started = false;
	end_us = sceKernelGetSystemTimeWide();
	sceKernelDcacheInvalidateRange(&mist_control, sizeof(mist_control));
	psp_me_spsc_ring_acquire_initial(to_me, &allegrex_cache_ops);
	psp_me_spsc_ring_acquire_initial(to_main, &allegrex_cache_ops);

	if (result->error == PSP_ME_SPSC_RING_MIST_ERROR_NONE &&
		(mist_control.error != PSP_ME_SPSC_RING_MIST_ERROR_NONE ||
		 mist_control.completed != total_messages || received != total_messages))
		result->error = mist_control.error ? mist_control.error :
			PSP_ME_SPSC_RING_MIST_ERROR_RING;
	if (latency_end_us == 0)
		latency_end_us = end_us;
	result->latency_us = latency_end_us - start_us;
	result->bulk_us = end_us - latency_end_us;
	copy_result(result, to_me, to_main);

 done:
	if (started)
	{
		request_abort(&mist_control);
		dispatch->wait(dispatch->opaque);
	}
	free(to_me);
	free(to_main);
	return result->error == PSP_ME_SPSC_RING_MIST_ERROR_NONE;
}
