#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pspkernel.h>
#include <pspthreadman.h>
#include "psp/psp_cps2_me_sound.h"
#include "common/audio_profile.h"
#include "common/cps2_sound_offload.h"
#include "common/runtime_paths.h"
#include "cps2/timer.h"
#include "cps2/driver.h"
#include "cps2/memintrf.h"
#include "sound/qsound.h"

#define PSP_CPS2_ME_WORKER_CAPACITY 16u
#define PSP_CPS2_ME_WORKER_TIMEOUT_US 2000000ULL
#define PSP_CPS2_ME_RENDER_POLL_SLEEP_US 50u
#define PSP_CPS2_ME_SHARED_RAM_OFFSET 0xc000u
#define PSP_CPS2_ME_SHARED_RAM_SIZE 0x1000u

static psp_me_qsound_worker_t worker;
static SceLwMutexWorkarea worker_mutex;
static uint32_t worker_generation;
static uint32_t window_frames;
static uint32_t owned_slices;
static uint32_t cpu_recovery_attempts;
static uint32_t cpu_recovery_successes;
static uint32_t cpu_recovery_failures;
static bool worker_mutex_ready;
static bool main_slice_active;
static bool main_slice_shared_acquired;
static bool main_slice_shared_dirty;
static bool resume_requested;
static bool authoritative;
static bool cpu_recovery_required;
static psp_me_qsound_worker_stats_t window_base;
static uint8_t recovery_memory[PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE]
	__attribute__((aligned(64)));

static bool worker_lock(void)
{
	return worker_mutex_ready &&
		sceKernelLockLwMutex(&worker_mutex, 1, NULL) >= 0;
}

static bool worker_try_lock(void)
{
	return worker_mutex_ready &&
		sceKernelTryLockLwMutex(&worker_mutex, 1) >= 0;
}

static void worker_unlock(void)
{
	if (worker_mutex_ready)
		(void)sceKernelUnlockLwMutex(&worker_mutex, 1);
}

static bool is_authoritative(void)
{
	return __atomic_load_n(&authoritative, __ATOMIC_ACQUIRE);
}

static bool recovery_required(void)
{
	return __atomic_load_n(&cpu_recovery_required, __ATOMIC_ACQUIRE);
}

static void set_authoritative(bool value)
{
	__atomic_store_n(&authoritative, value, __ATOMIC_RELEASE);
}

static void set_recovery_required(bool value)
{
	__atomic_store_n(&cpu_recovery_required, value, __ATOMIC_RELEASE);
}

static void reset_tracking(void)
{
	window_frames = 0;
	owned_slices = 0;
	cpu_recovery_attempts = 0;
	cpu_recovery_successes = 0;
	cpu_recovery_failures = 0;
	resume_requested = false;
	main_slice_active = false;
	main_slice_shared_acquired = false;
	main_slice_shared_dirty = false;
	set_authoritative(false);
	set_recovery_required(false);
}

static void mark_failed(const char *reason)
{
	if (!recovery_required())
		printf("[PSP_ME_QSOUND] persistent sound worker failed: %s; CPU recovery requested\n",
			reason);
	set_recovery_required(true);
}

static void log_window(const char *reason, bool force)
{
	psp_me_qsound_worker_stats_t stats;
	char path[1024];
	char line[1024];
	uint32_t frames = window_frames;
	int fd;
	int length;

	if (!force && frames < 300u)
		return;
	psp_me_qsound_worker_get_stats(&worker, &stats);
	length = snprintf(line, sizeof(line),
		"[psp-me-qsound] reason=%s generation=%lu frames=%lu authoritative=%u "
		"commands=%lu snapshots=%lu advances=%lu owned_slices=%lu irqs=%lu "
		"z80_resets=%lu bank_switches=%lu memory_reads=%lu memory_writes=%lu renders=%lu "
		"render_samples=%lu recoveries=%lu cpu_recovery_attempts=%lu "
		"cpu_recovery_successes=%lu cpu_recovery_failures=%lu "
		"command_high_water=%lu command_overflow=%lu event_high_water=%lu "
		"event_overflow=%lu fatal=%lu last_command_type=%lu emulated_time=%llu "
		"fatal_emulated_time=%llu\n",
		reason, (unsigned long)stats.generation, (unsigned long)frames,
		is_authoritative() ? 1u : 0u,
		(unsigned long)(stats.commands_processed - window_base.commands_processed),
		(unsigned long)(stats.snapshots - window_base.snapshots),
		(unsigned long)(stats.advances - window_base.advances),
		(unsigned long)owned_slices,
		(unsigned long)(stats.irqs - window_base.irqs),
		(unsigned long)(stats.z80_resets - window_base.z80_resets),
		(unsigned long)(stats.bank_switches - window_base.bank_switches),
		(unsigned long)(stats.memory_reads - window_base.memory_reads),
		(unsigned long)(stats.memory_writes - window_base.memory_writes),
		(unsigned long)(stats.renders - window_base.renders),
		(unsigned long)(stats.render_samples - window_base.render_samples),
		(unsigned long)(stats.recoveries - window_base.recoveries),
		(unsigned long)cpu_recovery_attempts,
		(unsigned long)cpu_recovery_successes,
		(unsigned long)cpu_recovery_failures,
		(unsigned long)stats.command_high_water,
		(unsigned long)stats.command_overflow,
		(unsigned long)stats.event_high_water,
		(unsigned long)stats.event_overflow,
		(unsigned long)stats.fatal_error,
		(unsigned long)stats.last_command_type,
		(unsigned long long)stats.emulated_time,
		(unsigned long long)stats.fatal_emulated_time);
	if (length > 0 && (size_t)length < sizeof(line))
	{
		snprintf(path, sizeof(path), "%spsp_me_qsound.log", launchDir);
		fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
		if (fd >= 0)
		{
			(void)write(fd, line, (size_t)length);
			close(fd);
		}
	}
	window_base = stats;
	window_frames = 0;
	owned_slices = 0;
	cpu_recovery_attempts = 0;
	cpu_recovery_successes = 0;
	cpu_recovery_failures = 0;
}

static bool recover_cpu_locked(void)
{
	psp_me_qsound_recovery_snapshot_t recovery;
	psp_me_qsound_worker_stats_t stats;
	qsound_context_t qsound;
	bool result;

	cpu_recovery_attempts++;
	psp_me_qsound_worker_get_stats(&worker, &stats);
	if (stats.fatal_error != 0)
	{
		result = psp_me_qsound_worker_read_published_recovery(&worker,
			&recovery, recovery_memory, &qsound);
	}
	else
	{
		result = psp_me_qsound_worker_recover(&worker, &recovery,
			recovery_memory, &qsound, PSP_CPS2_ME_WORKER_TIMEOUT_US);
	}
	if (!result)
	{
		cpu_recovery_failures++;
		return false;
	}

	memcpy(memory_region_cpu2, recovery_memory,
		PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE);
	Cz80_Set_State(&CZ80, &recovery.state);
	cps2_restore_z80_bank(recovery.bank);
	cps2_timer_restore_z80_suspended(recovery.suspended != 0);
	if (!qsound_default_restore_from_worker(&qsound))
	{
		cpu_recovery_failures++;
		return false;
	}
	set_authoritative(false);
	set_recovery_required(false);
	cpu_recovery_successes++;
	return true;
}

static bool recover_cpu(void)
{
	bool result;

	if (!is_authoritative() && !recovery_required())
		return true;
	if (!worker_lock())
	{
		cpu_recovery_attempts++;
		cpu_recovery_failures++;
		return false;
	}
	result = worker.running && recover_cpu_locked();
	worker_unlock();
	return result;
}

bool psp_cps2_me_sound_sync_init(void)
{
	char path[1024];

	memset(&worker, 0, sizeof(worker));
	memset(&worker_mutex, 0, sizeof(worker_mutex));
	memset(&window_base, 0, sizeof(window_base));
	worker_generation = 0;
	reset_tracking();
	worker_mutex_ready = sceKernelCreateLwMutex(&worker_mutex,
		"NJEMU ME QSound worker", 0, 0, NULL) >= 0;
	snprintf(path, sizeof(path), "%spsp_me_qsound.log", launchDir);
	remove(path);
	return worker_mutex_ready;
}

void psp_cps2_me_sound_sync_shutdown(void)
{
	if (worker_mutex_ready)
	{
		(void)sceKernelDeleteLwMutex(&worker_mutex);
		worker_mutex_ready = false;
	}
}

bool psp_cps2_me_sound_sync_ready(void)
{
	return worker_mutex_ready;
}

bool psp_cps2_me_sound_running(void)
{
	return worker.running;
}

bool psp_cps2_me_sound_bootstrap(const psp_me_qsound_worker_dispatch_t *dispatch)
{
	bool result = false;

	if (!dispatch || !worker_lock())
		return false;
	if (!psp_me_qsound_worker_start(&worker, dispatch,
			PSP_CPS2_ME_WORKER_CAPACITY, PSP_CPS2_ME_WORKER_TIMEOUT_US))
		goto done;
	worker_generation++;
	if (worker_generation == 0)
		worker_generation = 1;
	if (!psp_me_qsound_worker_reset(&worker, worker_generation,
			PSP_CPS2_ME_WORKER_TIMEOUT_US))
	{
		psp_me_qsound_worker_abort(&worker);
		goto done;
	}
	reset_tracking();
	psp_me_qsound_worker_get_stats(&worker, &window_base);
	result = true;

done:
	worker_unlock();
	return result;
}

void psp_cps2_me_sound_stop(void)
{
	if (!worker_mutex_ready)
		return;
	if ((is_authoritative() || recovery_required()) && !recover_cpu())
		printf("[PSP_ME_QSOUND] CPU recovery failed during worker stop\n");
	if (!worker_lock())
		return;
	if (worker.running)
	{
		log_window("stop", true);
		if (!psp_me_qsound_worker_shutdown(&worker, PSP_CPS2_ME_WORKER_TIMEOUT_US))
			psp_me_qsound_worker_abort(&worker);
	}
	reset_tracking();
	worker_unlock();
}

bool psp_cps2_me_sound_reset_generation(void)
{
	bool result = false;

	if ((is_authoritative() || recovery_required()) && !recover_cpu())
		return false;
	if (!worker_lock())
		return false;
	if (!worker.running)
		goto done;
	log_window("reset", true);
	worker_generation++;
	if (worker_generation == 0)
		worker_generation = 1;
	result = psp_me_qsound_worker_reset(&worker, worker_generation,
		PSP_CPS2_ME_WORKER_TIMEOUT_US);
	if (!result)
		psp_me_qsound_worker_abort(&worker);
	else
	{
		reset_tracking();
		psp_me_qsound_worker_get_stats(&worker, &window_base);
	}

done:
	worker_unlock();
	return result;
}

bool cps2_sound_offload_snapshot_from_cpu(void)
{
	cz80_state_t state;
	bool result = false;

	if (!worker.running)
		return true;
	if (!memory_region_cpu2 || memory_length_cpu2 < PSP_ME_QSOUND_Z80_ADDRESS_SPACE_SIZE ||
		!memory_region_sound1 || memory_length_sound1 == 0)
		return false;
	Cz80_Get_State(&CZ80, &state);
	if (!worker_lock())
		return false;
	if (worker.running)
	{
		result = psp_me_qsound_worker_snapshot(&worker, &state,
			memory_region_cpu2, memory_region_cpu2, memory_length_cpu2,
			memory_length_sound1, cps2_get_z80_bank(),
			cps2_timer_z80_suspended(), PSP_CPS2_ME_WORKER_TIMEOUT_US);
			if (result)
			{
				set_recovery_required(false);
				set_authoritative(true);
				log_window("snapshot", true);
			}
		}
	worker_unlock();
	if (!result)
	{
		printf("[PSP_ME_QSOUND] initial sound snapshot failed; CPU sound remains authoritative\n");
		log_window("snapshot-failed", true);
		set_authoritative(false);
		set_recovery_required(false);
	}
	return result;
}

bool cps2_sound_offload_prepare_cpu_state(void)
{
	resume_requested = is_authoritative();
	if (!is_authoritative() && !recovery_required())
		return true;
	if (!recover_cpu())
	{
		resume_requested = false;
		return false;
	}
	return true;
}

bool cps2_sound_offload_resume_from_cpu(void)
{
	bool resume = resume_requested;

	resume_requested = false;
	if (!resume)
		return true;
	if (!worker.running)
		return true;
	/* Save/load may move the CPS2 scheduler backwards in emulated time. Start a
	 * fresh worker generation before publishing the restored CPU state so the
	 * FIFO time-regression guard remains meaningful. */
	if (!psp_cps2_me_sound_reset_generation())
		return false;
	return cps2_sound_offload_snapshot_from_cpu();
}

bool cps2_sound_offload_z80_cpu_suppressed(void)
{
	return is_authoritative() || recovery_required();
}

bool cps2_sound_offload_main_slice_begin(void)
{
	main_slice_active = false;
	main_slice_shared_acquired = false;
	main_slice_shared_dirty = false;
	if (recovery_required())
	{
		(void)recover_cpu();
		return false;
	}
	if (!is_authoritative() || !qsound_sharedram1)
		return false;
	main_slice_active = true;
	return true;
}

bool cps2_sound_offload_main_shared_ram_access(bool write)
{
	bool result = false;
	bool locked = false;

	if (!main_slice_active)
		return !is_authoritative();
	if (main_slice_shared_acquired)
	{
		if (write)
			main_slice_shared_dirty = true;
		return true;
	}
	if (!worker_lock())
		goto fail;
	locked = true;
	if (!worker.running || !is_authoritative() || recovery_required() ||
		!psp_me_qsound_worker_sync(&worker, PSP_CPS2_ME_WORKER_TIMEOUT_US) ||
		!psp_me_qsound_worker_shared_ram_read(&worker,
			PSP_CPS2_ME_SHARED_RAM_OFFSET, qsound_sharedram1,
			PSP_CPS2_ME_SHARED_RAM_SIZE))
		goto fail;
	main_slice_shared_acquired = true;
	main_slice_shared_dirty = write;
	result = true;

fail:
	if (locked)
		worker_unlock();
	if (!result)
	{
		if (is_authoritative())
		{
			mark_failed("shared RAM access acquire");
			(void)recover_cpu();
		}
		main_slice_active = false;
		main_slice_shared_acquired = false;
		main_slice_shared_dirty = false;
	}
	return result;
}

bool cps2_sound_offload_main_slice_finish(uint32_t cycles, uint64_t emulated_time,
	bool run_z80)
{
	bool result = false;
	bool locked = false;

	if (!main_slice_active)
		return false;
	main_slice_active = false;
	if (!worker_lock())
		goto done;
	locked = true;
	if (worker.running && is_authoritative() && !recovery_required())
	{
		result = !main_slice_shared_dirty ||
			psp_me_qsound_worker_shared_ram_write(&worker,
				PSP_CPS2_ME_SHARED_RAM_OFFSET, qsound_sharedram1,
				PSP_CPS2_ME_SHARED_RAM_SIZE);
		if (result && run_z80)
			result = psp_me_qsound_worker_advance(&worker, cycles, emulated_time);
	}

done:
	main_slice_shared_acquired = false;
	main_slice_shared_dirty = false;
	if (locked)
		worker_unlock();
	if (!result)
	{
		mark_failed("shared RAM slice publish");
		(void)recover_cpu();
		return false;
	}
	if (run_z80)
		owned_slices++;
	return true;
}

bool cps2_sound_offload_advance(uint32_t cycles, uint64_t emulated_time)
{
	bool result = false;

	if (!is_authoritative())
		return false;
	if (recovery_required())
	{
		(void)recover_cpu();
		return false;
	}
	if (worker_lock())
	{
		if (worker.running && is_authoritative() && !recovery_required())
			result = psp_me_qsound_worker_advance(&worker, cycles, emulated_time);
		worker_unlock();
	}
	if (!result)
	{
		mark_failed("Z80 advance");
		(void)recover_cpu();
		return false;
	}
	owned_slices++;
	return true;
}

bool cps2_sound_offload_irq(int32_t state, uint64_t emulated_time)
{
	bool result = false;

	if (!is_authoritative())
		return false;
	if (worker_lock())
	{
		if (worker.running && is_authoritative() && !recovery_required())
			result = psp_me_qsound_worker_irq(&worker, state, emulated_time);
		worker_unlock();
	}
	if (!result)
	{
		mark_failed("Z80 IRQ");
		(void)recover_cpu();
	}
	return result;
}

bool cps2_sound_offload_z80_reset_line(int state, uint64_t emulated_time)
{
	bool result = false;

	if (!is_authoritative())
		return false;
	if (worker_lock())
	{
		if (worker.running && is_authoritative() && !recovery_required())
			result = psp_me_qsound_worker_z80_reset(&worker,
				state == ASSERT_LINE, emulated_time);
		worker_unlock();
	}
	if (!result)
	{
		mark_failed("Z80 reset");
		(void)recover_cpu();
	}
	return result;
}

bool cps2_sound_offload_memory_read(uint32_t offset, uint8_t *data, uint32_t size)
{
	bool result = false;

	if (!is_authoritative() || !data || size == 0)
		return false;
	if (worker_lock())
	{
		if (worker.running && is_authoritative() && !recovery_required())
			result = psp_me_qsound_worker_memory_read(&worker, offset, data, size,
				PSP_CPS2_ME_WORKER_TIMEOUT_US);
		worker_unlock();
	}
	if (!result)
	{
		mark_failed("shared RAM read");
		(void)recover_cpu();
	}
	return result;
}

bool cps2_sound_offload_memory_write_byte(uint32_t offset, uint8_t data)
{
	bool result = false;

	if (!is_authoritative())
		return false;
	if (worker_lock())
	{
		if (worker.running && is_authoritative() && !recovery_required())
			result = psp_me_qsound_worker_memory_write_byte(&worker, offset, data);
		worker_unlock();
	}
	if (!result)
	{
		mark_failed("shared RAM write");
		(void)recover_cpu();
	}
	return result;
}

bool cps2_sound_offload_render(int32_t **buffer, uint32_t samples)
{
	uint64_t wait_start;
	bool submitted = false;

	if (!buffer || !buffer[0] || !buffer[1] || !is_authoritative() ||
		recovery_required())
		return false;
	if (worker_lock())
	{
		if (worker.running && is_authoritative() && !recovery_required())
			submitted = psp_me_qsound_worker_render_begin(&worker, samples,
					worker.emulated_time);
		worker_unlock();
	}
	if (!submitted)
	{
		mark_failed("QSound render submit");
		(void)recover_cpu();
		return false;
	}

	wait_start = sceKernelGetSystemTimeWide();
	for (;;)
	{
		psp_me_qsound_render_result_t result = PSP_ME_QSOUND_RENDER_PENDING;

		if (worker_try_lock())
		{
			if (worker.running)
				result = psp_me_qsound_worker_render_poll(&worker,
					buffer[0], buffer[1], samples);
			else
				result = PSP_ME_QSOUND_RENDER_FAILED;
			worker_unlock();
		}
		if (result == PSP_ME_QSOUND_RENDER_COMPLETE)
		{
			audio_profile_add(AUDIO_PROFILE_PRODUCER_JOB_WAIT,
				sceKernelGetSystemTimeWide() - wait_start);
			return true;
		}
		if (result == PSP_ME_QSOUND_RENDER_FAILED ||
			sceKernelGetSystemTimeWide() - wait_start >= PSP_CPS2_ME_WORKER_TIMEOUT_US)
			break;
		sceKernelDelayThread(PSP_CPS2_ME_RENDER_POLL_SLEEP_US);
	}

	mark_failed("QSound render completion");
	(void)recover_cpu();
	return false;
}

void cps2_sound_offload_frame_completed(void)
{
	window_frames++;
	if (window_frames >= 300u)
		log_window("window", false);
}
