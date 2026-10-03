#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "psp/psp_me_sound_worker.h"

#define TEST_TIMEOUT_US 2000000ULL
#define TEST_SHADOW_MESSAGES 10048u

static uint8_t reference_memory[0x20000];
static psp_me_sound_z80_io_t reference_io[PSP_ME_SOUND_Z80_IO_CAPACITY];
static uint32_t reference_io_count;
static cz80_struc *reference_irq_cpu;

static uint8_t reference_z80_read(uint32_t address)
{
	return reference_memory[address & 0xffffu];
}

static void reference_z80_write(uint32_t address, uint8_t value)
{
	address &= 0xffffu;
	if (address >= PSP_ME_SOUND_Z80_RAM_OFFSET)
		reference_memory[address] = value;
}

static uint8_t reference_z80_port_read(uint16_t port)
{
	psp_me_sound_z80_io_t *entry = &reference_io[reference_io_count++];
	entry->port = port;
	entry->type = PSP_ME_SOUND_Z80_IO_READ;
	entry->value = 0x5au;
	return entry->value;
}

static void reference_z80_port_write(uint16_t port, uint8_t value)
{
	psp_me_sound_z80_io_t *entry = &reference_io[reference_io_count++];
	entry->port = port;
	entry->type = PSP_ME_SOUND_Z80_IO_WRITE;
	entry->value = value;
	if ((uint8_t)port == 0x0c && reference_irq_cpu)
	{
		entry = &reference_io[reference_io_count++];
		entry->port = 0;
		entry->type = PSP_ME_SOUND_Z80_IO_IRQ;
		entry->value = ASSERT_LINE;
		Cz80_Set_IRQ(reference_irq_cpu, 0, ASSERT_LINE);
	}
}

static uint32_t reference_ram_hash(void)
{
	uint32_t hash = 2166136261u;
	uint32_t i;

	for (i = PSP_ME_SOUND_Z80_RAM_OFFSET; i < PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE; i++)
	{
		hash ^= reference_memory[i];
		hash *= 16777619u;
	}
	return hash;
}

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

void sceKernelDcacheWritebackAll(void)
{
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

static int test_z80_shadow_slice_matches_reference(void)
{
	host_dispatch_t host = { 0 };
	psp_me_sound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_sound_worker_t worker;
	psp_me_sound_worker_stats_t stats;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	cz80_state_t expected_state;
	const uint32_t banks[4] = { 0x8000u, 0xc000u, 0xe000u, 0xf000u };
	const uint32_t cycles = 45u;

	memset(reference_memory, 0, sizeof(reference_memory));
	memset(reference_io, 0, sizeof(reference_io));
	reference_io_count = 0;
	/* IN A,(04); OUT (0c),A; LD (f800),A; JP 0000. */
	reference_memory[0x0000] = 0xdb;
	reference_memory[0x0001] = 0x04;
	reference_memory[0x0002] = 0xd3;
	reference_memory[0x0003] = 0x0c;
	reference_memory[0x0004] = 0x32;
	reference_memory[0x0005] = 0x00;
	reference_memory[0x0006] = 0xf8;
	reference_memory[0x0007] = 0xc3;
	reference_memory[0x0008] = 0x00;
	reference_memory[0x0009] = 0x00;

	Cz80_Init(&reference_cpu);
	Cz80_Set_Fetch(&reference_cpu, 0x0000u, 0xffffu,
		(uintptr_t)reference_memory);
	Cz80_Set_ReadBase(&reference_cpu, (uintptr_t)reference_memory);
	Cz80_Set_ReadB(&reference_cpu, reference_z80_read);
	Cz80_Set_WriteB(&reference_cpu, reference_z80_write);
	Cz80_Set_INPort(&reference_cpu, reference_z80_port_read);
	Cz80_Set_OUTPort(&reference_cpu, reference_z80_port_write);
	Cz80_Reset(&reference_cpu);
	Cz80_Get_State(&reference_cpu, &initial_state);
	reference_irq_cpu = &reference_cpu;

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0, 0, 0,
			TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Z80 shadow snapshot setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	(void)Cz80_Exec(&reference_cpu, (int32_t)cycles);
	reference_irq_cpu = NULL;
	Cz80_Get_State(&reference_cpu, &expected_state);
	if (reference_io_count != 3u ||
		!psp_me_sound_worker_z80_slice(&worker, reference_io, reference_io_count,
			cycles, 100u, &expected_state, banks, reference_ram_hash(), true) ||
		!psp_me_sound_worker_sync(&worker, 100u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		psp_me_sound_worker_get_stats(&worker, &stats);
		fprintf(stderr,
			"Z80 shadow slice execution failed: io_count=%u slices=%u io=%u state=%u "
			"ram=%u bank=%u io_mismatch=%u send_fail=%u fatal=%u\n",
			reference_io_count, stats.z80_slices, stats.z80_io_events,
			stats.z80_state_mismatches, stats.z80_ram_mismatches,
			stats.z80_bank_mismatches, stats.z80_io_mismatches,
			stats.z80_send_failures, stats.fatal_error);
		return 0;
	}

	psp_me_sound_worker_get_stats(&worker, &stats);
		if (stats.z80_snapshots != 1u || stats.z80_slices != 1u ||
			stats.z80_io_events != 3u || stats.z80_state_mismatches != 0u ||
		stats.z80_ram_mismatches != 0u || stats.z80_bank_mismatches != 0u ||
		stats.z80_io_mismatches != 0u || stats.z80_send_failures != 0u ||
		stats.fatal_error != PSP_ME_SOUND_WORKER_ERROR_NONE)
	{
		fprintf(stderr,
			"Z80 shadow stats mismatch: snapshots=%u slices=%u io=%u state=%u ram=%u "
			"bank=%u io_mismatch=%u send_fail=%u fatal=%u\n",
			stats.z80_snapshots, stats.z80_slices, stats.z80_io_events,
			stats.z80_state_mismatches, stats.z80_ram_mismatches,
			stats.z80_bank_mismatches, stats.z80_io_mismatches,
			stats.z80_send_failures, stats.fatal_error);
		return 0;
	}
	return 1;
}

int main(void)
{
	if (!test_shadow_order_reset_and_sync() || !test_time_regression_is_fatal() ||
		!test_z80_shadow_slice_matches_reference())
		return 1;

	printf("PSP ME sound worker host oracle: C3 transport/lifecycle plus isolated C4 Z80 slice passed\n");
	return 0;
}
