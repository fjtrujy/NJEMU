#include <stdio.h>
#include <malloc.h>
#include <limits.h>
#include <string.h>
#include <pspkernel.h>
#include <me-safe-task/me-stask-mist.h>
#include <me-core-mapper/hw-registers.h>
#include "common/audio_producer_driver.h"
#include "common/emulator_options.h"
#if defined(PSP_ME_SOUND_COPROCESSOR) || defined(PSP_ME_RING_SELFTEST)
#include <fcntl.h>
#include <unistd.h>
#include "common/runtime_paths.h"
#endif
#ifdef PSP_ME_SOUND_COPROCESSOR
#include <pspthreadman.h>
#include "mvs/timer.h"
#include "mvs/driver.h"
#include "mvs/me_sound_shadow.h"
#include "psp/psp_me_sound_worker.h"
#include "sound/ym2610.h"
#endif
#ifdef PSP_ME_RING_SELFTEST
#include "psp/psp_me_spsc_ring_mist_test.h"
#endif

static bool me_available;
static bool me_suspended;
static bool me_job_in_flight;
static uint32_t me_probe_data[16] __attribute__((aligned(64)));
#ifdef PSP_ME_RING_SELFTEST
static bool me_ring_selftest_attempted;
static bool me_ring_selftest_passed;
#endif

typedef struct psp_me_audio_job
{
	audio_producer_job_fn job;
	void *data;
	uint32_t size;
	uint32_t reserved[13];
} psp_me_audio_job_t;

_Static_assert(sizeof(psp_me_audio_job_t) == 64,
	"PSP ME job descriptor must occupy exactly one cache line");

static psp_me_audio_job_t me_job __attribute__((aligned(64)));
static void *me_job_data;
static uint32_t me_job_cache_size;
static void *me_workspace;
static uint32_t me_workspace_size;
#ifdef PSP_ME_SOUND_COPROCESSOR
static psp_me_sound_worker_t me_sound_worker;
static SceLwMutexWorkarea me_sound_worker_mutex;
static SceLwMutexWorkarea me_sound_ym_gate;
static uint32_t me_sound_worker_generation;
static uint32_t me_sound_shadow_window_frames;
static psp_me_sound_worker_stats_t me_sound_shadow_window_base;
static psp_me_sound_z80_io_t me_sound_z80_io[PSP_ME_SOUND_Z80_IO_CAPACITY];
static uint32_t me_sound_z80_io_count;
static uint32_t me_sound_z80_slice_count;
static uint32_t me_sound_z80_io_peak;
static uint32_t me_sound_z80_failure_reason;
static uint32_t me_sound_status_checks;
static uint32_t me_sound_status_mismatches;
static uint32_t me_sound_status_presented_reads;
static uint32_t me_sound_status_fallback_busy;
static uint32_t me_sound_status_fallback_stale;
static uint32_t me_sound_status_fence_attempts;
static uint32_t me_sound_status_fence_matches;
static uint32_t me_sound_status_fence_pending;
static uint32_t me_sound_status_fence_failures;
static uint64_t me_sound_status_fence_wait_us;
static uint32_t me_sound_status_fence_wait_max_us;
static uint64_t me_sound_status_required_time;
static uint32_t me_sound_z80_owned_slices;
static uint32_t me_sound_cpu_recovery_attempts;
static uint32_t me_sound_cpu_recovery_successes;
static uint32_t me_sound_cpu_recovery_failures;
static uint8_t me_sound_cpu_replay_command;
static uint8_t me_sound_recovery_ram[PSP_ME_SOUND_Z80_RAM_SIZE]
	__attribute__((aligned(64)));
static bool me_sound_worker_mutex_ready;
static bool me_sound_ym_gate_ready;
static bool me_sound_z80_slice_gate_locked;
static bool me_sound_z80_horizon_queued;
static bool me_sound_ym_timer_gate_locked;
static bool me_sound_ym_render_gate_locked;
static bool me_sound_shadow_pending_hint;
static bool me_sound_shadow_failed;
static bool me_sound_z80_active;
static bool me_sound_z80_autonomous;
static bool me_sound_z80_collecting;
static bool me_sound_z80_io_overflow;
static bool me_sound_z80_failed;
static bool me_sound_ym_render_pending;
static bool me_sound_ym_authoritative;
static bool me_sound_z80_control_authoritative;
static bool me_sound_cpu_recovery_required;
static bool me_sound_cpu_replay_command_pending;
static bool me_sound_status_dirty;
#endif

static void psp_audio_producer_waitJob(void);

#define PSP_ME_PROBE_A 0x13579bdfu
#define PSP_ME_PROBE_B 0x2468ace0u
#define PSP_ME_SOUND_STATUS_FENCE_BUDGET_US 250ULL

static bool psp_me_mode_enabled(void)
{
	return option_audio_processor != AUDIO_PROCESSOR_MAIN_CPU;
}

static const char *psp_me_mode_name(void)
{
	switch (option_audio_processor)
	{
	case AUDIO_PROCESSOR_MAIN_CPU:
		return "Main CPU";
	case AUDIO_PROCESSOR_MEDIA_ENGINE:
		return "Media Engine";
	case AUDIO_PROCESSOR_AUTO:
	default:
		return "Auto";
	}
}

static void psp_me_probe_task(void *param)
{
	uint32_t *data = (uint32_t *)param;

	data[2] = data[0] ^ data[1];
	data[3] = data[0] + data[1];
}

static void psp_me_audio_job_entry(void *param)
{
	psp_me_audio_job_t *job = (psp_me_audio_job_t *)param;

	meCoreDcacheInvalidateRange(job, sizeof(*job));
	meCoreDcacheInvalidateRange(job->data, job->size);
	job->job(job->data);
	meCoreDcacheWritebackRange(job->data, job->size);
}

#define PSP_ME_MIST_SYSCALL_INDEX 13

static int psp_me_mist_job_entry(int index, void *param)
{
	(void)index;
	psp_me_audio_job_entry(param);
	return meSafeTaskMistFinish();
}

static int psp_me_dispatch_init(void)
{
	MistInjector injector;
	int result = meSafeTaskMistInit();

	if (result < 0)
		return result;

	injector.index = PSP_ME_MIST_SYSCALL_INDEX;
	injector.addr = CACHED_KERNEL_MASK | (uint32_t)psp_me_mist_job_entry;
	meSafeTaskMistInjectSyscall(&injector);
	return 0;
}

static int psp_me_dispatch_job(void)
{
	MistTrigger trigger = {
		.index = PSP_ME_MIST_SYSCALL_INDEX,
		.param = &me_job,
	};

	return meSafeTaskMistTrigger(&trigger);
}

static void psp_me_dispatch_wait(void)
{
	meSafeTaskMistWait();
}

#ifdef PSP_ME_SOUND_COPROCESSOR

#define PSP_ME_SOUND_WORKER_CAPACITY 16u
#define PSP_ME_SOUND_WORKER_TIMEOUT_US 2000000ULL

enum
{
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_NONE = 0,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_SNAPSHOT,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_IRQ_LOCK,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_IRQ_SEND,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_IO_OVERFLOW,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_INVALID_SLICE,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_SLICE_LOCK,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_SLICE_SEND,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_CHECKPOINT_SYNC,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_TIMER_LOCK,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_TIMER_SEND,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_RENDER_PREPARE,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_RENDER_LOCK,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_RENDER_SEND,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_RENDER_COMPARE,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_GATE_LOCK,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_STATUS_SNAPSHOT,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_ADVANCE_SEND,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_CHECKPOINT,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_COMMAND_LOCK,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_COMMAND_SEND,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_CONTEXT_SYNC,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_RECOVERY_LOCK,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_RECOVERY_SNAPSHOT,
	PSP_ME_SOUND_Z80_LOCAL_FAILURE_RECOVERY_RESTORE
};

static bool psp_me_sound_worker_lock(void)
{
	return me_sound_worker_mutex_ready &&
		sceKernelLockLwMutex(&me_sound_worker_mutex, 1, NULL) >= 0;
}

static bool psp_me_sound_worker_try_lock(void)
{
	return me_sound_worker_mutex_ready &&
		sceKernelTryLockLwMutex(&me_sound_worker_mutex, 1) >= 0;
}

static void psp_me_sound_worker_unlock(void)
{
	if (me_sound_worker_mutex_ready)
		(void)sceKernelUnlockLwMutex(&me_sound_worker_mutex, 1);
}

static bool psp_me_sound_ym_gate_lock(void)
{
	return me_sound_ym_gate_ready &&
		sceKernelLockLwMutex(&me_sound_ym_gate, 1, NULL) >= 0;
}

static void psp_me_sound_ym_gate_unlock(void)
{
	if (me_sound_ym_gate_ready)
		(void)sceKernelUnlockLwMutex(&me_sound_ym_gate, 1);
}

static bool psp_me_sound_worker_dispatch_start(void (*task)(void *), void *data,
	uint32_t size, void *opaque)
{
	(void)opaque;
	if (!task || !data || size == 0 || me_job_in_flight)
		return false;

	me_job.job = task;
	me_job.data = data;
	me_job.size = size;
	sceKernelDcacheWritebackInvalidateRange(&me_job, sizeof(me_job));
	return psp_me_dispatch_job() >= 0;
}

static void psp_me_sound_worker_dispatch_wait(void *opaque)
{
	(void)opaque;
	psp_me_dispatch_wait();
}

static void psp_me_sound_shadow_mark_failed(const char *reason)
{
	if (!me_sound_shadow_failed)
		printf("[PSP_ME_SOUND] command shadow oracle failed: %s; CPU sound remains authoritative\n",
			reason);
	me_sound_shadow_failed = true;
}

static void psp_me_sound_z80_mark_failed(const char *reason)
{
	if (!me_sound_z80_failed)
	{
		if (me_sound_z80_control_authoritative)
			printf("[PSP_ME_SOUND] ME Z80/control failed: %s; CPU recovery requested\n",
				reason);
		else
			printf("[PSP_ME_SOUND] Z80 shadow oracle failed: %s; CPU Z80 remains authoritative\n",
				reason);
	}
	me_sound_z80_failed = true;
	me_sound_ym_authoritative = false;
	if (me_sound_z80_control_authoritative)
		me_sound_cpu_recovery_required = true;
	else
		__atomic_store_n(&me_sound_z80_active, false, __ATOMIC_RELEASE);
}

static uint32_t psp_me_sound_z80_ram_hash(const uint8_t *visible_memory)
{
	uint32_t hash = 2166136261u;
	uint32_t i;

	for (i = PSP_ME_SOUND_Z80_RAM_OFFSET;
		i < PSP_ME_SOUND_Z80_ADDRESS_SPACE_SIZE; i++)
	{
		hash ^= visible_memory[i];
		hash *= 16777619u;
	}
	return hash;
}

static void psp_me_sound_z80_reset_tracking(void)
{
	__atomic_store_n(&me_sound_z80_active, false, __ATOMIC_RELEASE);
	me_sound_z80_autonomous = false;
	me_sound_z80_control_authoritative = false;
	me_sound_cpu_recovery_required = false;
	__atomic_store_n(&me_sound_z80_collecting, false, __ATOMIC_RELEASE);
	me_sound_z80_io_overflow = false;
	me_sound_z80_io_count = 0;
	me_sound_z80_slice_count = 0;
	me_sound_z80_io_peak = 0;
	me_sound_z80_owned_slices = 0;
	me_sound_cpu_recovery_attempts = 0;
	me_sound_cpu_recovery_successes = 0;
	me_sound_cpu_recovery_failures = 0;
	me_sound_cpu_replay_command = 0;
	me_sound_cpu_replay_command_pending = false;
	me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_NONE;
	me_sound_status_checks = 0;
	me_sound_status_mismatches = 0;
	me_sound_status_presented_reads = 0;
	me_sound_status_fallback_busy = 0;
	me_sound_status_fallback_stale = 0;
	me_sound_status_fence_attempts = 0;
	me_sound_status_fence_matches = 0;
	me_sound_status_fence_pending = 0;
	me_sound_status_fence_failures = 0;
	me_sound_status_fence_wait_us = 0;
	me_sound_status_fence_wait_max_us = 0;
	me_sound_status_required_time = 0;
	__atomic_store_n(&me_sound_status_dirty, true, __ATOMIC_RELEASE);
	me_sound_z80_failed = false;
	me_sound_z80_horizon_queued = false;
	me_sound_ym_render_pending = false;
	me_sound_ym_authoritative = false;
}

static bool psp_me_sound_recover_cpu(void)
{
	psp_me_sound_recovery_snapshot_t recovery;
	bool result = false;
	bool gate_locked = false;
	bool worker_locked = false;
	uint32_t channel;

	if (!me_sound_cpu_recovery_required)
		return true;
	me_sound_cpu_recovery_attempts++;
	if (!psp_me_sound_ym_gate_lock())
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_RECOVERY_LOCK;
		goto done;
	}
	gate_locked = true;
	if (!psp_me_sound_worker_lock())
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_RECOVERY_LOCK;
		goto done;
	}
	worker_locked = true;
	if (!me_available || !me_sound_worker.running ||
		!psp_me_sound_worker_read_recovery_snapshot(&me_sound_worker, &recovery,
			me_sound_recovery_ram, NULL, PSP_ME_SOUND_WORKER_TIMEOUT_US))
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_RECOVERY_SNAPSHOT;
		goto done;
	}
	for (channel = 0; channel < 2u; channel++)
	{
		if (recovery.ym_timer_remaining[channel] > (uint64_t)INT_MAX)
		{
			me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_RECOVERY_RESTORE;
			goto done;
		}
	}
	if (!neogeo_restore_z80_shadow_state(&recovery.state, recovery.banks,
			me_sound_recovery_ram, recovery.sound_code,
			recovery.pending_command, recovery.result_code) ||
		!YM2610DefaultRestoreFromPcmWindow(
			(const ym2610_context_t *)me_sound_worker.ym_context) ||
		!timer_restore_ym2610_state(recovery.ym_timer_enabled,
			recovery.ym_timer_remaining))
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_RECOVERY_RESTORE;
		goto done;
	}
	if (me_sound_cpu_replay_command_pending)
	{
		neogeo_apply_z80_sound_command(me_sound_cpu_replay_command);
		me_sound_cpu_replay_command_pending = false;
	}

	me_sound_z80_control_authoritative = false;
	me_sound_cpu_recovery_required = false;
	me_sound_z80_autonomous = false;
	me_sound_ym_authoritative = false;
	me_sound_status_required_time = recovery.emulated_time;
	__atomic_store_n(&me_sound_status_dirty, true, __ATOMIC_RELEASE);
	__atomic_store_n(&me_sound_z80_active, false, __ATOMIC_RELEASE);
	me_sound_cpu_recovery_successes++;
	result = true;
	printf("[PSP_ME_SOUND] restored ME sound state to CPU at %llu us\n",
		(unsigned long long)recovery.emulated_time);

done:
	if (worker_locked)
		psp_me_sound_worker_unlock();
	if (gate_locked)
		psp_me_sound_ym_gate_unlock();
	if (!result)
		me_sound_cpu_recovery_failures++;
	return result;
}

void mvs_me_sound_shadow_scheduler_boundary(void)
{
	if (me_sound_cpu_recovery_required)
		(void)psp_me_sound_recover_cpu();
}

bool mvs_me_sound_shadow_z80_cpu_suppressed(void)
{
	return me_sound_z80_control_authoritative || me_sound_cpu_recovery_required;
}

static void psp_me_sound_shadow_log_window(const char *reason, bool force)
{
	psp_me_sound_worker_stats_t stats;
	char path[1024];
	char line[2048];
	uint32_t frames;
	uint32_t sent;
	uint32_t matched;
	uint32_t processed;
	uint32_t mismatches;
	uint32_t send_failures;
	uint32_t z80_snapshots;
	uint32_t z80_irqs;
	uint32_t z80_slices;
	uint32_t z80_io_events;
	uint32_t z80_state_mismatches;
	uint32_t z80_ram_mismatches;
	uint32_t z80_bank_mismatches;
	uint32_t z80_io_mismatches;
	uint32_t z80_send_failures;
	uint32_t z80_autonomous_slices;
	uint32_t z80_checkpoints;
	uint32_t ym_timer_callbacks;
	uint32_t ym_timer_overflows;
	uint32_t status_presented_reads;
	uint32_t status_fallback_busy;
	uint32_t status_fallback_stale;
	uint32_t status_fence_attempts;
	uint32_t status_fence_matches;
	uint32_t status_fence_pending;
	uint32_t status_fence_failures;
	uint64_t status_fence_wait_us;
	uint32_t status_fence_wait_max_us;
	uint32_t ym_renders;
	uint32_t ym_render_samples;
	uint32_t ym_render_errors;
	uint32_t ym_presented_renders;
	uint32_t ym_presented_samples;
	uint32_t ym_authoritative_renders;
	uint32_t ym_context_sync_failures;
	uint32_t ym_pcm_mismatches;
	uint32_t ym_status_mismatches;
	uint32_t ym_send_failures;
	int length;
	int fd;

	frames = __atomic_load_n(&me_sound_shadow_window_frames, __ATOMIC_RELAXED);
	if (frames == 0 && !force)
		return;
	psp_me_sound_worker_get_stats(&me_sound_worker, &stats);
	sent = stats.shadow_sent - me_sound_shadow_window_base.shadow_sent;
	matched = stats.shadow_matched - me_sound_shadow_window_base.shadow_matched;
	processed = stats.shadow_commands - me_sound_shadow_window_base.shadow_commands;
	mismatches = stats.shadow_mismatches - me_sound_shadow_window_base.shadow_mismatches;
	send_failures = stats.shadow_send_failures -
		me_sound_shadow_window_base.shadow_send_failures;
	z80_snapshots = stats.z80_snapshots - me_sound_shadow_window_base.z80_snapshots;
	z80_irqs = stats.z80_irqs - me_sound_shadow_window_base.z80_irqs;
	z80_slices = stats.z80_slices - me_sound_shadow_window_base.z80_slices;
	z80_io_events = stats.z80_io_events - me_sound_shadow_window_base.z80_io_events;
	z80_state_mismatches = stats.z80_state_mismatches -
		me_sound_shadow_window_base.z80_state_mismatches;
	z80_ram_mismatches = stats.z80_ram_mismatches -
		me_sound_shadow_window_base.z80_ram_mismatches;
	z80_bank_mismatches = stats.z80_bank_mismatches -
		me_sound_shadow_window_base.z80_bank_mismatches;
	z80_io_mismatches = stats.z80_io_mismatches -
		me_sound_shadow_window_base.z80_io_mismatches;
	z80_send_failures = stats.z80_send_failures -
		me_sound_shadow_window_base.z80_send_failures;
	z80_autonomous_slices = stats.z80_autonomous_slices -
		me_sound_shadow_window_base.z80_autonomous_slices;
	z80_checkpoints = stats.z80_checkpoints -
		me_sound_shadow_window_base.z80_checkpoints;
	ym_timer_callbacks = stats.ym_timer_callbacks -
		me_sound_shadow_window_base.ym_timer_callbacks;
	ym_timer_overflows = stats.ym_timer_overflows -
		me_sound_shadow_window_base.ym_timer_overflows;
	status_presented_reads = me_sound_status_presented_reads;
	status_fallback_busy = me_sound_status_fallback_busy;
	status_fallback_stale = me_sound_status_fallback_stale;
	status_fence_attempts = me_sound_status_fence_attempts;
	status_fence_matches = me_sound_status_fence_matches;
	status_fence_pending = me_sound_status_fence_pending;
	status_fence_failures = me_sound_status_fence_failures;
	status_fence_wait_us = me_sound_status_fence_wait_us;
	status_fence_wait_max_us = me_sound_status_fence_wait_max_us;
	ym_renders = stats.ym_renders - me_sound_shadow_window_base.ym_renders;
	ym_render_samples = stats.ym_render_samples -
		me_sound_shadow_window_base.ym_render_samples;
	ym_render_errors = stats.ym_render_errors -
		me_sound_shadow_window_base.ym_render_errors;
	ym_presented_renders = stats.ym_presented_renders -
		me_sound_shadow_window_base.ym_presented_renders;
	ym_presented_samples = stats.ym_presented_samples -
		me_sound_shadow_window_base.ym_presented_samples;
	ym_authoritative_renders = stats.ym_authoritative_renders -
		me_sound_shadow_window_base.ym_authoritative_renders;
	ym_context_sync_failures = stats.ym_context_sync_failures -
		me_sound_shadow_window_base.ym_context_sync_failures;
	ym_pcm_mismatches = stats.ym_pcm_mismatches -
		me_sound_shadow_window_base.ym_pcm_mismatches;
	ym_status_mismatches = stats.ym_status_mismatches -
		me_sound_shadow_window_base.ym_status_mismatches;
	ym_send_failures = stats.ym_send_failures -
		me_sound_shadow_window_base.ym_send_failures;
	length = snprintf(line, sizeof(line),
		"[psp-me-shadow] reason=%s generation=%lu frames=%lu sent=%lu matched=%lu "
		"mismatches=%lu send_failures=%lu pending=%lu pending_high_water=%lu "
		"me_processed=%lu command_high_water=%lu command_overflow=%lu "
		"event_high_water=%lu event_overflow=%lu z80_active=%u z80_snapshots=%lu "
		"z80_irqs=%lu z80_slices=%lu z80_io=%lu z80_state_mismatches=%lu "
			"z80_ram_mismatches=%lu z80_bank_mismatches=%lu z80_io_mismatches=%lu "
			"z80_send_failures=%lu z80_last_mismatch=%lu z80_batch_high_water=%lu "
			"z80_batch_overflow=%lu z80_local_failure=%lu z80_io_peak=%lu "
			"z80_autonomous_slices=%lu z80_owned_slices=%lu z80_checkpoints=%lu "
			"cpu_recovery_attempts=%lu cpu_recovery_successes=%lu "
			"cpu_recovery_failures=%lu "
			"ym_timer_callbacks=%lu ym_timer_overflows=%lu "
			"status_checks=%lu status_mismatches=%lu status_presented=%lu "
			"status_fallback_busy=%lu status_fallback_stale=%lu "
			"status_fence_attempts=%lu status_fence_matches=%lu "
			"status_fence_pending=%lu status_fence_failures=%lu "
			"status_fence_wait_us=%llu status_fence_wait_max_us=%lu "
			"ym_renders=%lu ym_samples=%lu ym_render_errors=%lu "
			"ym_presented_renders=%lu ym_presented_samples=%lu "
			"ym_authoritative_renders=%lu ym_context_sync_failures=%lu "
			"ym_pcm_mismatches=%lu ym_status_mismatches=%lu ym_send_failures=%lu "
			"ym_first_sample=%lu ym_first_channel=%lu ym_first_expected=%ld "
			"ym_first_actual=%ld "
			"fatal=%lu last_command_type=%lu "
			"emulated_time=%llu fatal_emulated_time=%llu\n",
		reason,
		(unsigned long)stats.generation,
		(unsigned long)frames,
		(unsigned long)sent,
		(unsigned long)matched,
		(unsigned long)mismatches,
		(unsigned long)send_failures,
		(unsigned long)stats.shadow_pending,
		(unsigned long)stats.shadow_pending_high_water,
		(unsigned long)processed,
		(unsigned long)stats.command_high_water,
		(unsigned long)stats.command_overflow,
		(unsigned long)stats.event_high_water,
		(unsigned long)stats.event_overflow,
		__atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE) ? 1u : 0u,
		(unsigned long)z80_snapshots,
		(unsigned long)z80_irqs,
		(unsigned long)z80_slices,
		(unsigned long)z80_io_events,
		(unsigned long)z80_state_mismatches,
		(unsigned long)z80_ram_mismatches,
		(unsigned long)z80_bank_mismatches,
		(unsigned long)z80_io_mismatches,
		(unsigned long)z80_send_failures,
			(unsigned long)stats.z80_last_mismatch,
			(unsigned long)stats.z80_batch_high_water,
			(unsigned long)stats.z80_batch_overflow,
			(unsigned long)me_sound_z80_failure_reason,
			(unsigned long)me_sound_z80_io_peak,
			(unsigned long)z80_autonomous_slices,
			(unsigned long)me_sound_z80_owned_slices,
			(unsigned long)z80_checkpoints,
			(unsigned long)me_sound_cpu_recovery_attempts,
			(unsigned long)me_sound_cpu_recovery_successes,
			(unsigned long)me_sound_cpu_recovery_failures,
			(unsigned long)ym_timer_callbacks,
			(unsigned long)ym_timer_overflows,
			(unsigned long)me_sound_status_checks,
			(unsigned long)me_sound_status_mismatches,
			(unsigned long)status_presented_reads,
			(unsigned long)status_fallback_busy,
			(unsigned long)status_fallback_stale,
			(unsigned long)status_fence_attempts,
			(unsigned long)status_fence_matches,
			(unsigned long)status_fence_pending,
			(unsigned long)status_fence_failures,
			(unsigned long long)status_fence_wait_us,
			(unsigned long)status_fence_wait_max_us,
			(unsigned long)ym_renders,
			(unsigned long)ym_render_samples,
			(unsigned long)ym_render_errors,
			(unsigned long)ym_presented_renders,
			(unsigned long)ym_presented_samples,
			(unsigned long)ym_authoritative_renders,
			(unsigned long)ym_context_sync_failures,
			(unsigned long)ym_pcm_mismatches,
			(unsigned long)ym_status_mismatches,
			(unsigned long)ym_send_failures,
			(unsigned long)stats.ym_first_pcm_mismatch_sample,
			(unsigned long)stats.ym_first_pcm_mismatch_channel,
			(long)stats.ym_first_pcm_expected,
			(long)stats.ym_first_pcm_actual,
			(unsigned long)stats.fatal_error,
			(unsigned long)stats.last_command_type,
			(unsigned long long)stats.emulated_time,
			(unsigned long long)stats.fatal_emulated_time);
	if (length > 0 && (size_t)length < sizeof(line))
	{
		printf("%s", line);
		snprintf(path, sizeof(path), "%spsp_me_sound_shadow.log", launchDir);
		fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
		if (fd >= 0)
		{
			write(fd, line, (size_t)length);
			close(fd);
		}
	}
	me_sound_shadow_window_base = stats;
	me_sound_status_checks = 0;
	me_sound_status_mismatches = 0;
	me_sound_status_presented_reads = 0;
	me_sound_status_fallback_busy = 0;
	me_sound_status_fallback_stale = 0;
	me_sound_status_fence_attempts = 0;
	me_sound_status_fence_matches = 0;
	me_sound_status_fence_pending = 0;
	me_sound_status_fence_failures = 0;
	me_sound_status_fence_wait_us = 0;
	me_sound_status_fence_wait_max_us = 0;
	me_sound_z80_owned_slices = 0;
	me_sound_cpu_recovery_attempts = 0;
	me_sound_cpu_recovery_successes = 0;
	me_sound_cpu_recovery_failures = 0;
	__atomic_store_n(&me_sound_shadow_window_frames, 0, __ATOMIC_RELAXED);
}

bool mvs_me_sound_shadow_command(uint8_t command, uint64_t emulated_time)
{
	bool result = false;
	bool active = __atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE);

	if (!psp_me_sound_worker_lock())
	{
		if (active)
		{
			if (me_sound_z80_control_authoritative)
			{
				me_sound_cpu_replay_command = command;
				me_sound_cpu_replay_command_pending = true;
			}
			me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_COMMAND_LOCK;
			psp_me_sound_shadow_mark_failed("command lock");
			psp_me_sound_z80_mark_failed("sound command lock");
		}
		return false;
	}
	if (me_available && me_sound_worker.running)
	{
		result = psp_me_sound_worker_shadow_sound(&me_sound_worker, command,
			emulated_time);
		if (result)
		{
			me_sound_status_required_time = emulated_time;
			__atomic_store_n(&me_sound_status_dirty, false, __ATOMIC_RELEASE);
			__atomic_store_n(&me_sound_shadow_pending_hint, true, __ATOMIC_RELEASE);
		}
	}
	psp_me_sound_worker_unlock();
	if (!result && active)
	{
		if (me_sound_z80_control_authoritative)
		{
			me_sound_cpu_replay_command = command;
			me_sound_cpu_replay_command_pending = true;
		}
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_COMMAND_SEND;
		psp_me_sound_shadow_mark_failed("send/poll");
		psp_me_sound_z80_mark_failed("sound command send");
	}
	return result;
}

void mvs_me_sound_shadow_status_pending(void)
{
	if (__atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE))
		__atomic_store_n(&me_sound_status_dirty, true, __ATOMIC_RELEASE);
}

static void psp_me_sound_status_record_fence_wait(uint64_t wait_us)
{
	me_sound_status_fence_wait_us += wait_us;
	if (wait_us > me_sound_status_fence_wait_max_us)
		me_sound_status_fence_wait_max_us = (uint32_t)wait_us;
}

static psp_me_sound_status_validation_t psp_me_sound_present_status(
	const psp_me_sound_status_snapshot_t *status, uint8_t sound_code,
	uint8_t pending_command, uint8_t result_code, uint8_t *presented_pending,
	uint8_t *presented_result)
{
	if (me_sound_z80_control_authoritative && !me_sound_cpu_recovery_required)
		return psp_me_sound_worker_present_authoritative_status(status,
			me_sound_worker.generation, me_sound_status_required_time,
			presented_pending, presented_result);
	return psp_me_sound_worker_present_status(status, me_sound_worker.generation,
		me_sound_status_required_time, sound_code, pending_command, result_code,
		presented_pending, presented_result);
}

bool mvs_me_sound_shadow_main_status(uint8_t sound_code, uint8_t pending_command,
	uint8_t result_code, uint8_t *presented_pending, uint8_t *presented_result)
{
	psp_me_sound_status_snapshot_t status;
	psp_me_sound_status_validation_t validation;
	psp_me_sound_fence_result_t fence_result;
	uint64_t required_time;
	uint64_t fence_start;
	uint64_t fence_wait;
	uint64_t fenced_time = 0;
	bool result = false;

	if (!presented_pending || !presented_result ||
		!__atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE) ||
		!me_sound_z80_autonomous ||
		__atomic_load_n(&me_sound_status_dirty, __ATOMIC_ACQUIRE))
		return false;
	if (!psp_me_sound_worker_try_lock())
	{
		me_sound_status_fallback_busy++;
		return false;
	}
	if (!me_available || !me_sound_worker.running ||
		!psp_me_sound_worker_read_status(&me_sound_worker, &status))
		goto done;

	required_time = me_sound_status_required_time;
	validation = psp_me_sound_present_status(&status, sound_code, pending_command,
		result_code, presented_pending, presented_result);
	if (validation == PSP_ME_SOUND_STATUS_MATCH)
	{
		me_sound_status_presented_reads++;
		result = true;
	}
	else
	{
		fence_start = sceKernelGetSystemTimeWide();
		if (!me_sound_worker.fence_in_flight)
		{
			me_sound_status_fence_attempts++;
			if (!psp_me_sound_worker_fence_begin(&me_sound_worker))
			{
				me_sound_status_fence_pending++;
				goto stale_fallback;
			}
		}
		for (;;)
		{
			fence_result = psp_me_sound_worker_fence_poll(&me_sound_worker,
				&fenced_time);
			fence_wait = sceKernelGetSystemTimeWide() - fence_start;
			if (fence_result == PSP_ME_SOUND_FENCE_COMPLETE)
			{
				if (!psp_me_sound_worker_read_status(&me_sound_worker, &status))
				{
					psp_me_sound_status_record_fence_wait(fence_wait);
					me_sound_status_fence_failures++;
					goto stale_fallback;
				}
				validation = psp_me_sound_present_status(&status, sound_code,
					pending_command, result_code, presented_pending, presented_result);
				if (validation == PSP_ME_SOUND_STATUS_MATCH)
				{
					psp_me_sound_status_record_fence_wait(fence_wait);
					me_sound_status_presented_reads++;
					me_sound_status_fence_matches++;
					result = true;
					goto done;
				}
				if (validation == PSP_ME_SOUND_STATUS_MISMATCH)
				{
					psp_me_sound_status_record_fence_wait(fence_wait);
					goto mismatch;
				}
				if (fenced_time < required_time)
				{
					if (fence_wait >= PSP_ME_SOUND_STATUS_FENCE_BUDGET_US)
					{
						psp_me_sound_status_record_fence_wait(fence_wait);
						me_sound_status_fence_pending++;
						goto stale_fallback;
					}
					me_sound_status_fence_attempts++;
					if (!psp_me_sound_worker_fence_begin(&me_sound_worker))
					{
						me_sound_status_fence_pending++;
						goto stale_fallback;
					}
					continue;
				}
				/* A completed FIFO fence must expose all work that was queued
				 * before this read. Remaining stale state is a timing divergence. */
				psp_me_sound_status_record_fence_wait(fence_wait);
				me_sound_status_fence_failures++;
				goto mismatch;
			}
			if (fence_result == PSP_ME_SOUND_FENCE_FAILED)
			{
				psp_me_sound_status_record_fence_wait(fence_wait);
				me_sound_status_fence_failures++;
				goto mismatch;
			}
			if (fence_wait >= PSP_ME_SOUND_STATUS_FENCE_BUDGET_US)
			{
				psp_me_sound_status_record_fence_wait(fence_wait);
				me_sound_status_fence_pending++;
				goto stale_fallback;
			}
		}
	}
mismatch:
		me_sound_status_mismatches++;
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_STATUS_SNAPSHOT;
		psp_me_sound_z80_mark_failed("main status snapshot");
	goto done;

stale_fallback:
	me_sound_status_fallback_stale++;

done:
	psp_me_sound_worker_unlock();
	return result;
}

bool mvs_me_sound_shadow_z80_snapshot(const cz80_state_t *state,
	const uint8_t *visible_memory, const uint8_t *source_rom,
	uint32_t source_length, const uint32_t banks[4], uint8_t sound_code,
	uint8_t pending_command, uint8_t result_code, uint32_t pcm_a_size,
	uint32_t pcm_b_size)
{
	bool result = false;
	bool gate_locked = false;

	psp_me_sound_z80_reset_tracking();
	if (!state || !visible_memory || !source_rom || !banks)
		return false;
	if (!me_available || !me_sound_worker.running)
		return false;
	if (!psp_me_sound_ym_gate_lock())
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_GATE_LOCK;
		psp_me_sound_z80_mark_failed("YM snapshot gate lock");
		return false;
	}
	gate_locked = true;
	if (!psp_me_sound_worker_lock())
		goto done;
	if (me_available && me_sound_worker.running)
	{
		uint32_t ym_sample_rate = 44100u >> (2 - option_samplerate);
		result = psp_me_sound_worker_z80_snapshot(&me_sound_worker, state,
			visible_memory, source_rom, source_length, banks, sound_code,
			pending_command, result_code, ym_sample_rate, pcm_a_size, pcm_b_size,
			true, PSP_ME_SOUND_Z80_MODE_AUTONOMOUS, PSP_ME_SOUND_WORKER_TIMEOUT_US);
		if (result)
		{
			me_sound_z80_autonomous = true;
			me_sound_ym_authoritative = true;
			me_sound_z80_control_authoritative = true;
			me_sound_cpu_recovery_required = false;
			me_sound_status_required_time = 0;
			__atomic_store_n(&me_sound_status_dirty, false, __ATOMIC_RELEASE);
			__atomic_store_n(&me_sound_z80_active, true, __ATOMIC_RELEASE);
		}
		else
		{
			me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_SNAPSHOT;
			psp_me_sound_z80_mark_failed("snapshot");
		}
	}
	psp_me_sound_worker_unlock();

done:
	if (gate_locked)
		psp_me_sound_ym_gate_unlock();
	return result;
}

bool mvs_me_sound_shadow_z80_slice_begin(uint64_t horizon_time,
	uint32_t scheduler_time_left)
{
	bool active = __atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE);
	bool result = false;
	bool suppress_cpu = false;

	me_sound_z80_slice_gate_locked = false;
	me_sound_z80_horizon_queued = false;
	if (me_sound_cpu_recovery_required)
	{
		if (!psp_me_sound_recover_cpu())
			return true;
		active = false;
	}
	if (active)
	{
		if (!psp_me_sound_ym_gate_lock())
		{
			me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_GATE_LOCK;
			psp_me_sound_z80_mark_failed("Z80/YM gate lock");
			if (!psp_me_sound_recover_cpu())
				return true;
			active = false;
		}
		else
			me_sound_z80_slice_gate_locked = true;
	}
	me_sound_z80_io_count = 0;
	me_sound_z80_io_overflow = false;
	__atomic_store_n(&me_sound_z80_collecting,
		active && !me_sound_z80_autonomous, __ATOMIC_RELEASE);
	if (!active || !me_sound_z80_autonomous)
		return false;

	me_sound_z80_slice_count++;
	if (!psp_me_sound_worker_lock())
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_SLICE_LOCK;
		psp_me_sound_z80_mark_failed("advance horizon lock");
		goto recover;
	}
	if (me_available && me_sound_worker.running &&
		__atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE))
		result = psp_me_sound_worker_z80_advance_horizon(&me_sound_worker,
			horizon_time, scheduler_time_left);
	psp_me_sound_worker_unlock();
	if (!result)
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_ADVANCE_SEND;
		psp_me_sound_z80_mark_failed("autonomous horizon send");
		goto recover;
	}
	me_sound_z80_horizon_queued = true;
	if (me_sound_z80_control_authoritative)
	{
		me_sound_z80_owned_slices++;
		suppress_cpu = true;
	}
	return suppress_cpu;

recover:
	if (me_sound_z80_slice_gate_locked)
	{
		me_sound_z80_slice_gate_locked = false;
		psp_me_sound_ym_gate_unlock();
	}
	if (me_sound_cpu_recovery_required && !psp_me_sound_recover_cpu())
		return true;
	return false;
}

static void psp_me_sound_z80_record_io(uint16_t port, uint8_t type, uint8_t value)
{
	psp_me_sound_z80_io_t *entry;

	if (!__atomic_load_n(&me_sound_z80_collecting, __ATOMIC_ACQUIRE))
		return;
	if (me_sound_z80_io_count >= PSP_ME_SOUND_Z80_IO_CAPACITY)
	{
		me_sound_z80_io_overflow = true;
		if (me_sound_z80_io_count != UINT32_MAX)
			me_sound_z80_io_count++;
		if (me_sound_z80_io_count > me_sound_z80_io_peak)
			me_sound_z80_io_peak = me_sound_z80_io_count;
		return;
	}
	entry = &me_sound_z80_io[me_sound_z80_io_count++];
	entry->port = port;
	entry->type = type;
	entry->value = value;
	if (me_sound_z80_io_count > me_sound_z80_io_peak)
		me_sound_z80_io_peak = me_sound_z80_io_count;
}

void mvs_me_sound_shadow_z80_io_read(uint16_t port, uint8_t value)
{
	psp_me_sound_z80_record_io(port, PSP_ME_SOUND_Z80_IO_READ, value);
}

void mvs_me_sound_shadow_z80_io_write(uint16_t port, uint8_t value)
{
	psp_me_sound_z80_record_io(port, PSP_ME_SOUND_Z80_IO_WRITE, value);
}

void mvs_me_sound_shadow_z80_preempt(uint32_t timer_channel)
{
	if (timer_channel > UINT16_MAX)
	{
		me_sound_z80_io_overflow = true;
		return;
	}
	psp_me_sound_z80_record_io((uint16_t)timer_channel,
		PSP_ME_SOUND_Z80_IO_PREEMPT, 0);
}

void mvs_me_sound_shadow_z80_irq(int32_t state, uint64_t emulated_time)
{
	bool result = true;

	if (!__atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE))
		return;
	if (me_sound_z80_autonomous)
		return;
	if (__atomic_load_n(&me_sound_z80_collecting, __ATOMIC_ACQUIRE))
	{
		psp_me_sound_z80_record_io(0, PSP_ME_SOUND_Z80_IO_IRQ, (uint8_t)state);
		return;
	}
	if (!psp_me_sound_worker_lock())
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_IRQ_LOCK;
		psp_me_sound_z80_mark_failed("IRQ lock");
		return;
	}
	if (me_available && me_sound_worker.running &&
		__atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE))
		result = psp_me_sound_worker_z80_irq(&me_sound_worker, state, emulated_time);
	psp_me_sound_worker_unlock();
	if (!result)
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_IRQ_SEND;
		psp_me_sound_z80_mark_failed("IRQ send");
	}
}

void mvs_me_sound_shadow_ym_timer(uint32_t channel, uint64_t emulated_time)
{
	bool result = true;

	me_sound_ym_timer_gate_locked = false;
	if (!__atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE))
		return;
	if (!me_sound_z80_slice_gate_locked)
	{
		if (!psp_me_sound_ym_gate_lock())
		{
			me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_GATE_LOCK;
			psp_me_sound_z80_mark_failed("YM timer gate lock");
			return;
		}
		me_sound_ym_timer_gate_locked = true;
	}
	if (me_sound_z80_autonomous)
		return;
	if (!psp_me_sound_worker_lock())
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_TIMER_LOCK;
		psp_me_sound_z80_mark_failed("YM timer lock");
		return;
	}
	if (me_available && me_sound_worker.running &&
		__atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE))
		result = psp_me_sound_worker_ym_timer(&me_sound_worker, channel,
			emulated_time);
	psp_me_sound_worker_unlock();
	if (!result)
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_TIMER_SEND;
		psp_me_sound_z80_mark_failed("YM timer send");
	}
}

void mvs_me_sound_shadow_ym_timer_completed(void)
{
	if (me_sound_ym_timer_gate_locked)
	{
		me_sound_ym_timer_gate_locked = false;
		psp_me_sound_ym_gate_unlock();
	}
}

bool mvs_me_sound_shadow_ym_render_begin(uint32_t samples, uint64_t emulated_time)
{
	ym2610_pcm_window_t window;
	bool result = false;

	if (!me_available || !me_sound_worker.running)
		return false;
	if (!psp_me_sound_ym_gate_lock())
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_GATE_LOCK;
		psp_me_sound_z80_mark_failed("YM render gate lock");
		return false;
	}
	me_sound_ym_render_gate_locked = true;
	if (!__atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE))
		goto inactive;
	if (samples == 0 || samples > PSP_ME_SOUND_YM_RENDER_MAX_SAMPLES)
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_RENDER_PREPARE;
		psp_me_sound_z80_mark_failed("YM render sample count");
		goto fail;
	}
	if (!psp_me_sound_worker_lock())
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_RENDER_LOCK;
		psp_me_sound_z80_mark_failed("YM render prepare lock");
		goto fail;
	}
	if (me_available && me_sound_worker.running &&
		__atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE))
	{
		result = psp_me_sound_worker_ym_render_prepare(&me_sound_worker,
			samples, emulated_time, &window, PSP_ME_SOUND_WORKER_TIMEOUT_US);
	}
	psp_me_sound_worker_unlock();
	if (!result)
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_RENDER_PREPARE;
		psp_me_sound_z80_mark_failed("YM render prepare");
		goto fail;
	}
	if (!YM2610DefaultFillPcmWindow(&window))
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_RENDER_PREPARE;
		psp_me_sound_z80_mark_failed("YM PCM window fill");
		goto fail;
	}

	result = false;
	if (!psp_me_sound_worker_lock())
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_RENDER_LOCK;
		psp_me_sound_z80_mark_failed("YM render lock");
		goto fail;
	}
	if (me_available && me_sound_worker.running &&
		__atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE))
	{
		result = psp_me_sound_worker_ym_render_begin(&me_sound_worker, &window,
			emulated_time, PSP_ME_SOUND_WORKER_TIMEOUT_US);
	}
	psp_me_sound_worker_unlock();
	if (!result)
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_RENDER_SEND;
		psp_me_sound_z80_mark_failed("YM render send");
		goto fail;
	}
	me_sound_ym_render_pending = true;
	return true;

inactive:
	me_sound_ym_render_gate_locked = false;
	psp_me_sound_ym_gate_unlock();
	return false;

fail:
	if (me_sound_ym_render_gate_locked)
	{
		me_sound_ym_render_gate_locked = false;
		psp_me_sound_ym_gate_unlock();
	}
	return false;
}

bool mvs_me_sound_shadow_ym_authoritative(void)
{
	return me_sound_ym_authoritative && me_sound_z80_autonomous &&
		__atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE);
}

bool mvs_me_sound_shadow_ym_render_completed_authoritative(int32_t **buffer,
	uint32_t samples)
{
	bool result = false;

	if (!me_sound_ym_render_pending)
		return false;
	me_sound_ym_render_pending = false;
	if (!buffer || !buffer[0] || !buffer[1] || !psp_me_sound_worker_lock())
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_RENDER_LOCK;
		psp_me_sound_z80_mark_failed("authoritative YM render completion lock");
		goto done;
	}
	if (me_available && me_sound_worker.running &&
		mvs_me_sound_shadow_ym_authoritative())
	{
		result = psp_me_sound_worker_ym_render_finish_authoritative(
			&me_sound_worker, buffer[0], buffer[1], samples,
			PSP_ME_SOUND_WORKER_TIMEOUT_US);
	}
	psp_me_sound_worker_unlock();
	if (!result)
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_CONTEXT_SYNC;
		psp_me_sound_z80_mark_failed("authoritative YM render/context sync");
	}

done:
	if (me_sound_ym_render_gate_locked)
	{
		me_sound_ym_render_gate_locked = false;
		psp_me_sound_ym_gate_unlock();
	}
	return result;
}

void mvs_me_sound_shadow_ym_render_completed(int32_t **buffer, uint32_t samples,
	uint8_t status_b)
{
	bool result = false;

	if (!me_sound_ym_render_pending)
		return;
	me_sound_ym_render_pending = false;
	if (!buffer || !buffer[0] || !buffer[1] || !psp_me_sound_worker_lock())
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_RENDER_LOCK;
		psp_me_sound_z80_mark_failed("YM render completion lock");
		goto done;
	}
	if (me_available && me_sound_worker.running)
	{
		result = psp_me_sound_worker_ym_render_finish_present(&me_sound_worker,
			buffer[0], buffer[1], buffer[0], buffer[1], samples, status_b,
			PSP_ME_SOUND_WORKER_TIMEOUT_US);
	}
	psp_me_sound_worker_unlock();
	if (!result)
	{
		me_sound_z80_failure_reason = PSP_ME_SOUND_Z80_LOCAL_FAILURE_YM_RENDER_COMPARE;
		psp_me_sound_z80_mark_failed("YM PCM/status compare");
	}

done:
	if (me_sound_ym_render_gate_locked)
	{
		me_sound_ym_render_gate_locked = false;
		psp_me_sound_ym_gate_unlock();
	}
}

void mvs_me_sound_shadow_z80_slice_completed(uint64_t emulated_time)
{
	__atomic_store_n(&me_sound_z80_collecting, false, __ATOMIC_RELEASE);
	if (me_sound_z80_horizon_queued &&
		__atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE) &&
		me_sound_z80_autonomous)
		me_sound_status_required_time = emulated_time;
	me_sound_z80_horizon_queued = false;
	if (me_sound_z80_slice_gate_locked)
	{
		me_sound_z80_slice_gate_locked = false;
		psp_me_sound_ym_gate_unlock();
	}
}

bool mvs_me_sound_shadow_checkpoint_due(void)
{
	return !me_sound_z80_control_authoritative && me_sound_z80_autonomous &&
		__atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE) &&
		__atomic_load_n(&me_sound_shadow_window_frames, __ATOMIC_RELAXED) >= 299u;
}

void mvs_me_sound_shadow_frame_completed(uint64_t emulated_time,
	uint8_t sound_code, uint8_t pending_command, uint8_t result_code,
	const cz80_state_t *expected_state, const uint32_t banks[4],
	const uint8_t *visible_memory)
{
	uint32_t frames = __atomic_add_fetch(&me_sound_shadow_window_frames, 1u,
		__ATOMIC_RELAXED);
	bool pending = __atomic_load_n(&me_sound_shadow_pending_hint, __ATOMIC_ACQUIRE);
	bool z80_active = __atomic_load_n(&me_sound_z80_active, __ATOMIC_ACQUIRE);

	if (!pending && frames < 300u)
		return;
	if (!psp_me_sound_worker_lock())
	{
		if (frames >= 300u)
			__atomic_store_n(&me_sound_shadow_window_frames, 0, __ATOMIC_RELAXED);
		return;
	}
	if (me_available && me_sound_worker.running)
	{
		if (pending && !psp_me_sound_worker_poll(&me_sound_worker))
			psp_me_sound_shadow_mark_failed("echo mismatch");
		if (frames >= 300u && z80_active)
		{
			psp_me_sound_status_snapshot_t status;
			bool checkpoint_ok = true;

			if (!psp_me_sound_worker_sync(&me_sound_worker, emulated_time,
					PSP_ME_SOUND_WORKER_TIMEOUT_US))
			{
				me_sound_z80_failure_reason =
					PSP_ME_SOUND_Z80_LOCAL_FAILURE_CHECKPOINT_SYNC;
				psp_me_sound_z80_mark_failed("checkpoint sync");
				psp_me_sound_shadow_mark_failed("checkpoint sync");
			}
			else
			{
				if (me_sound_z80_control_authoritative)
				{
					me_sound_status_checks++;
					if (!psp_me_sound_worker_read_status(&me_sound_worker, &status) ||
						status.emulated_time != emulated_time ||
						status.z80_time != emulated_time)
					{
						me_sound_status_mismatches++;
						me_sound_z80_failure_reason =
							PSP_ME_SOUND_Z80_LOCAL_FAILURE_STATUS_SNAPSHOT;
						psp_me_sound_z80_mark_failed("authoritative frame status");
					}
				}
				else if (me_sound_z80_autonomous)
				{
					if (!expected_state || !banks || !visible_memory ||
						!psp_me_sound_worker_z80_checkpoint(&me_sound_worker,
							expected_state, banks,
							psp_me_sound_z80_ram_hash(visible_memory), emulated_time,
							PSP_ME_SOUND_WORKER_TIMEOUT_US))
					{
						checkpoint_ok = false;
						me_sound_z80_failure_reason =
							PSP_ME_SOUND_Z80_LOCAL_FAILURE_CHECKPOINT;
						psp_me_sound_z80_mark_failed("autonomous checkpoint");
					}
				}
				if (checkpoint_ok && !me_sound_z80_control_authoritative)
					me_sound_status_checks++;
				if (checkpoint_ok && !me_sound_z80_control_authoritative &&
					(!psp_me_sound_worker_read_status(&me_sound_worker, &status) ||
					status.emulated_time != emulated_time ||
					status.sound_code != sound_code ||
					status.pending_command != pending_command ||
					status.result_code != result_code))
				{
					me_sound_status_mismatches++;
					me_sound_z80_failure_reason =
						PSP_ME_SOUND_Z80_LOCAL_FAILURE_STATUS_SNAPSHOT;
					psp_me_sound_z80_mark_failed("status snapshot");
				}
			}
		}
		__atomic_store_n(&me_sound_shadow_pending_hint,
			me_sound_worker.shadow_expected_count != 0, __ATOMIC_RELEASE);
		if (frames >= 300u)
			psp_me_sound_shadow_log_window("window", false);
	}
	else if (frames >= 300u)
		__atomic_store_n(&me_sound_shadow_window_frames, 0, __ATOMIC_RELAXED);
	psp_me_sound_worker_unlock();
}

static bool psp_me_sound_worker_bootstrap(void)
{
	const psp_me_sound_worker_dispatch_t dispatch = {
		psp_me_sound_worker_dispatch_start,
		psp_me_sound_worker_dispatch_wait,
		NULL,
	};
	bool result = false;

	psp_me_sound_z80_reset_tracking();
	if (!psp_me_sound_worker_lock())
		return false;
	if (!psp_me_sound_worker_start(&me_sound_worker, &dispatch,
		PSP_ME_SOUND_WORKER_CAPACITY, PSP_ME_SOUND_WORKER_TIMEOUT_US))
		goto done;

	me_sound_worker_generation++;
	if (me_sound_worker_generation == 0)
		me_sound_worker_generation = 1;
	if (!psp_me_sound_worker_reset(&me_sound_worker, me_sound_worker_generation,
		PSP_ME_SOUND_WORKER_TIMEOUT_US))
	{
		psp_me_sound_worker_abort(&me_sound_worker);
		goto done;
	}
	psp_me_sound_worker_get_stats(&me_sound_worker, &me_sound_shadow_window_base);
	__atomic_store_n(&me_sound_shadow_window_frames, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&me_sound_shadow_pending_hint, false, __ATOMIC_RELEASE);
	result = true;

done:
	psp_me_sound_worker_unlock();
	return result;
}

static void psp_me_sound_worker_stop(void)
{
	if (!psp_me_sound_worker_lock())
		return;
	if (me_sound_worker.running)
	{
		if (!psp_me_sound_worker_shutdown(&me_sound_worker,
			PSP_ME_SOUND_WORKER_TIMEOUT_US))
			psp_me_sound_worker_abort(&me_sound_worker);
		psp_me_sound_shadow_log_window("stop", true);
	}
	__atomic_store_n(&me_sound_shadow_pending_hint, false, __ATOMIC_RELEASE);
	psp_me_sound_z80_reset_tracking();
	psp_me_sound_worker_unlock();
}

static bool psp_me_sound_worker_reset_generation(void)
{
	bool result = false;

	if (!psp_me_sound_worker_lock())
		return false;
	if (!me_sound_worker.running)
		goto done;
	psp_me_sound_shadow_log_window("reset", true);
	psp_me_sound_z80_reset_tracking();
	me_sound_worker_generation++;
	if (me_sound_worker_generation == 0)
		me_sound_worker_generation = 1;
	result = psp_me_sound_worker_reset(&me_sound_worker,
		me_sound_worker_generation, PSP_ME_SOUND_WORKER_TIMEOUT_US);
	if (!result)
		psp_me_sound_worker_abort(&me_sound_worker);
	else
	{
		psp_me_sound_worker_get_stats(&me_sound_worker, &me_sound_shadow_window_base);
		__atomic_store_n(&me_sound_shadow_window_frames, 0, __ATOMIC_RELAXED);
		__atomic_store_n(&me_sound_shadow_pending_hint, false, __ATOMIC_RELEASE);
	}

done:
	psp_me_sound_worker_unlock();
	return result;
}

#endif /* PSP_ME_SOUND_COPROCESSOR */

	#ifdef PSP_ME_RING_SELFTEST

static bool psp_me_ring_selftest_dispatch_start(void (*task)(void *), void *data,
	uint32_t size, void *opaque)
{
	(void)opaque;
	if (!task || !data || size == 0 || me_job_in_flight)
		return false;

	me_job.job = task;
	me_job.data = data;
	me_job.size = size;
	sceKernelDcacheWritebackInvalidateRange(&me_job, sizeof(me_job));
	return psp_me_dispatch_job() >= 0;
}

static void psp_me_ring_selftest_dispatch_wait(void *opaque)
{
	(void)opaque;
	psp_me_dispatch_wait();
}

static void psp_me_ring_selftest_log(
	const psp_me_spsc_ring_mist_result_t *result)
{
	char path[1024];
	char line[1024];
	uint64_t latency_ns =
		(result->latency_us * 1000ULL) / PSP_ME_SPSC_RING_MIST_LATENCY_MESSAGES;
	uint64_t bulk_roundtrips_per_sec = result->bulk_us ?
		((uint64_t)PSP_ME_SPSC_RING_MIST_BULK_MESSAGES * 1000000ULL) /
			result->bulk_us : 0;
	int fd;
	int length;

	length = snprintf(line, sizeof(line),
		"[psp-me-ring] latency_messages=%u latency_us=%llu latency_avg_ns=%llu "
		"bulk_messages=%u bulk_us=%llu bulk_roundtrips_per_sec=%llu completed=%lu "
		"to_me_high_water=%lu to_me_overflow=%lu to_me_underflow=%lu "
		"to_me_seqerr=%lu to_me_corrupt=%lu "
		"to_main_high_water=%lu to_main_overflow=%lu to_main_underflow=%lu "
		"to_main_seqerr=%lu to_main_corrupt=%lu error=%lu\n",
		PSP_ME_SPSC_RING_MIST_LATENCY_MESSAGES,
		(unsigned long long)result->latency_us,
		(unsigned long long)latency_ns,
		PSP_ME_SPSC_RING_MIST_BULK_MESSAGES,
		(unsigned long long)result->bulk_us,
		(unsigned long long)bulk_roundtrips_per_sec,
		(unsigned long)result->completed,
		(unsigned long)result->to_me_high_water,
		(unsigned long)result->to_me_overflow,
		(unsigned long)result->to_me_underflow,
		(unsigned long)result->to_me_sequence_errors,
		(unsigned long)result->to_me_corrupt,
		(unsigned long)result->to_main_high_water,
		(unsigned long)result->to_main_overflow,
		(unsigned long)result->to_main_underflow,
		(unsigned long)result->to_main_sequence_errors,
		(unsigned long)result->to_main_corrupt,
		(unsigned long)result->error);
	if (length <= 0 || (size_t)length >= sizeof(line))
		return;

	printf("%s", line);
	snprintf(path, sizeof(path), "%spsp_me_ring_selftest.log", launchDir);
	fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd >= 0)
	{
		write(fd, line, (size_t)length);
		close(fd);
	}
}

static bool psp_me_ring_selftest(void)
{
	const psp_me_spsc_ring_mist_dispatch_t dispatch = {
		psp_me_ring_selftest_dispatch_start,
		psp_me_ring_selftest_dispatch_wait,
		NULL,
	};
	psp_me_spsc_ring_mist_result_t result;

	if (me_ring_selftest_attempted)
		return me_ring_selftest_passed;
	me_ring_selftest_attempted = true;
	me_ring_selftest_passed = psp_me_spsc_ring_mist_test_run(&dispatch, &result);
	psp_me_ring_selftest_log(&result);
	return me_ring_selftest_passed;
}

#endif /* PSP_ME_RING_SELFTEST */

static bool psp_me_probe(void)
{
	int result;

	memset(me_probe_data, 0, sizeof(me_probe_data));
	me_probe_data[0] = PSP_ME_PROBE_A;
	me_probe_data[1] = PSP_ME_PROBE_B;
	me_job.job = psp_me_probe_task;
	me_job.data = me_probe_data;
	me_job.size = sizeof(me_probe_data);
	sceKernelDcacheWritebackInvalidateRange(&me_job, sizeof(me_job));
	sceKernelDcacheWritebackInvalidateRange(me_probe_data, sizeof(me_probe_data));

	result = psp_me_dispatch_job();
	if (result < 0)
		return false;

	psp_me_dispatch_wait();
	sceKernelDcacheInvalidateRange(me_probe_data, sizeof(me_probe_data));

	return me_probe_data[2] == (PSP_ME_PROBE_A ^ PSP_ME_PROBE_B) &&
		me_probe_data[3] == (PSP_ME_PROBE_A + PSP_ME_PROBE_B);
}

static bool psp_me_enable(const char *context)
{
	int result;

	if (!psp_me_mode_enabled())
		return false;
#ifdef PSP_ME_SOUND_COPROCESSOR
	if (!me_sound_worker_mutex_ready || !me_sound_ym_gate_ready)
	{
		printf("[PSP_ME_AUDIO] %s: sound worker synchronization unavailable; using Main CPU\n",
			context);
		return false;
	}
#endif

	result = psp_me_dispatch_init();
	if (result < 0)
	{
		printf("[PSP_ME_AUDIO] %s: %s requested, but ME dispatcher is unavailable (%d); using Main CPU\n",
			context, psp_me_mode_name(), result);
		me_available = false;
		return false;
	}
	if (!psp_me_probe())
	{
		printf("[PSP_ME_AUDIO] %s: %s requested, but ME execution probe failed; using Main CPU\n",
			context, psp_me_mode_name());
		me_available = false;
		return false;
	}
#ifdef PSP_ME_RING_SELFTEST
	if (!psp_me_ring_selftest())
	{
		printf("[PSP_ME_AUDIO] %s: shared-ring self-test failed; using Main CPU\n",
			context);
		me_available = false;
		return false;
	}
	#endif
	#ifdef PSP_ME_SOUND_COPROCESSOR
	if (!psp_me_sound_worker_bootstrap())
	{
		printf("[PSP_ME_AUDIO] %s: persistent sound worker bootstrap failed; using Main CPU\n",
			context);
		me_available = false;
		return false;
	}
	#endif

	me_available = true;
	printf("[PSP_ME_AUDIO] %s: %s -> Media Engine (MIST); Main CPU retained as fallback\n",
		context, psp_me_mode_name());
	return true;
}

static bool psp_audio_producer_init(void)
{
	me_available = false;
	me_suspended = false;
	me_job_in_flight = false;
	me_job_data = NULL;
	me_job_cache_size = 0;
	me_workspace = NULL;
	me_workspace_size = 0;
#ifdef PSP_ME_SOUND_COPROCESSOR
	memset(&me_sound_worker, 0, sizeof(me_sound_worker));
	memset(&me_sound_worker_mutex, 0, sizeof(me_sound_worker_mutex));
	memset(&me_sound_ym_gate, 0, sizeof(me_sound_ym_gate));
	memset(&me_sound_shadow_window_base, 0, sizeof(me_sound_shadow_window_base));
	me_sound_worker_generation = 0;
	__atomic_store_n(&me_sound_shadow_window_frames, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&me_sound_shadow_pending_hint, false, __ATOMIC_RELEASE);
	me_sound_shadow_failed = false;
	me_sound_z80_slice_gate_locked = false;
	me_sound_ym_timer_gate_locked = false;
	me_sound_ym_render_gate_locked = false;
	me_sound_worker_mutex_ready = sceKernelCreateLwMutex(&me_sound_worker_mutex,
		"NJEMU ME sound worker", 0, 0, NULL) >= 0;
	me_sound_ym_gate_ready = sceKernelCreateLwMutex(&me_sound_ym_gate,
		"NJEMU ME sound YM gate", 0, 0, NULL) >= 0;
	{
		char path[1024];
		snprintf(path, sizeof(path), "%spsp_me_sound_shadow.log", launchDir);
		remove(path);
	}
#endif
	if (!audio_producer_cpu.init())
	{
#ifdef PSP_ME_SOUND_COPROCESSOR
		if (me_sound_ym_gate_ready)
		{
			(void)sceKernelDeleteLwMutex(&me_sound_ym_gate);
			me_sound_ym_gate_ready = false;
		}
		if (me_sound_worker_mutex_ready)
		{
			(void)sceKernelDeleteLwMutex(&me_sound_worker_mutex);
			me_sound_worker_mutex_ready = false;
		}
#endif
		return false;
	}
	if (!psp_me_mode_enabled())
	{
		printf("[PSP_ME_AUDIO] Audio processor: Main CPU; ME initialization skipped\n");
		return true;
	}
	psp_me_enable("startup");

	return true;
}

static void psp_audio_producer_shutdown(void)
{
#ifdef PSP_ME_SOUND_COPROCESSOR
	psp_me_sound_worker_stop();
	if (me_sound_ym_gate_ready)
	{
		(void)sceKernelDeleteLwMutex(&me_sound_ym_gate);
		me_sound_ym_gate_ready = false;
	}
	if (me_sound_worker_mutex_ready)
	{
		(void)sceKernelDeleteLwMutex(&me_sound_worker_mutex);
		me_sound_worker_mutex_ready = false;
	}
#endif
	if (me_job_in_flight)
	{
		psp_me_dispatch_wait();
		if (me_job_data && me_job_cache_size)
			sceKernelDcacheInvalidateRange(me_job_data, me_job_cache_size);
		me_job_in_flight = false;
	}
	me_available = false;
	me_suspended = false;
	me_job_data = NULL;
	me_job_cache_size = 0;
	free(me_workspace);
	me_workspace = NULL;
	me_workspace_size = 0;
	audio_producer_cpu.shutdown();
}

static void psp_audio_producer_reset(void)
{
	psp_audio_producer_waitJob();
	if (!psp_me_mode_enabled())
		me_available = false;
#ifdef PSP_ME_SOUND_COPROCESSOR
	else if (me_available)
	{
		if (!psp_me_sound_worker_reset_generation())
			me_available = false;
	}
#endif
	else if (!me_available && !me_suspended)
		psp_me_enable("reset");
	audio_producer_cpu.reset();
}

static void psp_audio_producer_suspend(void)
{
	psp_audio_producer_waitJob();
#ifdef PSP_ME_SOUND_COPROCESSOR
	psp_me_sound_worker_stop();
#endif
	me_available = false;
	me_suspended = true;
}

static void psp_audio_producer_resume(void)
{
	if (!me_suspended)
		return;

	me_suspended = false;
	if (psp_me_mode_enabled())
		psp_me_enable("resume");
}

static void psp_audio_producer_render(audio_producer_render_fn cpu_render,
	int16_t *buffer)
{
	audio_producer_cpu.render(cpu_render, buffer);
}

static bool psp_audio_producer_isAvailable(void)
{
	return me_available;
}

static bool psp_audio_producer_canRunJobs(void)
{
#ifdef PSP_ME_SOUND_COPROCESSOR
	/* C2 reserves MIST for the persistent worker.  Until that worker owns the
	 * complete sound island, YM2610 production intentionally stays on Allegrex. */
	return false;
#else
	return me_available;
#endif
}

static void *psp_audio_producer_acquireJobBuffer(uint32_t size, uint32_t alignment)
{
#ifdef PSP_ME_SOUND_COPROCESSOR
	(void)size;
	(void)alignment;
	return NULL;
#else
	void *workspace;
	uint32_t allocation_size;

	if (!me_available || size == 0 || alignment == 0 ||
		(alignment & (alignment - 1u)) != 0)
		return NULL;
	if (size > UINT32_MAX - (alignment - 1u))
		return NULL;
	allocation_size = (size + alignment - 1u) & ~(alignment - 1u);
	if (me_workspace && me_workspace_size >= allocation_size)
		return me_workspace;

	workspace = memalign(alignment, allocation_size);
	if (!workspace)
		return NULL;

	free(me_workspace);
	me_workspace = workspace;
	me_workspace_size = allocation_size;
	return me_workspace;
#endif
}

static bool psp_audio_producer_submitJob(audio_producer_job_fn job, void *data,
	uint32_t size)
{
#ifdef PSP_ME_SOUND_COPROCESSOR
	(void)job;
	(void)data;
	(void)size;
	return false;
#else
	uint32_t cache_size;
	int result;

	if (!me_available)
		return false;
	if (me_job_in_flight || !job || !data || size == 0)
		return false;
	if (size > UINT32_MAX - 63u)
		return false;
	cache_size = (size + 63u) & ~63u;
	if (data != me_workspace || me_workspace_size < cache_size)
		return false;
	me_job.job = job;
	me_job.data = data;
	me_job.size = cache_size;
	me_job_data = data;
	me_job_cache_size = cache_size;
	sceKernelDcacheWritebackInvalidateRange(&me_job, sizeof(me_job));
	sceKernelDcacheWritebackInvalidateRange(data, cache_size);

	result = psp_me_dispatch_job();
	if (result < 0)
	{
		me_job_data = NULL;
		me_job_cache_size = 0;
		return false;
	}

	me_job_in_flight = true;
	return true;
#endif
}

static void psp_audio_producer_waitJob(void)
{
	if (!me_job_in_flight)
		return;
	psp_me_dispatch_wait();
	if (me_job_data && me_job_cache_size)
		sceKernelDcacheInvalidateRange(me_job_data, me_job_cache_size);
	me_job_in_flight = false;
	me_job_data = NULL;
	me_job_cache_size = 0;
}

static const audio_producer_driver_t audio_producer_psp = {
	"psp-me",
	psp_audio_producer_init,
	psp_audio_producer_shutdown,
	psp_audio_producer_reset,
	psp_audio_producer_suspend,
	psp_audio_producer_resume,
	psp_audio_producer_render,
	psp_audio_producer_isAvailable,
	psp_audio_producer_canRunJobs,
	psp_audio_producer_acquireJobBuffer,
	psp_audio_producer_submitJob,
	psp_audio_producer_waitJob,
};

const audio_producer_driver_t *const audio_producer_driver = &audio_producer_psp;
