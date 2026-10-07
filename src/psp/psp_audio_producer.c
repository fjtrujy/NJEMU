#include <stdio.h>
#include <malloc.h>
#include <limits.h>
#include <string.h>
#include <pspkernel.h>
#include <me-safe-task/me-stask-mist.h>
#include <me-core-mapper/hw-registers.h>
#include "emucfg.h"
#include "common/audio_producer_driver.h"
#include "common/audio_profile.h"
#include "common/emulator_options.h"
#ifdef PSP_ME_RING_SELFTEST
#include <fcntl.h>
#include <unistd.h>
#include "common/runtime_paths.h"
#endif
#ifdef PSP_ME_SOUND_COPROCESSOR
#if (EMU_SYSTEM == CPS2)
#include "common/cps2_sound_offload.h"
#include "psp/psp_cps2_me_sound.h"
#else
#include "psp/psp_neogeo_me_sound.h"
#include "psp/psp_me_sound_lifecycle.h"
#endif
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
static void psp_audio_producer_waitJob(void);

#define PSP_ME_PROBE_A 0x13579bdfu
#define PSP_ME_PROBE_B 0x2468ace0u
static bool psp_me_mode_enabled(void)
{
	return option_audio_processor != AUDIO_PROCESSOR_MAIN_CPU;
}

#if defined(PSP_ME_SOUND_COPROCESSOR) && (EMU_SYSTEM != CPS2)
static bool psp_me_transport_available(void *opaque)
{
	(void)opaque;
	return me_available;
}
#endif

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

#if defined(PSP_ME_SOUND_COPROCESSOR) || defined(PSP_ME_RING_SELFTEST)

static bool psp_me_worker_dispatch_start(void (*task)(void *), void *data,
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

static void psp_me_worker_dispatch_wait(void *opaque)
{
	(void)opaque;
	psp_me_dispatch_wait();
}

#endif /* PSP_ME_SOUND_COPROCESSOR || PSP_ME_RING_SELFTEST */

#ifdef PSP_ME_RING_SELFTEST

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
		psp_me_worker_dispatch_start,
		psp_me_worker_dispatch_wait,
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
	#if (EMU_SYSTEM == CPS2)
	if (!psp_cps2_me_sound_sync_ready())
	{
		printf("[PSP_ME_AUDIO] %s: QSound worker synchronization unavailable; using Main CPU\n",
			context);
		return false;
	}
	if (psp_cps2_me_sound_running())
	{
		printf("[PSP_ME_AUDIO] %s: stopping stale QSound worker before ME bootstrap\n",
			context);
		psp_cps2_me_sound_stop();
		if (psp_cps2_me_sound_running())
		{
			printf("[PSP_ME_AUDIO] %s: stale QSound worker did not stop; using Main CPU\n",
				context);
			me_available = false;
			return false;
		}
	}
	#else
	if (!psp_neogeo_me_sound_sync_ready())
	{
		printf("[PSP_ME_AUDIO] %s: sound worker synchronization unavailable; using Main CPU\n",
			context);
		return false;
	}
	if (psp_neogeo_me_sound_running())
	{
		printf("[PSP_ME_AUDIO] %s: stopping stale sound worker before ME bootstrap\n",
			context);
		psp_neogeo_me_sound_stop();
		if (psp_neogeo_me_sound_running())
		{
			printf("[PSP_ME_AUDIO] %s: stale sound worker did not stop; using Main CPU\n",
				context);
			me_available = false;
			return false;
		}
	}
	#endif
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
	#if (EMU_SYSTEM == CPS2)
	{
		const psp_me_qsound_worker_dispatch_t dispatch = {
			psp_me_worker_dispatch_start,
			psp_me_worker_dispatch_wait,
			NULL,
		};

		if (!psp_cps2_me_sound_bootstrap(&dispatch))
		{
			printf("[PSP_ME_AUDIO] %s: persistent QSound worker bootstrap failed; using Main CPU\n",
				context);
			me_available = false;
			return false;
		}
	}
	#else
	{
		const psp_me_sound_worker_dispatch_t dispatch = {
			psp_me_worker_dispatch_start,
			psp_me_worker_dispatch_wait,
			NULL,
		};

		if (!psp_neogeo_me_sound_bootstrap(&dispatch))
		{
			printf("[PSP_ME_AUDIO] %s: persistent sound worker bootstrap failed; using Main CPU\n",
				context);
			me_available = false;
			return false;
		}
	}
	#endif
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
	#if (EMU_SYSTEM == CPS2)
	if (!psp_cps2_me_sound_sync_init())
		printf("[PSP_ME_AUDIO] QSound worker synchronization unavailable\n");
	#else
	if (!psp_neogeo_me_sound_sync_init(psp_me_transport_available, NULL))
		printf("[PSP_ME_AUDIO] sound worker synchronization unavailable\n");
	#endif
#endif
	if (!audio_producer_cpu.init())
	{
#ifdef PSP_ME_SOUND_COPROCESSOR
		#if (EMU_SYSTEM == CPS2)
			psp_cps2_me_sound_sync_shutdown();
		#else
		psp_neogeo_me_sound_sync_shutdown();
		#endif
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
	#if (EMU_SYSTEM == CPS2)
	psp_cps2_me_sound_stop();
	psp_cps2_me_sound_sync_shutdown();
	#else
	psp_neogeo_me_sound_stop();
	psp_neogeo_me_sound_sync_shutdown();
	#endif
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
#ifdef PSP_ME_SOUND_COPROCESSOR
	#if (EMU_SYSTEM == CPS2)
	if (!psp_me_mode_enabled())
	{
		psp_cps2_me_sound_stop();
		me_available = false;
	}
	else if (me_suspended)
		me_available = false;
	else if (psp_cps2_me_sound_running())
	{
		if (!psp_cps2_me_sound_reset_generation())
			me_available = false;
		else
			me_available = true;
	}
	else
	{
		me_available = false;
		(void)psp_me_enable("reset");
	}
	#else
		psp_me_sound_reset_action_t action = psp_me_sound_reset_action(
		psp_me_mode_enabled(), me_available, psp_neogeo_me_sound_running(), me_suspended);
	switch (action)
	{
	case PSP_ME_SOUND_RESET_STOP_WORKER:
		psp_neogeo_me_sound_stop();
		me_available = false;
		break;
	case PSP_ME_SOUND_RESET_RESET_WORKER:
		if (!psp_neogeo_me_sound_reset_generation())
			me_available = false;
		break;
	case PSP_ME_SOUND_RESET_START_WORKER:
		me_available = false;
		(void)psp_me_enable("reset");
		break;
	case PSP_ME_SOUND_RESET_RESTART_WORKER:
		psp_neogeo_me_sound_stop();
		me_available = false;
		(void)psp_me_enable("reset");
		break;
	case PSP_ME_SOUND_RESET_DEFER_SUSPENDED:
	case PSP_ME_SOUND_RESET_KEEP_CPU:
	default:
		me_available = false;
			break;
		}
	#endif
#else
	if (!psp_me_mode_enabled())
		me_available = false;
	else if (!me_available && !me_suspended)
		(void)psp_me_enable("reset");
#endif
	audio_producer_cpu.reset();
}

static void psp_audio_producer_suspend(void)
{
	psp_audio_producer_waitJob();
#ifdef PSP_ME_SOUND_COPROCESSOR
	#if (EMU_SYSTEM == CPS2)
	(void)cps2_sound_offload_prepare_cpu_state();
	psp_cps2_me_sound_stop();
	#else
		psp_neogeo_me_sound_stop();
	#endif
#endif
	me_available = false;
	me_suspended = true;
}

static void psp_audio_producer_resume(void)
{
	bool enabled;

	if (!me_suspended)
		return;

	me_suspended = false;
	if (!psp_me_mode_enabled())
		return;

	enabled = psp_me_enable("resume");
#if defined(PSP_ME_SOUND_COPROCESSOR) && (EMU_SYSTEM == CPS2)
	if (enabled && !cps2_sound_offload_snapshot_from_cpu())
	{
		printf("[PSP_ME_AUDIO] resume: CPS2 sound snapshot failed; using Main CPU\n");
		psp_cps2_me_sound_stop();
		me_available = false;
	}
#else
	(void)enabled;
#endif
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
	/* The persistent sound worker owns MIST, so bounded producer jobs cannot
	 * share the dispatcher while the full sound island is active. */
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
