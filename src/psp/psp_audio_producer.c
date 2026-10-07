#include <stdio.h>

#include "common/audio_producer_driver.h"
#include "common/emulator_options.h"
#include "psp/psp_audio_backend.h"

static bool me_available;
static bool me_suspended;

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

static bool psp_audio_producer_backend_available(void *opaque)
{
	(void)opaque;
	return me_available;
}

static bool psp_audio_producer_enable(const char *context)
{
	if (!psp_me_mode_enabled())
		return false;
	if (!psp_audio_backend_enable(context, psp_me_mode_name()))
	{
		me_available = false;
		return false;
	}

	me_available = true;
	printf("[PSP_ME_AUDIO] %s: %s -> Media Engine (MIST); Main CPU retained as fallback\n",
		context, psp_me_mode_name());
	return true;
}

static bool psp_audio_producer_init(void)
{
	me_available = false;
	me_suspended = false;
	psp_audio_backend_init(psp_audio_producer_backend_available, NULL);

	if (!audio_producer_cpu.init())
	{
		psp_audio_backend_shutdown();
		return false;
	}
	if (!psp_me_mode_enabled())
	{
		printf("[PSP_ME_AUDIO] Audio processor: Main CPU; ME initialization skipped\n");
		return true;
	}

	(void)psp_audio_producer_enable("startup");
	return true;
}

static void psp_audio_producer_shutdown(void)
{
	psp_audio_backend_shutdown();
	me_available = false;
	me_suspended = false;
	audio_producer_cpu.shutdown();
}

static void psp_audio_producer_reset(void)
{
	psp_audio_backend_reset_action_t action;

	psp_audio_backend_wait_job();
	action = psp_audio_backend_reset(psp_me_mode_enabled(), me_available,
		me_suspended);
	switch (action)
	{
	case PSP_AUDIO_BACKEND_RESET_KEEP_AVAILABLE:
		me_available = true;
		break;
	case PSP_AUDIO_BACKEND_RESET_REENABLE:
		me_available = false;
		(void)psp_audio_producer_enable("reset");
		break;
	case PSP_AUDIO_BACKEND_RESET_KEEP_CPU:
	default:
		me_available = false;
		break;
	}
	audio_producer_cpu.reset();
}

static void psp_audio_producer_suspend(void)
{
	psp_audio_backend_wait_job();
	psp_audio_backend_suspend();
	me_available = false;
	me_suspended = true;
}

static void psp_audio_producer_resume(void)
{
	if (!me_suspended)
		return;

	me_suspended = false;
	if (!psp_me_mode_enabled())
		return;
	if (psp_audio_producer_enable("resume") &&
		!psp_audio_backend_resume_after_enable())
		me_available = false;
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
	return psp_audio_backend_can_run_jobs(me_available);
}

static void *psp_audio_producer_acquireJobBuffer(uint32_t size,
	uint32_t alignment)
{
	if (!me_available)
		return NULL;
	return psp_audio_backend_acquire_job_buffer(size, alignment);
}

static bool psp_audio_producer_submitJob(audio_producer_job_fn job, void *data,
	uint32_t size)
{
	if (!me_available)
		return false;
	return psp_audio_backend_submit_job(job, data, size);
}

static void psp_audio_producer_waitJob(void)
{
	psp_audio_backend_wait_job();
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
