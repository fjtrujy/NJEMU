#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pspkernel.h>
#include <pspthreadman.h>
#include <me-safe-task/me-stask-mist.h>
#include <me-core-mapper/me-core-mapper.h>
#include <me-core-mapper/hw-registers.h>
#include "psp/psp_me_sound_worker.h"

PSP_MODULE_INFO("NJEMU ME Sound Worker", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define PSP_ME_SOUND_WORKER_HW_SYSCALL_INDEX 13
#define PSP_ME_SOUND_WORKER_HW_TIMEOUT_US 2000000ULL
#define PSP_ME_SOUND_WORKER_HW_SHADOW_MESSAGES 256u
#define PSP_ME_SOUND_WORKER_HW_LOG_PATH "host0:/njemu_me_sound_worker_hw.log"

typedef struct psp_me_sound_worker_hw_job
{
	void (*task)(void *);
	void *data;
	uint32_t size;
	uint32_t reserved[13];
} psp_me_sound_worker_hw_job_t;

_Static_assert(sizeof(psp_me_sound_worker_hw_job_t) == 64,
	"hardware worker job descriptor must occupy exactly one cache line");

static psp_me_sound_worker_hw_job_t hw_job __attribute__((aligned(64)));

static void hw_job_entry(void *param)
{
	psp_me_sound_worker_hw_job_t *job = (psp_me_sound_worker_hw_job_t *)param;

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
	trigger.index = PSP_ME_SOUND_WORKER_HW_SYSCALL_INDEX;
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
	injector.index = PSP_ME_SOUND_WORKER_HW_SYSCALL_INDEX;
	injector.addr = CACHED_KERNEL_MASK | (uint32_t)hw_mist_entry;
	meSafeTaskMistInjectSyscall(&injector);
	return 0;
}

static bool run_shadow_sequence(psp_me_sound_worker_t *worker,
	uint64_t base_time)
{
	uint32_t i;

	for (i = 0; i < PSP_ME_SOUND_WORKER_HW_SHADOW_MESSAGES; i++)
	{
		psp_me_sound_worker_stats_t stats;
		uint64_t start_us;
		uint64_t emulated_time = base_time + (uint64_t)(i / 4u);

		if (!psp_me_sound_worker_shadow_sound(worker, (uint8_t)(i * 37u + 11u),
			emulated_time))
			return false;
		start_us = sceKernelGetSystemTimeWide();
		for (;;)
		{
			if (!psp_me_sound_worker_poll(worker))
				return false;
			psp_me_sound_worker_get_stats(worker, &stats);
			if (stats.shadow_pending == 0)
				break;
			if (sceKernelGetSystemTimeWide() - start_us >=
				PSP_ME_SOUND_WORKER_HW_TIMEOUT_US)
				return false;
		}
	}
	return true;
}

static bool run_cycle(const psp_me_sound_worker_dispatch_t *dispatch,
	uint32_t generation, uint64_t first_time, uint64_t second_time,
	psp_me_sound_worker_stats_t *stats)
{
	psp_me_sound_worker_t worker;

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, dispatch, 8u,
		PSP_ME_SOUND_WORKER_HW_TIMEOUT_US))
		return false;
	if (!psp_me_sound_worker_reset(&worker, generation,
		PSP_ME_SOUND_WORKER_HW_TIMEOUT_US) ||
		!psp_me_sound_worker_sync(&worker, first_time,
			PSP_ME_SOUND_WORKER_HW_TIMEOUT_US) ||
		!run_shadow_sequence(&worker, first_time + 1u) ||
		!psp_me_sound_worker_sync(&worker, second_time,
			PSP_ME_SOUND_WORKER_HW_TIMEOUT_US) ||
		!psp_me_sound_worker_shutdown(&worker,
			PSP_ME_SOUND_WORKER_HW_TIMEOUT_US))
	{
		if (worker.running)
			psp_me_sound_worker_abort(&worker);
		psp_me_sound_worker_get_stats(&worker, stats);
		return false;
	}
	psp_me_sound_worker_get_stats(&worker, stats);
	return stats->generation == generation &&
		stats->commands_processed == 4u + PSP_ME_SOUND_WORKER_HW_SHADOW_MESSAGES &&
		stats->resets == 1u && stats->syncs == 2u && stats->shutdowns == 1u &&
		stats->shadow_commands == PSP_ME_SOUND_WORKER_HW_SHADOW_MESSAGES &&
		stats->shadow_sent == PSP_ME_SOUND_WORKER_HW_SHADOW_MESSAGES &&
		stats->shadow_matched == PSP_ME_SOUND_WORKER_HW_SHADOW_MESSAGES &&
		stats->shadow_mismatches == 0u && stats->shadow_send_failures == 0u &&
		stats->shadow_pending == 0u &&
		stats->fatal_error == PSP_ME_SOUND_WORKER_ERROR_NONE &&
		stats->emulated_time == second_time &&
		stats->command_overflow == 0u && stats->event_overflow == 0u;
}

static void log_result(int init_result, uint32_t completed_cycles,
	bool suspend_resume_recovery, bool passed,
	const psp_me_sound_worker_stats_t *last_stats, uint64_t elapsed_us)
{
	char line[1024];
	int length;
	int fd;

	length = snprintf(line, sizeof(line),
		"[psp-me-worker-hw] passed=%d init=%d cycles=%lu suspend_resume=%d "
		"elapsed_us=%llu generation=%lu commands=%lu resets=%lu syncs=%lu "
		"shutdowns=%lu shadow_commands=%lu shadow_sent=%lu shadow_matched=%lu "
		"shadow_mismatches=%lu shadow_send_failures=%lu shadow_pending=%lu "
		"shadow_pending_high_water=%lu heartbeat=%lu fatal=%lu emulated_time=%llu "
		"cmd_high_water=%lu cmd_overflow=%lu cmd_underflow=%lu "
		"event_high_water=%lu event_overflow=%lu event_underflow=%lu\n",
		passed ? 1 : 0,
		init_result,
		(unsigned long)completed_cycles,
		suspend_resume_recovery ? 1 : 0,
		(unsigned long long)elapsed_us,
		(unsigned long)last_stats->generation,
		(unsigned long)last_stats->commands_processed,
		(unsigned long)last_stats->resets,
		(unsigned long)last_stats->syncs,
		(unsigned long)last_stats->shutdowns,
		(unsigned long)last_stats->shadow_commands,
		(unsigned long)last_stats->shadow_sent,
		(unsigned long)last_stats->shadow_matched,
		(unsigned long)last_stats->shadow_mismatches,
		(unsigned long)last_stats->shadow_send_failures,
		(unsigned long)last_stats->shadow_pending,
		(unsigned long)last_stats->shadow_pending_high_water,
		(unsigned long)last_stats->heartbeat,
		(unsigned long)last_stats->fatal_error,
		(unsigned long long)last_stats->emulated_time,
		(unsigned long)last_stats->command_high_water,
		(unsigned long)last_stats->command_overflow,
		(unsigned long)last_stats->command_underflow,
		(unsigned long)last_stats->event_high_water,
		(unsigned long)last_stats->event_overflow,
		(unsigned long)last_stats->event_underflow);
	if (length <= 0 || (size_t)length >= sizeof(line))
		return;

	printf("%s", line);
	fd = open(PSP_ME_SOUND_WORKER_HW_LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
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
	bool suspend_resume_recovery = false;
	bool passed = false;
	uint64_t start_us;
	uint64_t elapsed_us;
	int init_result;

	(void)argc;
	(void)argv;
	memset(&stats, 0, sizeof(stats));
	remove(PSP_ME_SOUND_WORKER_HW_LOG_PATH);
	start_us = sceKernelGetSystemTimeWide();
	init_result = hw_init_mist();
	if (init_result >= 0)
	{
		if (run_cycle(&dispatch, 1u, 1000u, 5000u, &stats))
			completed_cycles++;
		if (completed_cycles == 1u &&
			run_cycle(&dispatch, 2u, 7000u, 9000u, &stats))
			completed_cycles++;

		/* This is the same stop/start ownership transition used by suspend/resume:
		 * stop the persistent worker before suspension, then bootstrap a fresh
		 * generation and continue from a new synchronization point after resume. */
		if (completed_cycles == 2u &&
			run_cycle(&dispatch, 3u, 11000u, 13000u, &stats))
		{
			completed_cycles++;
			if (run_cycle(&dispatch, 4u, 17000u, 19000u, &stats))
			{
				completed_cycles++;
				suspend_resume_recovery = true;
			}
		}
	}

	passed = init_result >= 0 && completed_cycles == 4u && suspend_resume_recovery;
	elapsed_us = sceKernelGetSystemTimeWide() - start_us;
	log_result(init_result, completed_cycles, suspend_resume_recovery, passed,
		&stats, elapsed_us);
	return passed ? 0 : 1;
}
