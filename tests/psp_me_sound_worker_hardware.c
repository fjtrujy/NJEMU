#include <fcntl.h>
#include <stdio.h>
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

PSP_MODULE_INFO("NJEMU ME Sound Worker", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define PSP_ME_SOUND_WORKER_HW_SYSCALL_INDEX 13
#define PSP_ME_SOUND_WORKER_HW_TIMEOUT_US 2000000ULL
#define PSP_ME_SOUND_WORKER_HW_SHADOW_MESSAGES 256u
#define PSP_ME_SOUND_WORKER_HW_LOG_PATH "host0:/njemu_me_sound_worker_hw.log"

static uint8_t z80_reference_memory[0x20000] __attribute__((aligned(64)));
static psp_me_sound_z80_io_t z80_reference_io[PSP_ME_SOUND_Z80_IO_CAPACITY];
static uint32_t z80_reference_io_count;
static uint8_t z80_reference_port_read_value;

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

static uint8_t z80_reference_read(uint32_t address)
{
	return z80_reference_memory[address & 0xffffu];
}

static void z80_reference_write(uint32_t address, uint8_t value)
{
	address &= 0xffffu;
	if (address >= PSP_ME_SOUND_Z80_RAM_OFFSET)
		z80_reference_memory[address] = value;
}

static uint8_t z80_reference_port_read(uint16_t port)
{
	psp_me_sound_z80_io_t *entry = &z80_reference_io[z80_reference_io_count++];
	entry->port = port;
	entry->type = PSP_ME_SOUND_Z80_IO_READ;
	entry->value = z80_reference_port_read_value;
	return entry->value;
}

static void z80_reference_port_write(uint16_t port, uint8_t value)
{
	psp_me_sound_z80_io_t *entry = &z80_reference_io[z80_reference_io_count++];
	entry->port = port;
	entry->type = PSP_ME_SOUND_Z80_IO_WRITE;
	entry->value = value;
}

static uint32_t z80_reference_ram_hash(void)
{
	uint32_t hash = 2166136261u;
	uint32_t i;

	for (i = PSP_ME_SOUND_Z80_RAM_OFFSET;
		i < PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE; i++)
	{
		hash ^= z80_reference_memory[i];
		hash *= 16777619u;
	}
	return hash;
}

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

static bool run_z80_shadow_sequence(psp_me_sound_worker_t *worker,
	uint64_t emulated_time)
{
	const uint32_t banks[4] = { 0x8000u, 0xc000u, 0xe000u, 0xf000u };
	const uint32_t timer_program_cycles = 108u;
	const uint32_t status_read_cycles = 24u;
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
	cz80_struc reference_cpu;
	cz80_state_t initial_state;
	cz80_state_t expected_state;
	ym2610_pcm_window_t window;
	int32_t expected_left[128];
	int32_t expected_right[128];

	memset(z80_reference_memory, 0, sizeof(z80_reference_memory));
	memcpy(z80_reference_memory, timer_program, sizeof(timer_program));
	memset(z80_reference_io, 0, sizeof(z80_reference_io));
	z80_reference_io_count = 0;
	z80_reference_port_read_value = 0;

	Cz80_Init(&reference_cpu);
	Cz80_Set_Fetch(&reference_cpu, 0x0000u, 0xffffu,
		(uintptr_t)z80_reference_memory);
	Cz80_Set_ReadBase(&reference_cpu, (uintptr_t)z80_reference_memory);
	Cz80_Set_ReadB(&reference_cpu, z80_reference_read);
	Cz80_Set_WriteB(&reference_cpu, z80_reference_write);
	Cz80_Set_INPort(&reference_cpu, z80_reference_port_read);
	Cz80_Set_OUTPort(&reference_cpu, z80_reference_port_write);
	Cz80_Reset(&reference_cpu);
	Cz80_Get_State(&reference_cpu, &initial_state);
	if (!psp_me_sound_worker_z80_snapshot(worker, &initial_state,
		z80_reference_memory, z80_reference_memory, sizeof(z80_reference_memory),
		banks, 0, 0, 0, 44100u, 0x1000u, 0x1000u,
		false, PSP_ME_SOUND_Z80_MODE_ORACLE, PSP_ME_SOUND_WORKER_HW_TIMEOUT_US))
		return false;

	(void)Cz80_Exec(&reference_cpu, (int32_t)timer_program_cycles);
	Cz80_Get_State(&reference_cpu, &expected_state);
	if (z80_reference_io_count != 6u ||
		!psp_me_sound_worker_z80_slice(worker, z80_reference_io,
			z80_reference_io_count, timer_program_cycles, emulated_time,
			&expected_state, banks, z80_reference_ram_hash(), true) ||
		!psp_me_sound_worker_ym_timer(worker, 0u, emulated_time + 1u) ||
		!psp_me_sound_worker_z80_irq(worker, ASSERT_LINE, emulated_time + 1u))
		return false;

	Cz80_Set_IRQ(&reference_cpu, 0, ASSERT_LINE);
	z80_reference_io_count = 0;
	z80_reference_port_read_value = 0x01u;
	(void)Cz80_Exec(&reference_cpu, (int32_t)status_read_cycles);
	Cz80_Get_State(&reference_cpu, &expected_state);
	if (z80_reference_io_count != 1u || z80_reference_memory[0xf800] != 0x01u)
		return false;
	if (!psp_me_sound_worker_z80_slice(worker, z80_reference_io,
		z80_reference_io_count, status_read_cycles, emulated_time + 2u,
		&expected_state, banks, z80_reference_ram_hash(), true))
		return false;

	/* Exercise the shared C5 render job on the physical ME. The timer-only
	 * sequence above leaves all audio generators silent, so an empty PCM window
	 * has a deterministic all-zero oracle while still validating job cache
	 * coherency, context rendering and the render ACK path. */
	if (!psp_me_sound_worker_ym_render_prepare(worker, 128u,
			emulated_time + 2u, &window, PSP_ME_SOUND_WORKER_HW_TIMEOUT_US))
		return false;
	memset(expected_left, 0, sizeof(expected_left));
	memset(expected_right, 0, sizeof(expected_right));
	return psp_me_sound_worker_ym_render_begin(worker, &window,
		emulated_time + 2u, PSP_ME_SOUND_WORKER_HW_TIMEOUT_US) &&
		psp_me_sound_worker_ym_render_finish(worker, expected_left, expected_right,
			128u, 0u, PSP_ME_SOUND_WORKER_HW_TIMEOUT_US);
}

static bool run_cycle(const psp_me_sound_worker_dispatch_t *dispatch,
	uint32_t generation, uint64_t first_time, uint64_t second_time,
	psp_me_sound_worker_stats_t *stats)
{
	psp_me_sound_worker_t worker;
	psp_me_sound_status_snapshot_t status;

	memset(&worker, 0, sizeof(worker));
	if (!psp_me_sound_worker_start(&worker, dispatch, 8u,
		PSP_ME_SOUND_WORKER_HW_TIMEOUT_US))
		return false;
	if (!psp_me_sound_worker_reset(&worker, generation,
			PSP_ME_SOUND_WORKER_HW_TIMEOUT_US) ||
		!psp_me_sound_worker_sync(&worker, first_time,
			PSP_ME_SOUND_WORKER_HW_TIMEOUT_US) ||
		!run_shadow_sequence(&worker, first_time + 1u) ||
		!run_z80_shadow_sequence(&worker, second_time - 2u) ||
		!psp_me_sound_worker_fence(&worker, PSP_ME_SOUND_WORKER_HW_TIMEOUT_US) ||
		!psp_me_sound_worker_read_status(&worker, &status) ||
		status.generation != generation || status.emulated_time != second_time ||
		!psp_me_sound_worker_sync(&worker, second_time,
			PSP_ME_SOUND_WORKER_HW_TIMEOUT_US) ||
		!psp_me_sound_worker_read_status(&worker, &status) ||
		status.generation != generation || status.emulated_time != second_time ||
		status.sound_code != 0u || status.pending_command != 0u ||
		status.result_code != 0u || status.initialized == 0u ||
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
			stats->commands_processed == 12u + PSP_ME_SOUND_WORKER_HW_SHADOW_MESSAGES &&
		stats->resets == 1u && stats->syncs == 2u && stats->shutdowns == 1u &&
		stats->shadow_commands == PSP_ME_SOUND_WORKER_HW_SHADOW_MESSAGES &&
		stats->shadow_sent == PSP_ME_SOUND_WORKER_HW_SHADOW_MESSAGES &&
		stats->shadow_matched == PSP_ME_SOUND_WORKER_HW_SHADOW_MESSAGES &&
		stats->shadow_mismatches == 0u && stats->shadow_send_failures == 0u &&
		stats->shadow_pending == 0u &&
			stats->z80_snapshots == 1u && stats->z80_irqs == 1u &&
			stats->z80_slices == 2u && stats->z80_io_events == 7u &&
		stats->z80_state_mismatches == 0u && stats->z80_ram_mismatches == 0u &&
		stats->z80_bank_mismatches == 0u && stats->z80_io_mismatches == 0u &&
		stats->z80_send_failures == 0u &&
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
		"shadow_pending_high_water=%lu z80_snapshots=%lu z80_irqs=%lu "
		"z80_slices=%lu z80_io=%lu z80_state_mismatches=%lu z80_ram_mismatches=%lu "
		"z80_bank_mismatches=%lu z80_io_mismatches=%lu z80_send_failures=%lu "
		"z80_last_mismatch=%lu z80_batch_high_water=%lu z80_batch_overflow=%lu "
		"heartbeat=%lu fatal=%lu "
		"emulated_time=%llu "
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
		(unsigned long)last_stats->z80_snapshots,
		(unsigned long)last_stats->z80_irqs,
		(unsigned long)last_stats->z80_slices,
		(unsigned long)last_stats->z80_io_events,
		(unsigned long)last_stats->z80_state_mismatches,
		(unsigned long)last_stats->z80_ram_mismatches,
		(unsigned long)last_stats->z80_bank_mismatches,
		(unsigned long)last_stats->z80_io_mismatches,
		(unsigned long)last_stats->z80_send_failures,
		(unsigned long)last_stats->z80_last_mismatch,
		(unsigned long)last_stats->z80_batch_high_water,
		(unsigned long)last_stats->z80_batch_overflow,
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
