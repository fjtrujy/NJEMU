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

int option_samplerate;
static struct sound_t test_sound;
struct sound_t *sound = &test_sound;

float timer_get_time(void)
{
	return 0.0f;
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

static int test_ncdz_flat_worker(void)
{
	host_dispatch_t host = { 0 };
	psp_me_sound_worker_dispatch_t dispatch = {
		host_dispatch_start,
		host_dispatch_wait,
		&host,
	};
	psp_me_sound_worker_t worker = { 0 };
	psp_me_sound_recovery_snapshot_t recovery;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	uint32_t banks[4] = { 0, 0, 0, 0 };
	uint8_t timers_enabled[2] = { 0, 0 };
	uint64_t timers_remaining[2] = { 0, 0 };
	uint8_t *memory = calloc(1, PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE);
	uint8_t *recovered = malloc(PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE);
	uint8_t *pcm = calloc(1, 0x20000u);
	uint8_t bytes[2];
	int32_t left[128];
	int32_t right[128];
	int ok = 0;

	if (!memory || !recovered || !pcm)
		goto done;
	option_samplerate = 2;
	memory[0x2000] = 0x12;
	memory[0x2001] = 0x34;
	memory[0x4321] = 0xa5;
	Cz80_Init(&reference_cpu);
	Cz80_Set_Fetch(&reference_cpu, 0x0000u, 0xffffu, (uintptr_t)memory);
	Cz80_Set_ReadBase(&reference_cpu, (uintptr_t)memory);
	Cz80_Reset(&reference_cpu);
	Cz80_Get_State(&reference_cpu, &initial_state);
	YM2610Init(8000000, pcm, 0x20000, NULL, NULL);

	if (!psp_me_sound_worker_start(&worker, &dispatch, 16u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, 1u, TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_snapshot_profiled_with_timers(&worker,
			&initial_state, memory, memory, PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE,
			banks, 0, 0, 0, 44100u, pcm, 0x20000u, 0u, timers_enabled,
			timers_remaining, true, PSP_ME_SOUND_Z80_MODE_AUTONOMOUS,
			PSP_ME_SOUND_MACHINE_PROFILE_NCDZ, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "NCDZ worker snapshot setup failed\n");
		goto done;
	}
	if (worker.machine.memory_mode != PSP_ME_SOUND_Z80_MEMORY_FLAT_64K ||
		worker.machine.z80_cycles_per_usec != 6u ||
		worker.machine.ym_irq_line != 1u ||
		worker.machine.ym_pcm_mode != PSP_ME_SOUND_YM_PCM_DIRECT)
	{
		fprintf(stderr, "NCDZ worker machine profile was not retained\n");
		goto done;
	}

	if (!psp_me_sound_worker_z80_memory_read(&worker, 0x2000u, bytes,
			sizeof(bytes), TEST_TIMEOUT_US) || bytes[0] != 0x12 || bytes[1] != 0x34 ||
		!psp_me_sound_worker_z80_memory_write_byte(&worker, 0x2000u, 0x56u) ||
		!psp_me_sound_worker_z80_memory_read_clear(&worker, 0x2000u, bytes, 1u,
			TEST_TIMEOUT_US) || bytes[0] != 0x56 ||
		!psp_me_sound_worker_z80_memory_read(&worker, 0x2000u, bytes, 1u,
			TEST_TIMEOUT_US) || bytes[0] != 0)
	{
		fprintf(stderr, "NCDZ worker flat-memory mailbox operations failed\n");
		goto done;
	}

	if (!psp_me_sound_worker_ym_render_begin_direct(&worker, 128u, 100u,
			TEST_TIMEOUT_US) ||
		!psp_me_sound_worker_ym_render_finish_authoritative(&worker, left, right,
			128u, false, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "NCDZ worker direct YM render failed\n");
		goto done;
	}

	if (!psp_me_sound_worker_read_recovery_memory(&worker, &recovery, recovered,
			PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE, NULL, TEST_TIMEOUT_US) ||
		recovery.mode != PSP_ME_SOUND_Z80_MODE_AUTONOMOUS ||
		recovered[0x2000] != 0 || recovered[0x2001] != 0x34 ||
		recovered[0x4321] != 0xa5)
	{
		fprintf(stderr, "NCDZ worker full-memory recovery snapshot failed\n");
		goto done;
	}
	if (!psp_me_sound_worker_shutdown(&worker, TEST_TIMEOUT_US))
	{
		fprintf(stderr, "NCDZ worker shutdown failed\n");
		goto done;
	}
	ok = 1;

done:
	if (worker.running)
		psp_me_sound_worker_abort(&worker);
	free(pcm);
	free(recovered);
	free(memory);
	return ok;
}

int main(void)
{
	if (!test_ncdz_flat_worker())
		return 1;
	printf("PSP ME NCDZ sound worker host test passed\n");
	return 0;
}
