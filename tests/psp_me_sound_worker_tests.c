#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "psp/psp_me_sound_worker.h"

#define TEST_TIMEOUT_US 2000000ULL
#define TEST_SHADOW_MESSAGES 10048u

typedef struct host_dispatch
{
	pthread_t thread;
	void (*task)(void *);
	void *data;
	int started;
} host_dispatch_t;

static void *host_thread_entry(void *opaque)
{
	host_dispatch_t *dispatch = (host_dispatch_t *)opaque;
	dispatch->task(dispatch->data);
	return NULL;
}

static bool host_dispatch_start(void (*task)(void *), void *data, uint32_t size,
	void *opaque)
{
	host_dispatch_t *dispatch = (host_dispatch_t *)opaque;
	(void)size;

	if (dispatch->started)
		return false;
	dispatch->task = task;
	dispatch->data = data;
	if (pthread_create(&dispatch->thread, NULL, host_thread_entry, dispatch) != 0)
		return false;
	dispatch->started = 1;
	return true;
}

static void host_dispatch_wait(void *opaque)
{
	host_dispatch_t *dispatch = (host_dispatch_t *)opaque;
	if (!dispatch->started)
		return;
	pthread_join(dispatch->thread, NULL);
	dispatch->started = 0;
}

void sceKernelDcacheWritebackInvalidateRange(void *address, uint32_t size)
{
	(void)address;
	(void)size;
}

void sceKernelDcacheInvalidateRange(void *address, uint32_t size)
{
	(void)address;
	(void)size;
}

void meCoreDcacheWritebackRange(void *address, uint32_t size)
{
	(void)address;
	(void)size;
}

void meCoreDcacheInvalidateRange(void *address, uint32_t size)
{
	(void)address;
	(void)size;
}

uint64_t sceKernelGetSystemTimeWide(void)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	return (uint64_t)now.tv_sec * 1000000ULL + (uint64_t)now.tv_usec;
}

static int wait_shadow_empty(psp_me_sound_worker_t *worker)
{
	uint64_t start = sceKernelGetSystemTimeWide();

	for (;;)
	{
		psp_me_sound_worker_stats_t stats;
		if (!psp_me_sound_worker_poll(worker))
			return 0;
		psp_me_sound_worker_get_stats(worker, &stats);
		if (stats.shadow_pending == 0)
			return 1;
		if (sceKernelGetSystemTimeWide() - start >= TEST_TIMEOUT_US)
			return 0;
		sched_yield();
	}
}

static int send_shadow_burst(psp_me_sound_worker_t *worker, uint32_t first,
	uint32_t count, uint64_t base_time)
{
	uint32_t i;

	for (i = 0; i < count; i++)
	{
		uint32_t sequence = first + i;
		uint8_t command = (uint8_t)(sequence * 37u + 11u);
		uint64_t emulated_time = base_time + (uint64_t)(sequence / 4u);
		if (!psp_me_sound_worker_shadow_sound(worker, command, emulated_time))
			return 0;
	}
	return 1;
}

static int test_shadow_order_reset_and_sync(void)
{
	host_dispatch_t host = { 0 };
	psp_me_sound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_sound_worker_t worker;
	psp_me_sound_worker_stats_t stats;
	uint32_t sent = 0;
	const uint32_t first_generation_messages = 48u;

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "worker startup/reset failed\n");
		return 0;
	}

	/* Leave the first generation echoes pending on purpose. RESET must drain
	 * them in-order while it waits for its own ACK. */
	if (!send_shadow_burst(&worker, 0u, first_generation_messages, 1000u) ||
		!psp_me_sound_worker_reset(&worker, 2u, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "interleaved shadow/reset failed\n");
		psp_me_sound_worker_abort(&worker);
		return 0;
	}
	sent = first_generation_messages;

	while (sent < TEST_SHADOW_MESSAGES)
	{
		uint32_t count = TEST_SHADOW_MESSAGES - sent;
		if (count > 32u)
			count = 32u;
		if (!send_shadow_burst(&worker, sent, count, 5000u) ||
			!wait_shadow_empty(&worker))
		{
			fprintf(stderr, "shadow burst failed at %u\n", sent);
			psp_me_sound_worker_abort(&worker);
			return 0;
		}
		sent += count;
	}

	if (!psp_me_sound_worker_sync(&worker, 5000u + TEST_SHADOW_MESSAGES,
		TEST_TIMEOUT_US) || !psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "shadow sync/shutdown failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	psp_me_sound_worker_get_stats(&worker, &stats);
	if (stats.generation != 2u ||
		stats.shadow_commands != TEST_SHADOW_MESSAGES ||
		stats.shadow_sent != TEST_SHADOW_MESSAGES ||
		stats.shadow_matched != TEST_SHADOW_MESSAGES ||
		stats.shadow_mismatches != 0u || stats.shadow_send_failures != 0u ||
		stats.shadow_pending != 0u || stats.command_overflow != 0u ||
		stats.event_overflow != 0u || stats.fatal_error != PSP_ME_SOUND_WORKER_ERROR_NONE)
	{
		fprintf(stderr,
			"shadow stats mismatch: gen=%u commands=%u sent=%u matched=%u mismatch=%u "
			"send_fail=%u pending=%u cmd_overflow=%u event_overflow=%u fatal=%u\n",
			stats.generation, stats.shadow_commands, stats.shadow_sent,
			stats.shadow_matched, stats.shadow_mismatches, stats.shadow_send_failures,
			stats.shadow_pending, stats.command_overflow, stats.event_overflow,
			stats.fatal_error);
		return 0;
	}
	return 1;
}

static int test_time_regression_is_fatal(void)
{
	host_dispatch_t host = { 0 };
	psp_me_sound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_sound_worker_t worker;
	psp_me_sound_worker_stats_t stats;
	uint64_t start;
	int observed_failure = 0;

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 8u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_shadow_sound(&worker, 0x12u, 100u) ||
		!wait_shadow_empty(&worker) ||
		!psp_me_sound_worker_shadow_sound(&worker, 0x34u, 99u))
	{
		fprintf(stderr, "time-regression setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	start = sceKernelGetSystemTimeWide();
	while (sceKernelGetSystemTimeWide() - start < TEST_TIMEOUT_US)
	{
		if (!psp_me_sound_worker_poll(&worker))
		{
			observed_failure = 1;
			break;
		}
		sched_yield();
	}
	psp_me_sound_worker_abort(&worker);
	psp_me_sound_worker_get_stats(&worker, &stats);
	if (!observed_failure || stats.fatal_error != PSP_ME_SOUND_WORKER_ERROR_TIME_REGRESSION)
	{
		fprintf(stderr, "time regression was not rejected: observed=%d fatal=%u\n",
			observed_failure, stats.fatal_error);
		return 0;
	}
	return 1;
}

int main(void)
{
	if (!test_shadow_order_reset_and_sync() || !test_time_regression_is_fatal())
		return 1;

	printf("PSP ME sound worker host oracle: 10,048 ordered shadow commands, reset/sync interleave, and time regression passed\n");
	return 0;
}
