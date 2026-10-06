#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "common/sound.h"
#include "psp/psp_me_qsound_worker.h"
#include "sound/qsound.h"

#define TEST_TIMEOUT_US 2000000ULL
#define TEST_Z80_ROM_SIZE 0x20000u
#define TEST_SAMPLE_ROM_SIZE 0x20000u

uint8_t *memory_region_sound1;
uint32_t memory_length_sound1;
static struct sound_t test_sound;
struct sound_t *sound = &test_sound;

const char *capcom_driver_name(void)
{
	return "ssf2";
}

uint64_t cps2_timer_sound_time_us(void)
{
	return 0;
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

static int test_cps2_qsound_worker(void)
{
	host_dispatch_t host = { 0 };
	psp_me_qsound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_qsound_worker_t worker = { 0 };
	psp_me_qsound_worker_stats_t stats;
	psp_me_qsound_recovery_snapshot_t recovery;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	qsound_context_t recovered_qsound;
	uint8_t *memory = calloc(1, PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE);
	uint8_t *source = calloc(1, TEST_Z80_ROM_SIZE);
	uint8_t *recovered = malloc(PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE);
	uint8_t shared_value = 0x77;
	uint8_t shared_result = 0;
	uint8_t reset_value = 0x66;
	uint8_t reset_result = 0xff;
	uint8_t memory_value = 0x5a;
	uint8_t memory_result = 0;
	int32_t left[128];
	int32_t right[128];
	int ok = 0;
	static const uint8_t program[] = {
		0x3e, 0x01,             /* LD A,1 */
		0x32, 0x03, 0xd0,       /* LD (d003),A: switch bank */
		0x32, 0x03, 0xd0,       /* same bank: must not copy again */
		0x3a, 0x24, 0xc1,       /* LD A,(c124) */
		0x32, 0x25, 0xc1,       /* LD (c125),A */
		0x76                    /* HALT */
	};

	memory_region_sound1 = calloc(1, TEST_SAMPLE_ROM_SIZE);
	memory_length_sound1 = TEST_SAMPLE_ROM_SIZE;
	if (!memory || !source || !recovered || !memory_region_sound1)
		goto done;

	memcpy(memory, program, sizeof(program));
	memcpy(source, program, sizeof(program));
	memcpy(source + 0x10000u, memory + 0x8000u, 0x4000u);
	memcpy(source + 0x14000u, memory + 0x8000u, 0x4000u);

	qsound_sh_start();
	qsound_sh_reset();
	Cz80_Init(&reference_cpu);
	Cz80_Set_Fetch(&reference_cpu, 0x0000u, 0xffffu, (uintptr_t)memory);
	Cz80_Set_ReadBase(&reference_cpu, (uintptr_t)memory);
	Cz80_Reset(&reference_cpu);
	Cz80_Get_State(&reference_cpu, &initial_state);

	if (!psp_me_qsound_worker_start(&worker, &dispatch, 16u, TEST_TIMEOUT_US) ||
		!psp_me_qsound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_qsound_worker_snapshot(&worker, &initial_state, memory, source,
			TEST_Z80_ROM_SIZE, TEST_SAMPLE_ROM_SIZE, 0x10000u, false,
			TEST_TIMEOUT_US))
	{
		fprintf(stderr, "CPS2 QSound worker snapshot setup failed\n");
		goto done;
	}

	if (!psp_me_qsound_worker_shared_ram_write(&worker, 0xc124u,
			&shared_value, 1u) ||
		!psp_me_qsound_worker_advance(&worker, 256u, 32u) ||
		!psp_me_qsound_worker_sync(&worker, TEST_TIMEOUT_US) ||
		!psp_me_qsound_worker_shared_ram_read(&worker, 0xc125u,
			&shared_result, 1u) || shared_result != shared_value)
	{
		fprintf(stderr, "CPS2 QSound worker shared RAM synchronization failed\n");
		goto done;
	}

	psp_me_qsound_worker_get_stats(&worker, &stats);
	if (stats.advances != 1u || stats.bank_switches != 1u ||
		stats.command_overflow != 0u || stats.event_overflow != 0u)
	{
		fprintf(stderr,
			"CPS2 QSound worker bank guard/stats mismatch: advances=%u banks=%u cmd_overflow=%u event_overflow=%u\n",
			stats.advances, stats.bank_switches, stats.command_overflow,
			stats.event_overflow);
		goto done;
	}

	if (!psp_me_qsound_worker_z80_reset(&worker, true, 32u) ||
		!psp_me_qsound_worker_shared_ram_write(&worker, 0xc124u,
			&reset_value, 1u) ||
		!psp_me_qsound_worker_shared_ram_write(&worker, 0xc125u,
			&memory_result, 1u) ||
		!psp_me_qsound_worker_advance(&worker, 256u, 64u) ||
		!psp_me_qsound_worker_sync(&worker, TEST_TIMEOUT_US) ||
		!psp_me_qsound_worker_shared_ram_read(&worker, 0xc125u,
			&reset_result, 1u) || reset_result != 0 ||
		!psp_me_qsound_worker_z80_reset(&worker, false, 64u) ||
		!psp_me_qsound_worker_advance(&worker, 256u, 96u) ||
		!psp_me_qsound_worker_sync(&worker, TEST_TIMEOUT_US) ||
		!psp_me_qsound_worker_shared_ram_read(&worker, 0xc125u,
			&reset_result, 1u) || reset_result != reset_value)
	{
		fprintf(stderr, "CPS2 QSound worker reset/suspend semantics failed\n");
		goto done;
	}

	if (!psp_me_qsound_worker_memory_write_byte(&worker, 0xf123u, memory_value) ||
		!psp_me_qsound_worker_memory_read(&worker, 0xf123u, &memory_result, 1u,
			TEST_TIMEOUT_US) || memory_result != memory_value)
	{
		fprintf(stderr, "CPS2 QSound worker ordered memory access failed\n");
		goto done;
	}

	psp_me_qsound_worker_get_stats(&worker, &stats);
	if (stats.advances != 3u || stats.z80_resets != 2u ||
		stats.memory_reads != 1u || stats.memory_writes != 1u ||
		stats.bank_switches != 1u)
	{
		fprintf(stderr,
			"CPS2 QSound worker reset/memory stats mismatch: advances=%u resets=%u reads=%u writes=%u banks=%u\n",
			stats.advances, stats.z80_resets, stats.memory_reads,
			stats.memory_writes, stats.bank_switches);
		goto done;
	}

	if (!psp_me_qsound_worker_render_begin(&worker, 128u, 96u) ||
		!psp_me_qsound_worker_render_finish(&worker, left, right, 128u,
			TEST_TIMEOUT_US))
	{
		fprintf(stderr, "CPS2 QSound worker render failed\n");
		goto done;
	}

	if (!psp_me_qsound_worker_recover(&worker, &recovery, recovered,
			&recovered_qsound, TEST_TIMEOUT_US) ||
		recovery.emulated_time != 96u || recovered[0xc125u] != reset_value ||
		recovered[0xf123u] != memory_value || recovery.suspended != 0)
	{
		fprintf(stderr, "CPS2 QSound worker recovery snapshot failed\n");
		goto done;
	}

	if (!psp_me_qsound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "CPS2 QSound worker shutdown failed\n");
		goto done;
	}
	ok = 1;

done:
	if (worker.running)
		psp_me_qsound_worker_abort(&worker);
	free(memory_region_sound1);
	memory_region_sound1 = NULL;
	memory_length_sound1 = 0;
	free(recovered);
	free(source);
	free(memory);
	return ok;
}

static int test_fatal_publishes_recovery(void)
{
	host_dispatch_t host = { 0 };
	psp_me_qsound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_qsound_worker_t worker = { 0 };
	psp_me_qsound_worker_stats_t stats;
	psp_me_qsound_recovery_snapshot_t recovery;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	qsound_context_t recovered_qsound;
	uint8_t *memory = calloc(1, PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE);
	uint8_t *source = calloc(1, TEST_Z80_ROM_SIZE);
	uint8_t *recovered = malloc(PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE);
	uint8_t shared_value = 0x4c;
	int ok = 0;
	static const uint8_t program[] = {
		0x3a, 0x24, 0xc1,       /* LD A,(c124) */
		0x32, 0x25, 0xc1,       /* LD (c125),A */
		0x76                    /* HALT */
	};

	memory_region_sound1 = calloc(1, TEST_SAMPLE_ROM_SIZE);
	memory_length_sound1 = TEST_SAMPLE_ROM_SIZE;
	if (!memory || !source || !recovered || !memory_region_sound1)
		goto done;
	memcpy(memory, program, sizeof(program));
	memcpy(source, program, sizeof(program));
	memcpy(source + 0x10000u, memory + 0x8000u, 0x4000u);

	qsound_sh_start();
	qsound_sh_reset();
	Cz80_Init(&reference_cpu);
	Cz80_Set_Fetch(&reference_cpu, 0x0000u, 0xffffu, (uintptr_t)memory);
	Cz80_Set_ReadBase(&reference_cpu, (uintptr_t)memory);
	Cz80_Reset(&reference_cpu);
	Cz80_Get_State(&reference_cpu, &initial_state);

	if (!psp_me_qsound_worker_start(&worker, &dispatch, 16u, TEST_TIMEOUT_US) ||
		!psp_me_qsound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_qsound_worker_snapshot(&worker, &initial_state, memory, source,
			TEST_Z80_ROM_SIZE, TEST_SAMPLE_ROM_SIZE, 0x10000u, false,
			TEST_TIMEOUT_US) ||
		!psp_me_qsound_worker_shared_ram_write(&worker, 0xc124u,
			&shared_value, 1u) ||
		!psp_me_qsound_worker_advance(&worker, 128u, 32u) ||
		!psp_me_qsound_worker_sync(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "CPS2 QSound fatal recovery setup failed\n");
		goto done;
	}

	/* A timestamp regression is a deterministic fatal-worker injection.  The ME
	 * must publish its last valid sound-island snapshot before terminating so
	 * Allegrex can recover without sending another command to the dead worker. */
	if (!psp_me_qsound_worker_advance(&worker, 1u, 31u))
	{
		fprintf(stderr, "CPS2 QSound fatal injection was not queued\n");
		goto done;
	}
	if (psp_me_qsound_worker_sync(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "CPS2 QSound fatal injection unexpectedly synchronized\n");
		goto done;
	}
	psp_me_qsound_worker_get_stats(&worker, &stats);
	if (stats.fatal_error == 0 ||
		!psp_me_qsound_worker_read_published_recovery(&worker, &recovery,
			recovered, &recovered_qsound) ||
		recovery.emulated_time != 32u || recovered[0xc125u] != shared_value)
	{
		fprintf(stderr,
			"CPS2 QSound fatal recovery snapshot mismatch: fatal=%u time=%llu value=%u\n",
			stats.fatal_error, (unsigned long long)recovery.emulated_time,
			(unsigned)recovered[0xc125u]);
		goto done;
	}
	ok = 1;

done:
	if (worker.running)
		psp_me_qsound_worker_abort(&worker);
	free(memory_region_sound1);
	memory_region_sound1 = NULL;
	memory_length_sound1 = 0;
	free(recovered);
	free(source);
	free(memory);
	return ok;
}

int main(void)
{
	if (!test_cps2_qsound_worker() || !test_fatal_publishes_recovery())
		return 1;
	printf("PSP ME CPS2 QSound worker host test passed\n");
	return 0;
}
