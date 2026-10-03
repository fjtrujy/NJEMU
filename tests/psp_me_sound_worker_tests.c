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
struct sound_t *sound;

float timer_get_time(void)
{
	return 0.0f;
}

static uint8_t reference_memory[0x20000];
static psp_me_sound_z80_io_t reference_io[PSP_ME_SOUND_Z80_IO_CAPACITY];
static uint32_t reference_io_count;
static uint8_t reference_port_read_value;

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
			44100u, 0x1000u, 0x1000u, false, TEST_TIMEOUT_US))
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
			44100u, 0x1000u, 0x1000u, false, TEST_TIMEOUT_US))
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
			44100u, sizeof(pcm_a), sizeof(pcm_b), false, TEST_TIMEOUT_US))
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
	if (!psp_me_sound_worker_ym_render_finish(&worker, left, right, 128,
			YM2610ContextRead(reference_ym, 2), TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "YM PCM render comparison failed\n");
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		free(ym_storage);
		return 0;
	}

	psp_me_sound_worker_get_stats(&worker, &stats);
	free(ym_storage);
	if (stats.ym_renders != 1u || stats.ym_render_samples != 128u ||
		stats.ym_render_errors != 0u || stats.ym_pcm_mismatches != 0u ||
		stats.ym_status_mismatches != 0u || stats.ym_send_failures != 0u ||
		stats.fatal_error != PSP_ME_SOUND_WORKER_ERROR_NONE)
	{
		fprintf(stderr,
			"YM PCM stats mismatch: renders=%u samples=%u errors=%u pcm=%u status=%u send=%u fatal=%u\n",
			stats.ym_renders, stats.ym_render_samples, stats.ym_render_errors,
			stats.ym_pcm_mismatches, stats.ym_status_mismatches,
			stats.ym_send_failures, stats.fatal_error);
		return 0;
	}
	return 1;
}

int main(void)
{
	if (!test_shadow_order_reset_and_sync() || !test_time_regression_is_fatal() ||
		!test_z80_shadow_slice_matches_reference() ||
		!test_ym_shadow_timer_irq_and_status() || !test_ym_shadow_pcm_render())
		return 1;

	printf("PSP ME sound worker host oracle: C3/C4 plus C5 YM timer/status/PCM passed\n");
	return 0;
}
