#include <limits.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <pspkernel.h>
#include <me-core-mapper/hw-registers.h>
#include <me-safe-task/me-stask-mist.h>

#include "psp/psp_me_dispatch.h"

#ifdef PSP_ME_RING_SELFTEST
#include <fcntl.h>
#include <unistd.h>

#include "common/runtime_paths.h"
#include "psp/psp_me_spsc_ring_mist_test.h"
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
static uint32_t me_probe_data[16] __attribute__((aligned(64)));
static bool me_job_in_flight;
static void *me_job_data;
static uint32_t me_job_cache_size;
static void *me_workspace;
static uint32_t me_workspace_size;

#ifdef PSP_ME_RING_SELFTEST
static bool me_ring_selftest_attempted;
static bool me_ring_selftest_passed;
#endif

#define PSP_ME_PROBE_A 0x13579bdfu
#define PSP_ME_PROBE_B 0x2468ace0u
#define PSP_ME_MIST_SYSCALL_INDEX 13

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

static int psp_me_mist_job_entry(int index, void *param)
{
	(void)index;
	psp_me_audio_job_entry(param);
	return meSafeTaskMistFinish();
}

static int psp_me_dispatch_init_mist(void)
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

static bool psp_me_dispatch_probe(void)
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
		psp_me_dispatch_worker_start,
		psp_me_dispatch_worker_wait,
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

void psp_me_dispatch_reset_state(void)
{
	memset(&me_job, 0, sizeof(me_job));
	me_job_in_flight = false;
	me_job_data = NULL;
	me_job_cache_size = 0;
	me_workspace = NULL;
	me_workspace_size = 0;
}

void psp_me_dispatch_shutdown(void)
{
	psp_me_dispatch_wait_job();
	free(me_workspace);
	me_workspace = NULL;
	me_workspace_size = 0;
	memset(&me_job, 0, sizeof(me_job));
}

bool psp_me_dispatch_enable(const char *context, const char *mode_name)
{
	int result = psp_me_dispatch_init_mist();

	if (result < 0)
	{
		printf("[PSP_ME_AUDIO] %s: %s requested, but ME dispatcher is unavailable (%d); using Main CPU\n",
			context, mode_name, result);
		return false;
	}
	if (!psp_me_dispatch_probe())
	{
		printf("[PSP_ME_AUDIO] %s: %s requested, but ME execution probe failed; using Main CPU\n",
			context, mode_name);
		return false;
	}
#ifdef PSP_ME_RING_SELFTEST
	if (!psp_me_ring_selftest())
	{
		printf("[PSP_ME_AUDIO] %s: shared-ring self-test failed; using Main CPU\n",
			context);
		return false;
	}
#endif
	return true;
}

bool psp_me_dispatch_worker_start(void (*task)(void *), void *data,
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

void psp_me_dispatch_worker_wait(void *opaque)
{
	(void)opaque;
	psp_me_dispatch_wait();
}

void *psp_me_dispatch_acquire_job_buffer(uint32_t size, uint32_t alignment)
{
	void *workspace;
	uint32_t allocation_size;

	if (size == 0 || alignment == 0 || (alignment & (alignment - 1u)) != 0)
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
}

bool psp_me_dispatch_submit_job(audio_producer_job_fn job, void *data,
	uint32_t size)
{
	uint32_t cache_size;
	int result;

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
}

void psp_me_dispatch_wait_job(void)
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
