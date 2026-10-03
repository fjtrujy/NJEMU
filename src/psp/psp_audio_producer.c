#include <stdio.h>
#include <malloc.h>
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
#include "mvs/me_sound_shadow.h"
#include "psp/psp_me_sound_worker.h"
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
static uint32_t me_sound_worker_generation;
static uint32_t me_sound_shadow_window_frames;
static psp_me_sound_worker_stats_t me_sound_shadow_window_base;
static bool me_sound_worker_mutex_ready;
static bool me_sound_shadow_pending_hint;
static bool me_sound_shadow_failed;
#endif

static void psp_audio_producer_waitJob(void);

#define PSP_ME_PROBE_A 0x13579bdfu
#define PSP_ME_PROBE_B 0x2468ace0u

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

#define PSP_ME_SOUND_WORKER_CAPACITY 8u
#define PSP_ME_SOUND_WORKER_TIMEOUT_US 2000000ULL

static bool psp_me_sound_worker_lock(void)
{
	return me_sound_worker_mutex_ready &&
		sceKernelLockLwMutex(&me_sound_worker_mutex, 1, NULL) >= 0;
}

static void psp_me_sound_worker_unlock(void)
{
	if (me_sound_worker_mutex_ready)
		(void)sceKernelUnlockLwMutex(&me_sound_worker_mutex, 1);
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

static void psp_me_sound_shadow_log_window(const char *reason, bool force)
{
	psp_me_sound_worker_stats_t stats;
	char path[1024];
	char line[1024];
	uint32_t frames;
	uint32_t sent;
	uint32_t matched;
	uint32_t processed;
	uint32_t mismatches;
	uint32_t send_failures;
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
	length = snprintf(line, sizeof(line),
		"[psp-me-shadow] reason=%s generation=%lu frames=%lu sent=%lu matched=%lu "
		"mismatches=%lu send_failures=%lu pending=%lu pending_high_water=%lu "
		"me_processed=%lu command_high_water=%lu command_overflow=%lu "
		"event_high_water=%lu event_overflow=%lu fatal=%lu emulated_time=%llu\n",
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
		(unsigned long)stats.fatal_error,
		(unsigned long long)stats.emulated_time);
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
	__atomic_store_n(&me_sound_shadow_window_frames, 0, __ATOMIC_RELAXED);
}

bool mvs_me_sound_shadow_command(uint8_t command, uint64_t emulated_time)
{
	bool result = false;

	if (!psp_me_sound_worker_lock())
		return false;
	if (me_available && me_sound_worker.running)
	{
		result = psp_me_sound_worker_shadow_sound(&me_sound_worker, command,
			emulated_time);
		if (result)
			__atomic_store_n(&me_sound_shadow_pending_hint, true, __ATOMIC_RELEASE);
		else
			psp_me_sound_shadow_mark_failed("send/poll");
	}
	psp_me_sound_worker_unlock();
	return result;
}

void mvs_me_sound_shadow_frame_completed(void)
{
	uint32_t frames = __atomic_add_fetch(&me_sound_shadow_window_frames, 1u,
		__ATOMIC_RELAXED);
	bool pending = __atomic_load_n(&me_sound_shadow_pending_hint, __ATOMIC_ACQUIRE);

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
	if (!me_sound_worker_mutex_ready)
	{
		printf("[PSP_ME_AUDIO] %s: sound worker mutex unavailable; using Main CPU\n",
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
	memset(&me_sound_shadow_window_base, 0, sizeof(me_sound_shadow_window_base));
	me_sound_worker_generation = 0;
	__atomic_store_n(&me_sound_shadow_window_frames, 0, __ATOMIC_RELAXED);
	__atomic_store_n(&me_sound_shadow_pending_hint, false, __ATOMIC_RELEASE);
	me_sound_shadow_failed = false;
	me_sound_worker_mutex_ready = sceKernelCreateLwMutex(&me_sound_worker_mutex,
		"NJEMU ME sound worker", 0, 1, NULL) >= 0;
	{
		char path[1024];
		snprintf(path, sizeof(path), "%spsp_me_sound_shadow.log", launchDir);
		remove(path);
	}
#endif
	if (!audio_producer_cpu.init())
		return false;
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
