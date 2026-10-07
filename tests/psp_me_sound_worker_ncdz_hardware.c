#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pspkernel.h>
#include <pspthreadman.h>
#include <me-safe-task/me-stask-mist.h>
#include <me-core-mapper/me-core-mapper.h>
#include <me-core-mapper/hw-registers.h>

#include "common/audio_producer_driver.h"
#include "common/audio_profile.h"
#include "psp/psp_me_sound_worker.h"

PSP_MODULE_INFO("NJEMU ME NCDZ Worker", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define PSP_ME_NCDZ_HW_SYSCALL_INDEX 13
#define PSP_ME_NCDZ_HW_TIMEOUT_US 2000000ULL
#define PSP_ME_NCDZ_HW_PCM_A_SIZE 0x100000u
#define PSP_ME_NCDZ_HW_LOG_PATH "host0:/njemu_me_ncdz_worker_hw.log"

static uint8_t ncdz_memory[PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE]
	__attribute__((aligned(64)));
static uint8_t ncdz_recovered_memory[PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE]
	__attribute__((aligned(64)));
static uint8_t ncdz_pcm_a[PSP_ME_NCDZ_HW_PCM_A_SIZE]
	__attribute__((aligned(64)));

int pcm_cache_enable;

static bool hardware_no_jobs(void)
{
	return false;
}

static const audio_producer_driver_t hardware_audio_producer = {
	.ident = "hardware-test",
	.canRunJobs = hardware_no_jobs,
};

const audio_producer_driver_t *const audio_producer_driver =
	&hardware_audio_producer;

float timer_get_time(void)
{
	return 0.0f;
}

uint8_t *pcm_cache_read(uint16_t block)
{
	(void)block;
	return NULL;
}

void ym2610_adpcma_job_run(void *data)
{
	(void)data;
}

#ifdef PSP_AUDIO_PROFILE
uint64_t audio_profile_now_us(void)
{
	return sceKernelGetSystemTimeWide();
}

void audio_profile_add(audio_profile_metric_t metric, uint64_t elapsed_us)
{
	(void)metric;
	(void)elapsed_us;
}
#endif

typedef struct psp_me_ncdz_hw_job
{
	void (*task)(void *);
	void *data;
	uint32_t size;
	uint32_t reserved[13];
} psp_me_ncdz_hw_job_t;

_Static_assert(sizeof(psp_me_ncdz_hw_job_t) == 64,
	"NCDZ hardware worker job descriptor must occupy exactly one cache line");

static psp_me_ncdz_hw_job_t hw_job __attribute__((aligned(64)));

static void hw_job_entry(void *param)
{
	psp_me_ncdz_hw_job_t *job = (psp_me_ncdz_hw_job_t *)param;

	meCoreDcacheInvalidateRange(job, sizeof(*job));
	meCoreDcacheInvalidateRange(job->data, job->size);
	job->task(job->data);
	meCoreDcacheWritebackRange(job->data, job->size);
}

static int hw_mist_entry(int index, void *param)
{
	(void)index;
	hw_job_entry(param);
	return meSafeTaskMistFinish();
}

static bool hw_dispatch_start(void (*task)(void *), void *data, uint32_t size,
	void *opaque)
{
	MistTrigger trigger;
	int trigger_result;

	(void)opaque;
	hw_job.task = task;
	hw_job.data = data;
	hw_job.size = size;
	sceKernelDcacheWritebackInvalidateRange(&hw_job, sizeof(hw_job));
	trigger.index = PSP_ME_NCDZ_HW_SYSCALL_INDEX;
	trigger.param = &hw_job;
	trigger_result = meSafeTaskMistTrigger(&trigger);
	return trigger_result >= 0;
}

static void hw_dispatch_wait(void *opaque)
{
	(void)opaque;
	meSafeTaskMistWait();
}

static int hw_init_mist(void)
{
	MistInjector injector;
	int result = meSafeTaskMistInit();

	if (result < 0)
		return result;
	injector.index = PSP_ME_NCDZ_HW_SYSCALL_INDEX;
	injector.addr = CACHED_KERNEL_MASK | (uint32_t)hw_mist_entry;
	meSafeTaskMistInjectSyscall(&injector);
	return 0;
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

static bool run_ncdz_profile_sequence(psp_me_sound_worker_t *worker,
	uint64_t base_time)
{
	static const uint32_t banks[4] = { 0, 0, 0, 0 };
	static const uint8_t timers_enabled[2] = { 0, 0 };
	static const uint64_t timers_remaining[2] = { 0, 0 };
	static const uint8_t program[] = {
		0x3e, 0x08, 0xd3, 0x04,
		0x3e, 0x0f, 0xd3, 0x05,
		0x3e, 0x3c, 0x32, 0x21, 0x43,
		0x76,
	};
	psp_me_sound_recovery_snapshot_t recovery;
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	void *ym_storage = NULL;
	ym2610_context_t *recovered_ym = alloc_ym_context(&ym_storage);
	uint8_t bytes[2];
	int32_t left[128];
	int32_t right[128];
	bool result = false;

	if (!recovered_ym)
		return false;

	memset(ncdz_memory, 0, sizeof(ncdz_memory));
	memset(ncdz_recovered_memory, 0, sizeof(ncdz_recovered_memory));
	memset(ncdz_pcm_a, 0, sizeof(ncdz_pcm_a));
	memcpy(ncdz_memory, program, sizeof(program));
	ncdz_memory[0x2000] = 0x12;
	ncdz_memory[0x2001] = 0x34;
	ncdz_memory[0x4321] = 0xa5;

	Cz80_Init(&reference_cpu);
	Cz80_Set_Fetch(&reference_cpu, 0x0000u, 0xffffu, (uintptr_t)ncdz_memory);
	Cz80_Set_ReadBase(&reference_cpu, (uintptr_t)ncdz_memory);
	Cz80_Reset(&reference_cpu);
	Cz80_Get_State(&reference_cpu, &initial_state);

	if (!psp_me_sound_worker_z80_snapshot_profiled_with_timers(worker,
			&initial_state, ncdz_memory, ncdz_memory,
			PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE, banks, 0, 0, 0, 44100u,
			ncdz_pcm_a, PSP_ME_NCDZ_HW_PCM_A_SIZE, 0u, timers_enabled,
			timers_remaining, false, PSP_ME_SOUND_Z80_MODE_AUTONOMOUS,
			PSP_ME_SOUND_MACHINE_PROFILE_NCDZ, PSP_ME_NCDZ_HW_TIMEOUT_US))
		goto done;
	if (worker->machine.memory_mode != PSP_ME_SOUND_Z80_MEMORY_FLAT_64K ||
		worker->machine.z80_cycles_per_usec != 6u ||
		worker->machine.ym_irq_line != 1u ||
		worker->machine.ym_pcm_mode != PSP_ME_SOUND_YM_PCM_DIRECT)
		goto done;

	if (!psp_me_sound_worker_z80_memory_read(worker, 0x2000u, bytes,
			sizeof(bytes), PSP_ME_NCDZ_HW_TIMEOUT_US) ||
		bytes[0] != 0x12 || bytes[1] != 0x34 ||
		!psp_me_sound_worker_z80_memory_write_byte(worker, 0x2000u, 0x56u) ||
		!psp_me_sound_worker_z80_memory_read_clear(worker, 0x2000u, bytes, 1u,
			PSP_ME_NCDZ_HW_TIMEOUT_US) || bytes[0] != 0x56 ||
		!psp_me_sound_worker_z80_memory_read(worker, 0x2000u, bytes, 1u,
			PSP_ME_NCDZ_HW_TIMEOUT_US) || bytes[0] != 0)
		goto done;

	if (!psp_me_sound_worker_ym_render_begin_direct(worker, 128u, base_time + 1u,
			PSP_ME_NCDZ_HW_TIMEOUT_US) ||
		!psp_me_sound_worker_ym_render_finish_authoritative(worker, left, right,
			128u, false, PSP_ME_NCDZ_HW_TIMEOUT_US))
		goto done;

	if (!psp_me_sound_worker_z80_advance_horizon(worker, base_time + 25u, 1000u) ||
		!psp_me_sound_worker_fence(worker, PSP_ME_NCDZ_HW_TIMEOUT_US) ||
		!psp_me_sound_worker_z80_memory_read(worker, 0x4321u, bytes, 1u,
			PSP_ME_NCDZ_HW_TIMEOUT_US) || bytes[0] != 0x3c)
		goto done;

	if (!psp_me_sound_worker_read_recovery_memory(worker, &recovery,
			ncdz_recovered_memory, PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE,
			recovered_ym, PSP_ME_NCDZ_HW_TIMEOUT_US) ||
		recovery.generation != worker->generation ||
		recovery.mode != PSP_ME_SOUND_Z80_MODE_AUTONOMOUS ||
		recovery.z80_time != base_time + 25u ||
		ncdz_recovered_memory[0x2000] != 0 ||
		ncdz_recovered_memory[0x2001] != 0x34 ||
		ncdz_recovered_memory[0x4321] != 0x3c)
		goto done;

	YM2610ContextWrite(recovered_ym, 0, 0x08);
	if (YM2610ContextRead(recovered_ym, 1) != 0x0f)
		goto done;

	result = true;

done:
	free(ym_storage);
	return result;
}

static bool run_cycle(const psp_me_sound_worker_dispatch_t *dispatch,
	uint32_t generation, uint64_t base_time, psp_me_sound_worker_stats_t *stats)
{
	psp_me_sound_worker_t worker;

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, dispatch, 16u,
			PSP_ME_NCDZ_HW_TIMEOUT_US) ||
		!psp_me_sound_worker_reset(&worker, generation,
			PSP_ME_NCDZ_HW_TIMEOUT_US) ||
		!psp_me_sound_worker_sync(&worker, base_time,
			PSP_ME_NCDZ_HW_TIMEOUT_US) ||
		!run_ncdz_profile_sequence(&worker, base_time) ||
		!psp_me_sound_worker_shutdown(&worker, PSP_ME_NCDZ_HW_TIMEOUT_US))
	{
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		psp_me_sound_worker_get_stats(&worker, stats);
		return false;
	}

	psp_me_sound_worker_get_stats(&worker, stats);
	return stats->generation == generation &&
		stats->resets == 1u && stats->syncs == 1u && stats->shutdowns == 1u &&
		stats->z80_snapshots == 1u && stats->z80_autonomous_slices != 0u &&
		stats->ym_renders == 1u && stats->ym_authoritative_renders == 1u &&
		stats->ym_render_samples == 128u && stats->ym_render_errors == 0u &&
		stats->z80_send_failures == 0u &&
		stats->fatal_error == PSP_ME_SOUND_WORKER_ERROR_NONE &&
		stats->emulated_time == base_time + 25u &&
		stats->command_overflow == 0u && stats->event_overflow == 0u;
}

static void log_result(int init_result, uint32_t completed_cycles, bool passed,
	const psp_me_sound_worker_stats_t *last_stats, uint64_t elapsed_us)
{
	char line[1024];
	int length;
	int fd;

	length = snprintf(line, sizeof(line),
		"[psp-me-ncdz-worker-hw] passed=%d init=%d cycles=%lu "
		"ordered_memory=%d direct_ym=%d recovery_64k=%d autonomous=%d "
		"elapsed_us=%llu generation=%lu commands=%lu z80_snapshots=%lu "
		"z80_slices=%lu z80_autonomous_slices=%lu ym_renders=%lu "
		"ym_authoritative_renders=%lu ym_samples=%lu ym_errors=%lu "
		"z80_send_failures=%lu fatal=%lu emulated_time=%llu "
		"cmd_overflow=%lu event_overflow=%lu\n",
		passed ? 1 : 0,
		init_result,
		(unsigned long)completed_cycles,
		passed ? 1 : 0,
		passed ? 1 : 0,
		passed ? 1 : 0,
		passed ? 1 : 0,
		(unsigned long long)elapsed_us,
		(unsigned long)last_stats->generation,
		(unsigned long)last_stats->commands_processed,
		(unsigned long)last_stats->z80_snapshots,
		(unsigned long)last_stats->z80_slices,
		(unsigned long)last_stats->z80_autonomous_slices,
		(unsigned long)last_stats->ym_renders,
		(unsigned long)last_stats->ym_authoritative_renders,
		(unsigned long)last_stats->ym_render_samples,
		(unsigned long)last_stats->ym_render_errors,
		(unsigned long)last_stats->z80_send_failures,
		(unsigned long)last_stats->fatal_error,
		(unsigned long long)last_stats->emulated_time,
		(unsigned long)last_stats->command_overflow,
		(unsigned long)last_stats->event_overflow);
	if (length <= 0 || (size_t)length >= sizeof(line))
		return;

	printf("%s", line);
	fd = open(PSP_ME_NCDZ_HW_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd >= 0)
	{
		write(fd, line, (size_t)length);
		close(fd);
	}
}

int main(int argc, char *argv[])
{
	const psp_me_sound_worker_dispatch_t dispatch = {
		hw_dispatch_start,
		hw_dispatch_wait,
		NULL,
	};
	psp_me_sound_worker_stats_t stats;
	uint32_t completed_cycles = 0;
	bool passed;
	uint64_t start_us;
	uint64_t elapsed_us;
	int init_result;

	(void)argc;
	(void)argv;
	memset(&stats, 0, sizeof(stats));
	remove(PSP_ME_NCDZ_HW_LOG_PATH);
	start_us = sceKernelGetSystemTimeWide();
	init_result = hw_init_mist();
	if (init_result >= 0)
	{
		if (run_cycle(&dispatch, 1u, 1000u, &stats))
			completed_cycles++;
		if (completed_cycles == 1u && run_cycle(&dispatch, 2u, 5000u, &stats))
			completed_cycles++;
	}

	passed = init_result >= 0 && completed_cycles == 2u;
	elapsed_us = sceKernelGetSystemTimeWide() - start_us;
	log_result(init_result, completed_cycles, passed, &stats, elapsed_us);
	return passed ? 0 : 1;
}
