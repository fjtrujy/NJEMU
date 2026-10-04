#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "common/sound.h"
#include "psp/psp_me_sound_worker.h"

#define TEST_TIMEOUT_US 2000000ULL
#define TEST_SHADOW_MESSAGES 10048u

int option_samplerate;
static struct sound_t test_sound;
struct sound_t *sound = &test_sound;

float timer_get_time(void)
{
	return 0.0f;
}

static uint8_t reference_memory[0x20000];
static psp_me_sound_z80_io_t reference_io[PSP_ME_SOUND_Z80_IO_CAPACITY];
static uint32_t reference_io_count;
static uint8_t reference_port_read_value;
static cz80_struc *reference_preempt_cpu;
static bool reference_preempt_on_timer_start;

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
	entry->value = reference_port_read_value;
	return entry->value;
}

static void reference_z80_port_write(uint16_t port, uint8_t value)
{
	psp_me_sound_z80_io_t *entry = &reference_io[reference_io_count++];
	entry->port = port;
	entry->type = PSP_ME_SOUND_Z80_IO_WRITE;
	entry->value = value;
	if (reference_preempt_on_timer_start && (uint8_t)port == 0x05 && value == 0x05)
	{
		entry = &reference_io[reference_io_count++];
		entry->port = 0;
		entry->type = PSP_ME_SOUND_Z80_IO_PREEMPT;
		entry->value = 0;
		reference_preempt_on_timer_start = false;
		if (reference_preempt_cpu)
			reference_preempt_cpu->ICount = 0;
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

static ym2610_context_t *alloc_ym_context(void **storage_out)
{
	size_t size = YM2610ContextSize();
	size_t alignment = YM2610ContextAlignment();
	uint8_t *storage = malloc(size + alignment - 1u);
	uintptr_t aligned;

	if (!storage)
		return NULL;
	aligned = ((uintptr_t)storage + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
	*storage_out = storage;
	return (ym2610_context_t *)aligned;
}

static void ym_write_b(ym2610_context_t *context, uint8_t reg, uint8_t value)
{
	YM2610ContextWrite(context, 2, reg);
	YM2610ContextWrite(context, 3, value);
}

static void configure_adpcma_zero(ym2610_context_t *context)
{
	ym_write_b(context, 0x01, 0x3f);
	ym_write_b(context, 0x08, 0xdf);
	ym_write_b(context, 0x10, 0x00);
	ym_write_b(context, 0x18, 0x00);
	ym_write_b(context, 0x20, 0x00);
	ym_write_b(context, 0x28, 0x00);
	ym_write_b(context, 0x00, 0x01);
}

static void configure_default_adpcma_zero(void)
{
	YM2610Write(2, 0x01); YM2610Write(3, 0x3f);
	YM2610Write(2, 0x08); YM2610Write(3, 0xdf);
	YM2610Write(2, 0x10); YM2610Write(3, 0x00);
	YM2610Write(2, 0x18); YM2610Write(3, 0x00);
	YM2610Write(2, 0x20); YM2610Write(3, 0x00);
	YM2610Write(2, 0x28); YM2610Write(3, 0x00);
	YM2610Write(2, 0x00); YM2610Write(3, 0x01);
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
	reference_port_read_value = 0;
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
	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0, 0, 0,
			44100u, 0x1000u, 0x1000u, false, PSP_ME_SOUND_Z80_MODE_ORACLE,
			TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Z80 shadow snapshot setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	(void)Cz80_Exec(&reference_cpu, (int32_t)cycles);
	Cz80_Get_State(&reference_cpu, &expected_state);
	if (reference_io_count != 2u ||
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
				stats.z80_io_events != 2u || stats.z80_state_mismatches != 0u ||
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

static int test_z80_shadow_large_io_trace(void)
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
	const uint32_t expected_io = 300u;
	const uint32_t cycles = 7u + expected_io * 23u;

	memset(reference_memory, 0, sizeof(reference_memory));
	memset(reference_io, 0, sizeof(reference_io));
	reference_io_count = 0;
	reference_port_read_value = 0;
	reference_preempt_cpu = NULL;
	reference_preempt_on_timer_start = false;

	/* LD A,5a; loop: OUT (0c),A; JR loop.  Each loop iteration is 23 cycles,
	 * so this generates exactly 300 writes in one scheduler slice. */
	reference_memory[0x0000] = 0x3e;
	reference_memory[0x0001] = 0x5a;
	reference_memory[0x0002] = 0xd3;
	reference_memory[0x0003] = 0x0c;
	reference_memory[0x0004] = 0x18;
	reference_memory[0x0005] = 0xfc;

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

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0, 0, 0,
			44100u, 0x1000u, 0x1000u, false, PSP_ME_SOUND_Z80_MODE_ORACLE,
			TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Large Z80 trace snapshot setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	(void)Cz80_Exec(&reference_cpu, (int32_t)cycles);
	Cz80_Get_State(&reference_cpu, &expected_state);
	if (reference_io_count != expected_io ||
		!psp_me_sound_worker_z80_slice(&worker, reference_io, reference_io_count,
			cycles, 100u, &expected_state, banks, reference_ram_hash(), true) ||
		!psp_me_sound_worker_sync(&worker, 100u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Large Z80 trace replay failed: io=%u expected=%u\n",
			reference_io_count, expected_io);
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	psp_me_sound_worker_get_stats(&worker, &stats);
	if (stats.z80_slices != 1u || stats.z80_io_events != expected_io ||
		stats.z80_state_mismatches != 0u || stats.z80_ram_mismatches != 0u ||
		stats.z80_bank_mismatches != 0u || stats.z80_io_mismatches != 0u ||
		stats.z80_send_failures != 0u || stats.z80_batch_overflow != 0u ||
		stats.fatal_error != PSP_ME_SOUND_WORKER_ERROR_NONE)
	{
		fprintf(stderr,
			"Large Z80 trace stats mismatch: slices=%u io=%u state=%u ram=%u bank=%u "
			"io_mismatch=%u send=%u batch_overflow=%u fatal=%u\n",
			stats.z80_slices, stats.z80_io_events, stats.z80_state_mismatches,
			stats.z80_ram_mismatches, stats.z80_bank_mismatches,
			stats.z80_io_mismatches, stats.z80_send_failures,
			stats.z80_batch_overflow, stats.fatal_error);
		return 0;
	}
	return 1;
}

static int test_sound_status_snapshot(void)
{
	host_dispatch_t host = { 0 };
	psp_me_sound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_sound_worker_t worker;
	psp_me_sound_worker_stats_t stats;
	psp_me_sound_status_snapshot_t initial_status;
	psp_me_sound_status_snapshot_t final_status;
	uint8_t presented_pending = 0xaau;
	uint8_t presented_result = 0xbbu;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	cz80_state_t expected_state;
	const uint32_t banks[4] = { 0x8000u, 0xc000u, 0xe000u, 0xf000u };
	const uint32_t cycles = 22u;

	memset(reference_memory, 0, sizeof(reference_memory));
	memset(reference_io, 0, sizeof(reference_io));
	reference_io_count = 0;
	reference_port_read_value = 0x5au;
	reference_preempt_cpu = NULL;
	reference_preempt_on_timer_start = false;
	/* IN A,(00) consumes the command, OUT (0c),A publishes the result. */
	reference_memory[0x0000] = 0xdb;
	reference_memory[0x0001] = 0x00;
	reference_memory[0x0002] = 0xd3;
	reference_memory[0x0003] = 0x0c;

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

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0x5au, 1u, 0x22u,
			44100u, 0x1000u, 0x1000u, false, PSP_ME_SOUND_Z80_MODE_AUTONOMOUS,
			TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_read_status(&worker, &initial_status))
	{
		fprintf(stderr, "Sound status snapshot setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}
	if (initial_status.generation != 1u || initial_status.emulated_time != 0u ||
		initial_status.sound_code != 0x5au || initial_status.pending_command != 1u ||
		initial_status.result_code != 0x22u || initial_status.sequence == 0u)
	{
		fprintf(stderr,
			"Initial sound status mismatch: gen=%u seq=%u time=%llu code=%u pending=%u result=%u\n",
			initial_status.generation, initial_status.sequence,
			(unsigned long long)initial_status.emulated_time, initial_status.sound_code,
			initial_status.pending_command, initial_status.result_code);
		psp_me_sound_worker_abort(&worker);
		return 0;
	}
	(void)Cz80_Exec(&reference_cpu, (int32_t)cycles);
	Cz80_Get_State(&reference_cpu, &expected_state);
	if (reference_io_count != 2u ||
		!psp_me_sound_worker_z80_advance(&worker, cycles, 1000u, 101u) ||
		!psp_me_sound_worker_z80_checkpoint(&worker, &expected_state, banks,
			reference_ram_hash(), 101u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_sync(&worker, 102u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_read_status(&worker, &final_status) ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Sound status snapshot replay failed: io=%u\n",
			reference_io_count);
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}
	if (final_status.generation != 1u || final_status.emulated_time != 102u ||
		final_status.sound_code != 0x5au || final_status.pending_command != 0u ||
		final_status.result_code != 0x5au ||
		final_status.last_advance_elapsed_us != cycles / 4u ||
		final_status.sequence <= initial_status.sequence)
	{
		fprintf(stderr,
			"Final sound status mismatch: gen=%u seq=%u time=%llu code=%u pending=%u result=%u\n",
			final_status.generation, final_status.sequence,
			(unsigned long long)final_status.emulated_time, final_status.sound_code,
			final_status.pending_command, final_status.result_code);
		return 0;
	}
	if (psp_me_sound_worker_validate_status(&final_status, 1u, 102u,
			0x5au, 0u, 0x5au) != PSP_ME_SOUND_STATUS_MATCH ||
		psp_me_sound_worker_validate_status(&final_status, 1u, 103u,
			0x5au, 0u, 0x5au) != PSP_ME_SOUND_STATUS_STALE ||
		psp_me_sound_worker_validate_status(&final_status, 1u, 102u,
			0x5au, 0u, 0x22u) != PSP_ME_SOUND_STATUS_MISMATCH ||
		psp_me_sound_worker_validate_status(&final_status, 2u, 102u,
			0x5au, 0u, 0x5au) != PSP_ME_SOUND_STATUS_UNAVAILABLE)
	{
		fprintf(stderr, "Sound status presentation validation failed\n");
		return 0;
	}
	if (psp_me_sound_worker_present_status(&final_status, 1u, 102u,
			0x5au, 0u, 0x5au, &presented_pending, &presented_result) !=
			PSP_ME_SOUND_STATUS_MATCH ||
		presented_pending != 0u || presented_result != 0x5au)
	{
		fprintf(stderr, "Sound status ME presentation failed: pending=%u result=%u\n",
			presented_pending, presented_result);
		return 0;
	}
	presented_pending = 0xaau;
	presented_result = 0xbbu;
	if (psp_me_sound_worker_present_status(&final_status, 1u, 103u,
			0x5au, 0u, 0x5au, &presented_pending, &presented_result) !=
			PSP_ME_SOUND_STATUS_STALE ||
		presented_pending != 0xaau || presented_result != 0xbbu ||
		psp_me_sound_worker_present_status(&final_status, 1u, 102u,
			0x5au, 0u, 0x22u, &presented_pending, &presented_result) !=
			PSP_ME_SOUND_STATUS_MISMATCH ||
		presented_pending != 0xaau || presented_result != 0xbbu)
	{
		fprintf(stderr, "Sound status fail-closed presentation mutated fallback values\n");
		return 0;
	}
	psp_me_sound_worker_get_stats(&worker, &stats);
	if (stats.z80_autonomous_slices != 1u || stats.z80_checkpoints != 1u ||
		stats.z80_io_events != 0u || stats.z80_state_mismatches != 0u ||
		stats.z80_ram_mismatches != 0u || stats.z80_bank_mismatches != 0u)
	{
		fprintf(stderr,
			"Autonomous status stats mismatch: slices=%u checkpoints=%u io=%u state=%u ram=%u bank=%u\n",
			stats.z80_autonomous_slices, stats.z80_checkpoints,
			stats.z80_io_events, stats.z80_state_mismatches,
			stats.z80_ram_mismatches, stats.z80_bank_mismatches);
		return 0;
	}
	return 1;
}

static int test_sound_status_fence_ordering(void)
{
	host_dispatch_t host = { 0 };
	psp_me_sound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_sound_worker_t worker;
	psp_me_sound_status_snapshot_t status;
	psp_me_sound_fence_result_t fence_result;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	uint64_t fenced_time = 0;
	const uint32_t banks[4] = { 0x8000u, 0xc000u, 0xe000u, 0xf000u };

	memset(reference_memory, 0, sizeof(reference_memory));
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

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0, 0, 0,
			44100u, 0x1000u, 0x1000u, false,
			PSP_ME_SOUND_Z80_MODE_AUTONOMOUS, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_shadow_sound(&worker, 0x66u, 20u) ||
		!psp_me_sound_worker_fence(&worker, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_read_status(&worker, &status))
	{
		fprintf(stderr, "Sound status fence setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}
	if (status.emulated_time != 20u || status.sound_code != 0x66u ||
		status.pending_command != 1u || status.last_advance_elapsed_us != 0u)
	{
		fprintf(stderr,
			"Sound status fence mutated time/state: time=%llu code=%u pending=%u elapsed=%u\n",
			(unsigned long long)status.emulated_time, status.sound_code,
			status.pending_command, status.last_advance_elapsed_us);
		psp_me_sound_worker_abort(&worker);
		return 0;
	}
	/* Equal timestamps do not prove FIFO visibility: a newly queued sound
	 * command can legitimately make the previous snapshot look mismatched. */
	if (psp_me_sound_worker_validate_status(&status, 1u, 20u,
			0x77u, 1u, 0u) != PSP_ME_SOUND_STATUS_MISMATCH ||
		!psp_me_sound_worker_shadow_sound(&worker, 0x77u, 20u) ||
		!psp_me_sound_worker_shadow_sound(&worker, 0x88u, 20u) ||
		!psp_me_sound_worker_shadow_sound(&worker, 0x88u, 20u) ||
		!psp_me_sound_worker_shadow_sound(&worker, 0x99u, 20u) ||
		!psp_me_sound_worker_fence(&worker, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_read_status(&worker, &status) ||
		psp_me_sound_worker_validate_status(&status, 1u, 20u,
			0x99u, 1u, 0u) != PSP_ME_SOUND_STATUS_MATCH)
	{
		fprintf(stderr, "Sound status equal-time fence recovery failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	if (!psp_me_sound_worker_fence_begin(&worker) ||
		!psp_me_sound_worker_z80_advance_horizon(&worker, 40u, 40u))
	{
		fprintf(stderr, "Sound status old-fence setup failed\n");
		psp_me_sound_worker_abort(&worker);
		return 0;
	}
	do
	{
		fence_result = psp_me_sound_worker_fence_poll(&worker, &fenced_time);
		if (fence_result == PSP_ME_SOUND_FENCE_PENDING)
			sched_yield();
	} while (fence_result == PSP_ME_SOUND_FENCE_PENDING);
	if (fence_result != PSP_ME_SOUND_FENCE_COMPLETE || fenced_time != 20u)
	{
		fprintf(stderr, "Sound status old fence coverage mismatch: result=%d time=%llu\n",
			(int)fence_result, (unsigned long long)fenced_time);
		psp_me_sound_worker_abort(&worker);
		return 0;
	}

	if (!psp_me_sound_worker_fence(&worker, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_read_status(&worker, &status) ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Sound status fence ordering failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}
	if (status.emulated_time != 40u || status.last_advance_elapsed_us != 40u)
	{
		fprintf(stderr,
			"Sound status fence did not observe prior horizon: time=%llu elapsed=%u\n",
			(unsigned long long)status.emulated_time,
			status.last_advance_elapsed_us);
		return 0;
	}
	return 1;
}

static int test_sound_recovery_snapshot(void)
{
	host_dispatch_t host = { 0 };
	psp_me_sound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_sound_worker_t worker;
	psp_me_sound_recovery_snapshot_t recovery;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	cz80_state_t expected_state;
	cz80_state_t restored_state;
	static uint8_t recovered_ram[PSP_ME_SOUND_Z80_RAM_SIZE];
	static uint8_t pcm_a[0x1000];
	static uint8_t pcm_b[0x1000];
	const uint32_t banks[4] = { 0x8000u, 0xc000u, 0xe000u, 0xf000u };
	void *ym_storage = NULL;
	void *restored_ym_storage = NULL;
	ym2610_context_t *recovered_ym = alloc_ym_context(&ym_storage);
	ym2610_context_t *restored_ym = alloc_ym_context(&restored_ym_storage);
	uint32_t restored_banks[4] = { 0, 0, 0, 0 };
	uint8_t restored_sound_code = 0;
	uint8_t restored_pending_command = 0;
	uint8_t restored_result_code = 0;
	const uint8_t program[] = {
		0x3e, 0x08, 0xd3, 0x04, /* Select SSG amplitude register. */
		0x3e, 0x0f, 0xd3, 0x05, /* Make YM state observably non-zero. */
		0xdb, 0x00,             /* Consume sound command into A. */
		0x32, 0x00, 0xf8,       /* Persist it in Z80 RAM. */
		0xd3, 0x0c,             /* Publish result byte. */
		0x76,                   /* Halt at a stable checkpoint. */
	};

	if (!recovered_ym || !restored_ym)
	{
		fprintf(stderr, "Recovery YM context allocation failed\n");
		free(restored_ym_storage);
		free(ym_storage);
		return 0;
	}
	memset(pcm_a, 0, sizeof(pcm_a));
	memset(pcm_b, 0, sizeof(pcm_b));
	memset(reference_memory, 0, sizeof(reference_memory));
	memcpy(reference_memory, program, sizeof(program));
	memset(recovered_ram, 0xa5, sizeof(recovered_ram));
	memset(reference_io, 0, sizeof(reference_io));
	reference_io_count = 0;
	reference_port_read_value = 0x5au;
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

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0x5au, 1u, 0x22u,
			44100u, 0x1000u, 0x1000u, false,
			PSP_ME_SOUND_Z80_MODE_AUTONOMOUS, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_advance_horizon(&worker, 25u, 1000u))
	{
		fprintf(stderr, "Recovery snapshot setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		free(restored_ym_storage);
		free(ym_storage);
		return 0;
	}

	(void)Cz80_Exec(&reference_cpu, 100);
	Cz80_Get_State(&reference_cpu, &expected_state);
	if (!psp_me_sound_worker_read_recovery_snapshot(&worker, &recovery,
			recovered_ram, recovered_ym, TEST_TIMEOUT_US) ||
		recovery.generation != 1u || recovery.emulated_time != 25u ||
		recovery.z80_time != 25u || recovery.sound_code != 0x5au ||
		recovery.pending_command != 0u || recovery.result_code != 0x5au ||
		memcmp(&recovery.state, &expected_state, sizeof(expected_state)) != 0 ||
		memcmp(recovery.banks, banks, sizeof(banks)) != 0 ||
		recovered_ram[0] != 0x5au ||
		(YM2610ContextWrite(recovered_ym, 0, 0x08),
			YM2610ContextRead(recovered_ym, 1)) != 0x0fu ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr,
			"Recovery snapshot mismatch: gen=%u time=%llu z80=%llu code=%u pending=%u result=%u ram=%u\n",
			recovery.generation, (unsigned long long)recovery.emulated_time,
			(unsigned long long)recovery.z80_time, recovery.sound_code,
			recovery.pending_command, recovery.result_code,
			recovered_ram[0]);
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		free(restored_ym_storage);
		free(ym_storage);
		return 0;
	}

	/* Deliberately corrupt every CPU-side component, then rebuild it solely
	 * from the recovery payload. This models the future failback fence without
	 * depending on MVS globals in the host worker oracle. */
	Cz80_Set_Reg(&reference_cpu, CZ80_PC, 0x1234u);
	reference_memory[PSP_ME_SOUND_Z80_RAM_OFFSET] = 0xa5u;
	restored_sound_code = 0x11u;
	restored_pending_command = 1u;
	restored_result_code = 0x22u;
	YM2610ContextInit(restored_ym, 8000000, 44100,
		pcm_a, sizeof(pcm_a), pcm_b, sizeof(pcm_b), NULL, NULL, NULL);
	YM2610ContextWrite(restored_ym, 0, 0x08);
	YM2610ContextWrite(restored_ym, 1, 0x01);

	Cz80_Set_State(&reference_cpu, &recovery.state);
	memcpy(reference_memory + PSP_ME_SOUND_Z80_RAM_OFFSET, recovered_ram,
		PSP_ME_SOUND_Z80_RAM_SIZE);
	memcpy(restored_banks, recovery.banks, sizeof(restored_banks));
	restored_sound_code = recovery.sound_code;
	restored_pending_command = recovery.pending_command;
	restored_result_code = recovery.result_code;
	if (!YM2610ContextRestoreFromPcmWindow(restored_ym, recovered_ym))
	{
		fprintf(stderr, "Recovery YM round-trip restore failed\n");
		free(restored_ym_storage);
		free(ym_storage);
		return 0;
	}
	Cz80_Get_State(&reference_cpu, &restored_state);
	if (memcmp(&restored_state, &recovery.state, sizeof(restored_state)) != 0 ||
		memcmp(restored_banks, recovery.banks, sizeof(restored_banks)) != 0 ||
		reference_memory[PSP_ME_SOUND_Z80_RAM_OFFSET] != recovered_ram[0] ||
		restored_sound_code != recovery.sound_code ||
		restored_pending_command != recovery.pending_command ||
		restored_result_code != recovery.result_code ||
		(YM2610ContextWrite(restored_ym, 0, 0x08),
			YM2610ContextRead(restored_ym, 1)) != 0x0fu)
	{
		fprintf(stderr, "Recovery sound-island round-trip diverged\n");
		free(restored_ym_storage);
		free(ym_storage);
		return 0;
	}

	free(restored_ym_storage);
	free(ym_storage);
	return 1;
}

static int test_ym_shadow_timer_irq_and_status(void)
{
	host_dispatch_t host = { 0 };
	psp_me_sound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_sound_worker_t worker;
	psp_me_sound_worker_stats_t stats;
	psp_me_sound_status_snapshot_t status;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	cz80_state_t expected_state;
	const uint32_t banks[4] = { 0x8000u, 0xc000u, 0xe000u, 0xf000u };
	const uint32_t timer_program_cycles = 108u;
	const uint32_t status_read_cycles = 24u;
	uint32_t pc = 0;
	const uint8_t timer_program[] = {
		0x3e, 0x24, 0xd3, 0x04, /* Timer A high register. */
		0x3e, 0xff, 0xd3, 0x05,
		0x3e, 0x25, 0xd3, 0x04, /* Timer A low register. */
		0x3e, 0x03, 0xd3, 0x05,
		0x3e, 0x27, 0xd3, 0x04, /* Load + enable Timer A IRQ. */
		0x3e, 0x05, 0xd3, 0x05,
		0xdb, 0x04,             /* Executed in the second slice. */
		0x32, 0x00, 0xf8,
	};

	memset(reference_memory, 0, sizeof(reference_memory));
	memcpy(reference_memory, timer_program, sizeof(timer_program));
	memset(reference_io, 0, sizeof(reference_io));
	reference_io_count = 0;
	reference_port_read_value = 0;

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

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0, 0, 0,
			44100u, 0x1000u, 0x1000u, false, PSP_ME_SOUND_Z80_MODE_ORACLE,
			TEST_TIMEOUT_US))
	{
		fprintf(stderr, "YM shadow timer snapshot setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	(void)Cz80_Exec(&reference_cpu, (int32_t)timer_program_cycles);
	Cz80_Get_State(&reference_cpu, &expected_state);
	if (reference_io_count != 6u ||
		!psp_me_sound_worker_z80_slice(&worker, reference_io, reference_io_count,
			timer_program_cycles, 100u, &expected_state, banks,
			reference_ram_hash(), true) ||
		!psp_me_sound_worker_ym_timer(&worker, 0u, 101u) ||
		!psp_me_sound_worker_z80_irq(&worker, ASSERT_LINE, 101u))
	{
		fprintf(stderr, "YM shadow timer/IRQ setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	Cz80_Set_IRQ(&reference_cpu, 0, ASSERT_LINE);
	reference_io_count = 0;
	reference_port_read_value = 0x01u;
	(void)Cz80_Exec(&reference_cpu, (int32_t)status_read_cycles);
	Cz80_Get_State(&reference_cpu, &expected_state);
	pc = Cz80_Get_Reg(&reference_cpu, CZ80_PC);
	if (reference_io_count != 1u || reference_memory[0xf800] != 0x01u ||
		!psp_me_sound_worker_z80_slice(&worker, reference_io, reference_io_count,
			status_read_cycles, 102u, &expected_state, banks,
			reference_ram_hash(), true) ||
		!psp_me_sound_worker_sync(&worker, 102u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "YM shadow status validation failed: pc=%u io=%u ram=%u\n",
			pc, reference_io_count, reference_memory[0xf800]);
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	psp_me_sound_worker_get_stats(&worker, &stats);
	if (stats.z80_snapshots != 1u || stats.z80_irqs != 1u ||
		stats.z80_slices != 2u || stats.z80_io_events != 7u ||
		stats.z80_state_mismatches != 0u || stats.z80_ram_mismatches != 0u ||
		stats.z80_bank_mismatches != 0u || stats.z80_io_mismatches != 0u ||
		stats.z80_send_failures != 0u ||
		stats.fatal_error != PSP_ME_SOUND_WORKER_ERROR_NONE)
	{
		fprintf(stderr,
			"YM shadow stats mismatch: snapshots=%u irqs=%u slices=%u io=%u state=%u "
			"ram=%u bank=%u io_mismatch=%u send_fail=%u fatal=%u\n",
			stats.z80_snapshots, stats.z80_irqs, stats.z80_slices,
			stats.z80_io_events, stats.z80_state_mismatches,
			stats.z80_ram_mismatches, stats.z80_bank_mismatches,
			stats.z80_io_mismatches, stats.z80_send_failures, stats.fatal_error);
		return 0;
	}
	return 1;
}

static int test_ym_timer_preemption_boundary(void)
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
	const uint32_t requested_cycles = 200u;
	const uint8_t timer_program[] = {
		0x3e, 0x24, 0xd3, 0x04, /* Timer A high register. */
		0x3e, 0xff, 0xd3, 0x05,
		0x3e, 0x25, 0xd3, 0x04, /* Timer A low register. */
		0x3e, 0x03, 0xd3, 0x05,
		0x3e, 0x27, 0xd3, 0x04, /* Load + enable Timer A IRQ. */
		0x3e, 0x05, 0xd3, 0x05,
		0xdb, 0x04,             /* Must not execute after the preempting OUT. */
		0x32, 0x00, 0xf8,
	};

	memset(reference_memory, 0, sizeof(reference_memory));
	memcpy(reference_memory, timer_program, sizeof(timer_program));
	memset(reference_io, 0, sizeof(reference_io));
	reference_io_count = 0;
	reference_port_read_value = 0;
	reference_preempt_cpu = NULL;
	reference_preempt_on_timer_start = false;

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

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0, 0, 0,
			44100u, 0x1000u, 0x1000u, false, PSP_ME_SOUND_Z80_MODE_ORACLE,
			TEST_TIMEOUT_US))
	{
		fprintf(stderr, "YM preemption snapshot setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	reference_preempt_cpu = &reference_cpu;
	reference_preempt_on_timer_start = true;
	(void)Cz80_Exec(&reference_cpu, (int32_t)requested_cycles);
	reference_preempt_cpu = NULL;
	Cz80_Get_State(&reference_cpu, &expected_state);
	if (reference_preempt_on_timer_start || reference_io_count != 7u ||
		Cz80_Get_Reg(&reference_cpu, CZ80_PC) != 24u ||
		!psp_me_sound_worker_z80_slice(&worker, reference_io, reference_io_count,
			requested_cycles, 100u, &expected_state, banks,
			reference_ram_hash(), true) ||
		!psp_me_sound_worker_sync(&worker, 100u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "YM timer preemption replay failed: pc=%u io=%u pending=%d\n",
			Cz80_Get_Reg(&reference_cpu, CZ80_PC), reference_io_count,
			reference_preempt_on_timer_start ? 1 : 0);
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	psp_me_sound_worker_get_stats(&worker, &stats);
	if (stats.z80_slices != 1u || stats.z80_io_events != 7u ||
		stats.z80_state_mismatches != 0u || stats.z80_ram_mismatches != 0u ||
		stats.z80_bank_mismatches != 0u || stats.z80_io_mismatches != 0u ||
		stats.z80_send_failures != 0u ||
		stats.fatal_error != PSP_ME_SOUND_WORKER_ERROR_NONE)
	{
		fprintf(stderr,
			"YM preemption stats mismatch: slices=%u io=%u state=%u ram=%u bank=%u "
			"io_mismatch=%u send=%u fatal=%u\n",
			stats.z80_slices, stats.z80_io_events, stats.z80_state_mismatches,
			stats.z80_ram_mismatches, stats.z80_bank_mismatches,
			stats.z80_io_mismatches, stats.z80_send_failures, stats.fatal_error);
		return 0;
	}
	return 1;
}

static int test_autonomous_ym_timer_preemption_boundary(void)
{
	host_dispatch_t host = { 0 };
	psp_me_sound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_sound_worker_t worker;
	psp_me_sound_worker_stats_t stats;
	psp_me_sound_status_snapshot_t status;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	cz80_state_t expected_state;
	const uint32_t banks[4] = { 0x8000u, 0xc000u, 0xe000u, 0xf000u };
	const uint32_t requested_cycles = 200u;
	const uint8_t timer_program[] = {
		0x3e, 0x24, 0xd3, 0x04,
		0x3e, 0xff, 0xd3, 0x05,
		0x3e, 0x25, 0xd3, 0x04,
		0x3e, 0x03, 0xd3, 0x05,
		0x3e, 0x27, 0xd3, 0x04,
		0x3e, 0x05, 0xd3, 0x05,
		0xdb, 0x04,
		0x32, 0x00, 0xf8,
	};

	memset(reference_memory, 0, sizeof(reference_memory));
	memcpy(reference_memory, timer_program, sizeof(timer_program));
	memset(reference_io, 0, sizeof(reference_io));
	reference_io_count = 0;
	reference_port_read_value = 0;
	reference_preempt_cpu = NULL;
	reference_preempt_on_timer_start = false;

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

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0, 0, 0,
			44100u, 0x1000u, 0x1000u, false,
			PSP_ME_SOUND_Z80_MODE_AUTONOMOUS, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Autonomous YM preemption snapshot setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	reference_preempt_cpu = &reference_cpu;
	reference_preempt_on_timer_start = true;
	(void)Cz80_Exec(&reference_cpu, (int32_t)requested_cycles);
	reference_preempt_cpu = NULL;
	Cz80_Get_State(&reference_cpu, &expected_state);
	if (reference_preempt_on_timer_start || Cz80_Get_Reg(&reference_cpu, CZ80_PC) != 24u ||
		!psp_me_sound_worker_z80_advance(&worker, requested_cycles, 1000u, 100u) ||
		!psp_me_sound_worker_z80_checkpoint(&worker, &expected_state, banks,
			reference_ram_hash(), 100u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_read_status(&worker, &status) ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr,
			"Autonomous YM timer preemption failed: pc=%u io=%u pending=%d\n",
			Cz80_Get_Reg(&reference_cpu, CZ80_PC), reference_io_count,
			reference_preempt_on_timer_start ? 1 : 0);
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}
	if (status.last_advance_elapsed_us != 24u)
	{
		fprintf(stderr, "Autonomous YM preemption elapsed mismatch: %u\n",
			status.last_advance_elapsed_us);
		return 0;
	}

	psp_me_sound_worker_get_stats(&worker, &stats);
	if (stats.z80_autonomous_slices != 1u || stats.z80_checkpoints != 1u ||
		stats.z80_io_events != 0u || stats.z80_state_mismatches != 0u ||
		stats.z80_ram_mismatches != 0u || stats.z80_bank_mismatches != 0u ||
		stats.z80_io_mismatches != 0u || stats.fatal_error != PSP_ME_SOUND_WORKER_ERROR_NONE)
	{
		fprintf(stderr,
			"Autonomous YM preemption stats mismatch: slices=%u checkpoints=%u io=%u state=%u ram=%u bank=%u io_mismatch=%u fatal=%u\n",
			stats.z80_autonomous_slices, stats.z80_checkpoints,
			stats.z80_io_events, stats.z80_state_mismatches,
			stats.z80_ram_mismatches, stats.z80_bank_mismatches,
			stats.z80_io_mismatches, stats.fatal_error);
		return 0;
	}
	return 1;
}

static int test_autonomous_ym_timer_overflow_schedule(void)
{
	host_dispatch_t host = { 0 };
	psp_me_sound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_sound_worker_t worker;
	psp_me_sound_status_snapshot_t first_status;
	psp_me_sound_status_snapshot_t second_status;
	ym2610_pcm_window_t window;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	const uint32_t banks[4] = { 0x8000u, 0xc000u, 0xe000u, 0xf000u };
	const uint8_t timer_program[] = {
		0x3e, 0x24, 0xd3, 0x04,
		0x3e, 0xff, 0xd3, 0x05,
		0x3e, 0x25, 0xd3, 0x04,
		0x3e, 0x03, 0xd3, 0x05,
		0x3e, 0x27, 0xd3, 0x04,
		0x3e, 0x05, 0xd3, 0x05,
		0x00, 0x00, 0x00, 0x00,
	};

	memset(reference_memory, 0, sizeof(reference_memory));
	memcpy(reference_memory, timer_program, sizeof(timer_program));
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

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0, 0, 0,
			44100u, 0x1000u, 0x1000u, false,
			PSP_ME_SOUND_Z80_MODE_AUTONOMOUS, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_advance(&worker, 200u, 1000u, 24u) ||
		!psp_me_sound_worker_z80_advance(&worker, 72u, 18u, 42u) ||
		!psp_me_sound_worker_ym_render_prepare(&worker, 1u, 42u, &window,
			TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_read_status(&worker, &first_status))
	{
		fprintf(stderr, "Autonomous YM local timer first overflow failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}
	if (first_status.ym_timer_overflows != 1u ||
		first_status.ym_timer_callbacks < 2u ||
		first_status.irq_state != ASSERT_LINE)
	{
		fprintf(stderr,
			"Autonomous YM first timer status mismatch: callbacks=%u overflows=%u irq=%u\n",
			first_status.ym_timer_callbacks, first_status.ym_timer_overflows,
			first_status.irq_state);
		psp_me_sound_worker_abort(&worker);
		return 0;
	}

	if (!psp_me_sound_worker_z80_advance(&worker, 72u, 18u, 60u) ||
		!psp_me_sound_worker_sync(&worker, 60u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_read_status(&worker, &second_status) ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Autonomous YM local timer reload failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}
	if (second_status.ym_timer_overflows != 2u ||
		second_status.ym_timer_callbacks < 3u ||
		second_status.irq_state != ASSERT_LINE)
	{
		fprintf(stderr,
			"Autonomous YM reload status mismatch: callbacks=%u overflows=%u irq=%u\n",
			second_status.ym_timer_callbacks, second_status.ym_timer_overflows,
			second_status.irq_state);
		return 0;
	}
	return 1;
}

static int test_autonomous_advance_horizon_timer_boundary(void)
{
	host_dispatch_t host = { 0 };
	psp_me_sound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_sound_worker_t worker;
	psp_me_sound_status_snapshot_t status;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	const uint32_t banks[4] = { 0x8000u, 0xc000u, 0xe000u, 0xf000u };
	const uint8_t timer_program[] = {
		0x3e, 0x24, 0xd3, 0x04,
		0x3e, 0xff, 0xd3, 0x05,
		0x3e, 0x25, 0xd3, 0x04,
		0x3e, 0x03, 0xd3, 0x05,
		0x3e, 0x27, 0xd3, 0x04,
		0x3e, 0x05, 0xd3, 0x05,
		0x00, 0x00, 0x00, 0x00,
	};
	memset(reference_memory, 0, sizeof(reference_memory));
	memcpy(reference_memory, timer_program, sizeof(timer_program));
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

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0, 0, 0,
			44100u, 0x1000u, 0x1000u, false,
			PSP_ME_SOUND_Z80_MODE_AUTONOMOUS, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_advance_horizon(&worker, 50u, 1000u) ||
		!psp_me_sound_worker_z80_advance_horizon(&worker, 42u, 976u) ||
		!psp_me_sound_worker_fence(&worker, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_read_status(&worker, &status))
	{
		fprintf(stderr, "Autonomous advance-horizon first timer setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}
	if (status.emulated_time != 42u || status.ym_timer_overflows != 1u ||
		status.ym_timer_callbacks < 2u || status.irq_state != ASSERT_LINE)
	{
		fprintf(stderr,
			"Autonomous advance-horizon first timer mismatch: time=%llu callbacks=%u overflows=%u irq=%u\n",
			(unsigned long long)status.emulated_time, status.ym_timer_callbacks,
			status.ym_timer_overflows, status.irq_state);
		psp_me_sound_worker_abort(&worker);
		return 0;
	}

	if (!psp_me_sound_worker_z80_advance_horizon(&worker, 60u, 958u) ||
		!psp_me_sound_worker_fence(&worker, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_read_status(&worker, &status) ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Autonomous advance-horizon reload failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}
	if (status.emulated_time != 60u || status.ym_timer_overflows != 2u ||
		status.ym_timer_callbacks < 3u || status.irq_state != ASSERT_LINE)
	{
		fprintf(stderr,
			"Autonomous advance-horizon reload mismatch: time=%llu callbacks=%u overflows=%u irq=%u\n",
			(unsigned long long)status.emulated_time, status.ym_timer_callbacks,
			status.ym_timer_overflows, status.irq_state);
		return 0;
	}
	return 1;
}

static int test_autonomous_advance_horizon_uses_z80_clock(void)
{
	host_dispatch_t host = { 0 };
	psp_me_sound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_sound_worker_t worker;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	cz80_state_t expected_state;
	const uint32_t banks[4] = { 0x8000u, 0xc000u, 0xe000u, 0xf000u };

	memset(reference_memory, 0, sizeof(reference_memory));
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

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0, 0, 0,
			44100u, 0x1000u, 0x1000u, false,
			PSP_ME_SOUND_Z80_MODE_AUTONOMOUS, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_shadow_sound(&worker, 0x77u, 20u) ||
		!psp_me_sound_worker_z80_advance_horizon(&worker, 40u, 40u))
	{
		fprintf(stderr, "Autonomous horizon Z80 clock setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	Cz80_Set_IRQ(&reference_cpu, IRQ_LINE_NMI, PULSE_LINE);
	(void)Cz80_Exec(&reference_cpu, 40 * 4);
	Cz80_Get_State(&reference_cpu, &expected_state);
	if (!psp_me_sound_worker_z80_checkpoint(&worker, &expected_state, banks,
			reference_ram_hash(), 40u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Autonomous Z80 clock checkpoint failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}
	return 1;
}

static int test_autonomous_advance_horizon_long_timer_preemption(void)
{
	host_dispatch_t host = { 0 };
	psp_me_sound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_sound_worker_t worker;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	cz80_state_t expected_state;
	const uint32_t banks[4] = { 0x8000u, 0xc000u, 0xe000u, 0xf000u };
	const uint8_t timer_program[] = {
		0x3e, 0x24, 0xd3, 0x04,
		0x3e, 0xfc, 0xd3, 0x05, /* TA = 1008 -> 16 * 18 us = 288 us. */
		0x3e, 0x25, 0xd3, 0x04,
		0x3e, 0x00, 0xd3, 0x05,
		0x3e, 0x27, 0xd3, 0x04,
		0x3e, 0x05, 0xd3, 0x05,
		0xdb, 0x04,
		0x32, 0x00, 0xf8,
	};

	memset(reference_memory, 0, sizeof(reference_memory));
	memcpy(reference_memory, timer_program, sizeof(timer_program));
	memset(reference_io, 0, sizeof(reference_io));
	reference_io_count = 0;
	reference_port_read_value = 0;
	reference_preempt_cpu = NULL;
	reference_preempt_on_timer_start = false;

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

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0, 0, 0,
			44100u, 0x1000u, 0x1000u, false,
			PSP_ME_SOUND_Z80_MODE_AUTONOMOUS, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Long-timer advance-horizon snapshot setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}

	reference_preempt_cpu = &reference_cpu;
	reference_preempt_on_timer_start = true;
	(void)Cz80_Exec(&reference_cpu, 200);
	reference_preempt_cpu = NULL;
	Cz80_Get_State(&reference_cpu, &expected_state);
	if (reference_preempt_on_timer_start ||
		!psp_me_sound_worker_z80_advance_horizon(&worker, 50u, 1000u) ||
		!psp_me_sound_worker_z80_checkpoint(&worker, &expected_state, banks,
			reference_ram_hash(), 24u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr,
			"Long-timer advance-horizon preemption mismatch: pc=%u io=%u\n",
			Cz80_Get_Reg(&reference_cpu, CZ80_PC), reference_io_count);
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}
	return 1;
}

static int test_autonomous_ym_timer_b_stop_restart(void)
{
	host_dispatch_t host = { 0 };
	psp_me_sound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_sound_worker_t worker;
	psp_me_sound_status_snapshot_t status;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	const uint32_t banks[4] = { 0x8000u, 0xc000u, 0xe000u, 0xf000u };
	const uint8_t timer_program[] = {
		0x3e, 0x26, 0xd3, 0x04, /* Timer B register. */
		0x3e, 0xff, 0xd3, 0x05,
		0x3e, 0x27, 0xd3, 0x04, /* Load + enable Timer B IRQ. */
		0x3e, 0x0a, 0xd3, 0x05,
		0x3e, 0x27, 0xd3, 0x04, /* Stop Timer B before expiry. */
		0x3e, 0x00, 0xd3, 0x05,
		0x3e, 0x27, 0xd3, 0x04, /* Restart Timer B. */
		0x3e, 0x0a, 0xd3, 0x05,
		0x00, 0x00, 0x00, 0x00,
	};

	memset(reference_memory, 0, sizeof(reference_memory));
	memcpy(reference_memory, timer_program, sizeof(timer_program));
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

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0, 0, 0,
			44100u, 0x1000u, 0x1000u, false,
			PSP_ME_SOUND_Z80_MODE_AUTONOMOUS, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_advance(&worker, 200u, 1000u, 16u) ||
		!psp_me_sound_worker_z80_advance(&worker, 200u, 984u, 32u) ||
		!psp_me_sound_worker_z80_advance(&worker, 1152u, 288u, 320u) ||
		!psp_me_sound_worker_sync(&worker, 320u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_read_status(&worker, &status) ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Autonomous YM Timer B stop/restart failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		return 0;
	}
	if (status.ym_timer_overflows != 1u || status.ym_timer_callbacks < 4u ||
		status.irq_state != ASSERT_LINE)
	{
		fprintf(stderr,
			"Autonomous YM Timer B status mismatch: callbacks=%u overflows=%u irq=%u\n",
			status.ym_timer_callbacks, status.ym_timer_overflows, status.irq_state);
		return 0;
	}
	return 1;
}

static int test_ym_shadow_pcm_render(void)
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
	static uint8_t pcm_a[0x1000];
	static uint8_t pcm_b[0x1000];
	ym2610_pcm_window_t window;
	int32_t left[128], right[128];
	int32_t presented_left[128], presented_right[128];
	int32_t rejected_left[128], rejected_right[128];
	int32_t *buffers[2] = { left, right };
	void *ym_storage = NULL;
	ym2610_context_t *reference_ym = alloc_ym_context(&ym_storage);
	const uint32_t cycles = 260u;
	uint32_t i;
	uint32_t pc = 0;
	const uint8_t program[] = {
		0x3e, 0x01, 0xd3, 0x06, 0x3e, 0x3f, 0xd3, 0x07,
		0x3e, 0x08, 0xd3, 0x06, 0x3e, 0xdf, 0xd3, 0x07,
		0x3e, 0x10, 0xd3, 0x06, 0x3e, 0x00, 0xd3, 0x07,
		0x3e, 0x18, 0xd3, 0x06, 0x3e, 0x00, 0xd3, 0x07,
		0x3e, 0x20, 0xd3, 0x06, 0x3e, 0x00, 0xd3, 0x07,
		0x3e, 0x28, 0xd3, 0x06, 0x3e, 0x00, 0xd3, 0x07,
		0x3e, 0x00, 0xd3, 0x06, 0x3e, 0x01, 0xd3, 0x07,
		0x76,
	};

	if (!reference_ym)
	{
		fprintf(stderr, "YM render reference context allocation failed\n");
		return 0;
	}
	for (i = 0; i < sizeof(pcm_a); i++)
		pcm_a[i] = (uint8_t)(i * 37u + 11u);
	for (i = 0; i < sizeof(pcm_b); i++)
		pcm_b[i] = (uint8_t)(i * 19u + 7u);

	memset(reference_memory, 0, sizeof(reference_memory));
	memcpy(reference_memory, program, sizeof(program));
	memset(reference_io, 0, sizeof(reference_io));
	reference_io_count = 0;
	reference_port_read_value = 0;
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

	YM2610ContextInit(reference_ym, 8000000, 44100, pcm_a, sizeof(pcm_a),
		pcm_b, sizeof(pcm_b), NULL, NULL, NULL);
	configure_adpcma_zero(reference_ym);

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0, 0, 0,
			44100u, sizeof(pcm_a), sizeof(pcm_b), false,
			PSP_ME_SOUND_Z80_MODE_ORACLE, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "YM PCM shadow snapshot setup failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		free(ym_storage);
		return 0;
	}

	(void)Cz80_Exec(&reference_cpu, (int32_t)cycles);
	Cz80_Get_State(&reference_cpu, &expected_state);
	pc = Cz80_Get_Reg(&reference_cpu, CZ80_PC);
	if (reference_io_count != 14u ||
		!psp_me_sound_worker_z80_slice(&worker, reference_io, reference_io_count,
			cycles, 200u, &expected_state, banks, reference_ram_hash(), true))
	{
		fprintf(stderr, "YM PCM setup slice failed: pc=%u io=%u\n", pc,
			reference_io_count);
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		free(ym_storage);
		return 0;
	}

	if (!psp_me_sound_worker_ym_render_prepare(&worker, 128u, 200u, &window,
			TEST_TIMEOUT_US))
	{
		fprintf(stderr, "YM PCM render prepare failed\n");
		psp_me_sound_worker_abort(&worker);
		free(ym_storage);
		return 0;
	}
	for (i = 0; i < YM2610_PCM_WINDOW_ADPCMA_CHANNELS; i++)
	{
		if (window.adpcma[i].size != 0)
			memcpy(window.adpcma[i].data, pcm_a + window.adpcma[i].base_byte,
				window.adpcma[i].size);
	}
	for (i = 0; i < window.adpcmb_segment_count; i++)
	{
		if (window.adpcmb[i].size != 0)
			memcpy(window.adpcmb[i].data, pcm_b + window.adpcmb[i].base_byte,
				window.adpcmb[i].size);
	}
	if (!psp_me_sound_worker_ym_render_begin(&worker, &window, 200u,
			TEST_TIMEOUT_US))
	{
		fprintf(stderr, "YM PCM render submission failed\n");
		psp_me_sound_worker_abort(&worker);
		free(ym_storage);
		return 0;
	}
	YM2610ContextUpdate(reference_ym, buffers, 128);
	memset(presented_left, 0x5a, sizeof(presented_left));
	memset(presented_right, 0x5a, sizeof(presented_right));
	if (!psp_me_sound_worker_ym_render_finish_present(&worker, left, right,
			presented_left, presented_right, 128,
			YM2610ContextRead(reference_ym, 2), TEST_TIMEOUT_US) ||
		memcmp(presented_left, left, sizeof(left)) != 0 ||
		memcmp(presented_right, right, sizeof(right)) != 0)
	{
		fprintf(stderr, "YM PCM validated presentation failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		free(ym_storage);
		return 0;
	}

	if (!psp_me_sound_worker_ym_render_prepare(&worker, 128u, 201u, &window,
			TEST_TIMEOUT_US))
	{
		fprintf(stderr, "YM PCM mismatch render prepare failed\n");
		psp_me_sound_worker_abort(&worker);
		free(ym_storage);
		return 0;
	}
	for (i = 0; i < YM2610_PCM_WINDOW_ADPCMA_CHANNELS; i++)
	{
		if (window.adpcma[i].size != 0)
			memcpy(window.adpcma[i].data, pcm_a + window.adpcma[i].base_byte,
				window.adpcma[i].size);
	}
	for (i = 0; i < window.adpcmb_segment_count; i++)
	{
		if (window.adpcmb[i].size != 0)
			memcpy(window.adpcmb[i].data, pcm_b + window.adpcmb[i].base_byte,
				window.adpcmb[i].size);
	}
	if (!psp_me_sound_worker_ym_render_begin(&worker, &window, 201u,
			TEST_TIMEOUT_US))
	{
		fprintf(stderr, "YM PCM mismatch render submission failed\n");
		psp_me_sound_worker_abort(&worker);
		free(ym_storage);
		return 0;
	}
	YM2610ContextUpdate(reference_ym, buffers, 128);
	left[0] ^= 1;
	memset(rejected_left, 0x33, sizeof(rejected_left));
	memset(rejected_right, 0x44, sizeof(rejected_right));
	memcpy(presented_left, rejected_left, sizeof(rejected_left));
	memcpy(presented_right, rejected_right, sizeof(rejected_right));
	if (psp_me_sound_worker_ym_render_finish_present(&worker, left, right,
			presented_left, presented_right, 128,
			YM2610ContextRead(reference_ym, 2), TEST_TIMEOUT_US) ||
		memcmp(presented_left, rejected_left, sizeof(rejected_left)) != 0 ||
		memcmp(presented_right, rejected_right, sizeof(rejected_right)) != 0 ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "YM PCM mismatch fail-closed presentation failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		free(ym_storage);
		return 0;
	}

	psp_me_sound_worker_get_stats(&worker, &stats);
	free(ym_storage);
	if (stats.ym_renders != 2u || stats.ym_render_samples != 256u ||
		stats.ym_render_errors != 0u || stats.ym_presented_renders != 1u ||
		stats.ym_presented_samples != 128u || stats.ym_pcm_mismatches != 1u ||
		stats.ym_status_mismatches != 0u || stats.ym_send_failures != 0u ||
		stats.fatal_error != PSP_ME_SOUND_WORKER_ERROR_NONE)
	{
		fprintf(stderr,
			"YM PCM stats mismatch: renders=%u samples=%u errors=%u presented=%u/%u pcm=%u status=%u send=%u fatal=%u\n",
			stats.ym_renders, stats.ym_render_samples, stats.ym_render_errors,
			stats.ym_presented_renders, stats.ym_presented_samples,
			stats.ym_pcm_mismatches, stats.ym_status_mismatches,
			stats.ym_send_failures, stats.fatal_error);
		return 0;
	}
	return 1;
}

static int test_ym_authoritative_render_and_cpu_fallback(void)
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
	const uint32_t banks[4] = { 0x8000u, 0xc000u, 0xe000u, 0xf000u };
	static uint8_t pcm_a[0x1000];
	static uint8_t pcm_b[0x1000];
	ym2610_pcm_window_t window;
	int32_t me_left[128], me_right[128];
	int32_t fallback_left[128], fallback_right[128];
	int32_t reference_left[128], reference_right[128];
	int32_t *fallback_buffers[2] = { fallback_left, fallback_right };
	int32_t *reference_buffers[2] = { reference_left, reference_right };
	void *reference_storage = NULL;
	ym2610_context_t *reference_ym = alloc_ym_context(&reference_storage);
	int old_samplerate = option_samplerate;
	uint32_t i;
	int ok = 0;

	if (!reference_ym)
		return 0;
	option_samplerate = 2;
	for (i = 0; i < sizeof(pcm_a); i++)
		pcm_a[i] = (uint8_t)(i * 29u + 3u);
	for (i = 0; i < sizeof(pcm_b); i++)
		pcm_b[i] = (uint8_t)(i * 17u + 9u);
	YM2610Init(8000000, pcm_a, sizeof(pcm_a), pcm_b, sizeof(pcm_b), NULL, NULL);
	YM2610ContextInit(reference_ym, 8000000, 44100, pcm_a, sizeof(pcm_a),
		pcm_b, sizeof(pcm_b), NULL, NULL, NULL);
	configure_default_adpcma_zero();
	configure_adpcma_zero(reference_ym);

	memset(reference_memory, 0, sizeof(reference_memory));
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

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, &dispatch, 64u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot(&worker, &initial_state, reference_memory,
			reference_memory, sizeof(reference_memory), banks, 0, 0, 0,
			44100u, sizeof(pcm_a), sizeof(pcm_b), true,
			PSP_ME_SOUND_Z80_MODE_AUTONOMOUS, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_ym_render_prepare(&worker, 128u, 100u, &window,
			TEST_TIMEOUT_US) || !YM2610DefaultFillPcmWindow(&window) ||
		!psp_me_sound_worker_ym_render_begin(&worker, &window, 100u,
			TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Authoritative YM first render setup failed\n");
		goto done;
	}
	YM2610ContextUpdate(reference_ym, reference_buffers, 128);
	memset(me_left, 0x55, sizeof(me_left));
	memset(me_right, 0x66, sizeof(me_right));
	if (!psp_me_sound_worker_ym_render_finish_authoritative(&worker,
			me_left, me_right, 128u, TEST_TIMEOUT_US) ||
		memcmp(me_left, reference_left, sizeof(me_left)) != 0 ||
		memcmp(me_right, reference_right, sizeof(me_right)) != 0)
	{
		fprintf(stderr, "Authoritative YM presentation/context sync failed\n");
		goto done;
	}

	/* Start a second ME render, but deliberately ask finish for the wrong sample
	 * count. The worker must consume the ACK without synchronizing CPU YM or
	 * touching the output, after which CPU rendering of that same block must
	 * still match the independent reference exactly. */
	if (!psp_me_sound_worker_ym_render_prepare(&worker, 128u, 200u, &window,
			TEST_TIMEOUT_US) || !YM2610DefaultFillPcmWindow(&window) ||
		!psp_me_sound_worker_ym_render_begin(&worker, &window, 200u,
			TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Authoritative YM fallback render setup failed\n");
		goto done;
	}
	memset(me_left, 0x33, sizeof(me_left));
	memset(me_right, 0x44, sizeof(me_right));
	if (psp_me_sound_worker_ym_render_finish_authoritative(&worker,
			me_left, me_right, 127u, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Authoritative YM invalid metadata unexpectedly passed\n");
		goto done;
	}
	for (i = 0; i < 128u; i++)
	{
		if (me_left[i] != (int32_t)0x33333333 ||
			me_right[i] != (int32_t)0x44444444)
		{
			fprintf(stderr, "Authoritative YM failure mutated output\n");
			goto done;
		}
	}
	YM2610Update(fallback_buffers, 128);
	YM2610ContextUpdate(reference_ym, reference_buffers, 128);
	if (memcmp(fallback_left, reference_left, sizeof(fallback_left)) != 0 ||
		memcmp(fallback_right, reference_right, sizeof(fallback_right)) != 0 ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "Authoritative YM CPU fallback diverged\n");
		goto done;
	}
	psp_me_sound_worker_get_stats(&worker, &stats);
	if (stats.ym_renders != 2u || stats.ym_render_samples != 256u ||
		stats.ym_presented_renders != 1u || stats.ym_presented_samples != 128u ||
		stats.ym_authoritative_renders != 1u ||
		stats.ym_context_sync_failures != 0u || stats.ym_send_failures != 1u ||
		stats.fatal_error != PSP_ME_SOUND_WORKER_ERROR_NONE)
	{
		fprintf(stderr,
			"Authoritative YM stats mismatch: renders=%u samples=%u presented=%u/%u authoritative=%u sync_fail=%u send=%u fatal=%u\n",
			stats.ym_renders, stats.ym_render_samples, stats.ym_presented_renders,
			stats.ym_presented_samples, stats.ym_authoritative_renders,
			stats.ym_context_sync_failures, stats.ym_send_failures,
			stats.fatal_error);
		goto done;
	}
	ok = 1;

done:
	if (worker.running)
		psp_me_sound_worker_abort(&worker);
	option_samplerate = old_samplerate;
	free(reference_storage);
	return ok;
}

int main(void)
{
	if (!test_shadow_order_reset_and_sync() || !test_time_regression_is_fatal() ||
		!test_z80_shadow_slice_matches_reference() || !test_z80_shadow_large_io_trace() ||
		!test_sound_status_snapshot() || !test_sound_status_fence_ordering() ||
		!test_sound_recovery_snapshot() ||
		!test_ym_shadow_timer_irq_and_status() || !test_ym_timer_preemption_boundary() ||
		!test_autonomous_ym_timer_preemption_boundary() ||
		!test_autonomous_ym_timer_overflow_schedule() ||
		!test_autonomous_advance_horizon_timer_boundary() ||
		!test_autonomous_advance_horizon_uses_z80_clock() ||
		!test_autonomous_advance_horizon_long_timer_preemption() ||
		!test_autonomous_ym_timer_b_stop_restart() ||
		!test_ym_shadow_pcm_render() ||
		!test_ym_authoritative_render_and_cpu_fallback())
		return 1;

	printf("PSP ME sound worker host oracle: C3/C4/C5 plus C6 autonomous scheduling/timers passed\n");
	return 0;
}
