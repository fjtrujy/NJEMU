#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <pspkernel.h>
#include <me-safe-task/me-stask.h>
#include "common/audio_producer_driver.h"

static bool me_available;
static bool me_module_loaded;
static bool me_job_in_flight;
static uint32_t me_probe_data[16] __attribute__((aligned(64)));

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

static void psp_me_probe_task(void *param)
{
	uint32_t *data = (uint32_t *)param;

	meCoreDcacheInvalidateRange(data, sizeof(me_probe_data));
	data[2] = data[0] ^ data[1];
	data[3] = data[0] + data[1];
	meCoreDcacheWritebackRange(data, sizeof(me_probe_data));
}

static bool psp_me_probe(void)
{
	Task task = {
		.func = psp_me_probe_task,
		.param = me_probe_data,
		.index = 0,
	};
	int result;

	memset(me_probe_data, 0, sizeof(me_probe_data));
	me_probe_data[0] = PSP_ME_PROBE_A;
	me_probe_data[1] = PSP_ME_PROBE_B;
	sceKernelDcacheWritebackInvalidateRange(me_probe_data, sizeof(me_probe_data));

	result = meSafeTaskDispatch(&task);
	if (result < 0)
		return false;

	meSafeTaskWaitReady();
	sceKernelDcacheInvalidateRange(me_probe_data, sizeof(me_probe_data));

	return me_probe_data[2] == (PSP_ME_PROBE_A ^ PSP_ME_PROBE_B) &&
		me_probe_data[3] == (PSP_ME_PROBE_A + PSP_ME_PROBE_B);
}

static void psp_me_audio_job_entry(void *param)
{
	psp_me_audio_job_t *job = (psp_me_audio_job_t *)param;

	meCoreDcacheInvalidateRange(job, sizeof(*job));
	meCoreDcacheInvalidateRange(job->data, job->size);
	job->job(job->data);
	meCoreDcacheWritebackRange(job->data, job->size);
}

static bool psp_audio_producer_init(void)
{
	int result;

	me_available = false;
	me_module_loaded = false;
	me_job_in_flight = false;
	me_job_data = NULL;
	me_job_cache_size = 0;
	me_workspace = NULL;
	me_workspace_size = 0;
	if (!audio_producer_cpu.init())
		return false;

	result = meSafeTaskInitDispatcher();
	if (result < 0)
	{
		printf("[PSP_ME_AUDIO] ME dispatcher unavailable (%d); using CPU producer\n",
			result);
		return true;
	}

	/* Loading AVCODEC exercises the safe-task patched ME EDRAM path while
	 * leaving ordinary System Controller ME syscalls available. */
	meSafeTaskLoadModule();
	me_module_loaded = true;

	if (!psp_me_probe())
	{
		printf("[PSP_ME_AUDIO] ME execution probe failed; using CPU producer\n");
		meSafeTaskUnloadModule();
		me_module_loaded = false;
		return true;
	}

	me_available = true;
	printf("[PSP_ME_AUDIO] ME execution probe passed; CPU producer retained as fallback\n");

	return true;
}

static void psp_audio_producer_shutdown(void)
{
	if (me_job_in_flight)
	{
		meSafeTaskWaitReady();
		if (me_job_data && me_job_cache_size)
			sceKernelDcacheInvalidateRange(me_job_data, me_job_cache_size);
		me_job_in_flight = false;
	}

	if (me_module_loaded)
		meSafeTaskUnloadModule();

	me_module_loaded = false;
	me_available = false;
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
	audio_producer_cpu.reset();
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
	return me_available;
}

static void *psp_audio_producer_acquireJobBuffer(uint32_t size, uint32_t alignment)
{
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
}

static bool psp_audio_producer_submitJob(audio_producer_job_fn job, void *data,
	uint32_t size)
{
	Task task;
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

	task.func = psp_me_audio_job_entry;
	task.param = &me_job;
	task.index = 0;
	result = meSafeTaskDispatch(&task);
	if (result < 0)
	{
		me_job_data = NULL;
		me_job_cache_size = 0;
		return false;
	}

	me_job_in_flight = true;
	return true;
}

static void psp_audio_producer_waitJob(void)
{
	if (!me_job_in_flight)
		return;
	meSafeTaskWaitReady();
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
	psp_audio_producer_render,
	psp_audio_producer_isAvailable,
	psp_audio_producer_canRunJobs,
	psp_audio_producer_acquireJobBuffer,
	psp_audio_producer_submitJob,
	psp_audio_producer_waitJob,
};

const audio_producer_driver_t *const audio_producer_driver = &audio_producer_psp;
