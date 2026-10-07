#include <stddef.h>

#include "psp/psp_audio_backend.h"
#include "psp/psp_me_dispatch.h"

void psp_audio_backend_init(psp_audio_backend_available_fn available,
	void *opaque)
{
	(void)available;
	(void)opaque;
	psp_me_dispatch_reset_state();
}

void psp_audio_backend_shutdown(void)
{
	psp_me_dispatch_shutdown();
}

bool psp_audio_backend_enable(const char *context, const char *mode_name)
{
	return psp_me_dispatch_enable(context, mode_name);
}

psp_audio_backend_reset_action_t psp_audio_backend_reset(
	bool mode_enabled, bool available, bool suspended)
{
	if (!mode_enabled || suspended)
		return PSP_AUDIO_BACKEND_RESET_KEEP_CPU;
	return available ? PSP_AUDIO_BACKEND_RESET_KEEP_AVAILABLE :
		PSP_AUDIO_BACKEND_RESET_REENABLE;
}

void psp_audio_backend_suspend(void)
{
}

bool psp_audio_backend_resume_after_enable(void)
{
	return true;
}

bool psp_audio_backend_can_run_jobs(bool available)
{
	return available;
}

void *psp_audio_backend_acquire_job_buffer(uint32_t size, uint32_t alignment)
{
	return psp_me_dispatch_acquire_job_buffer(size, alignment);
}

bool psp_audio_backend_submit_job(audio_producer_job_fn job, void *data,
	uint32_t size)
{
	return psp_me_dispatch_submit_job(job, data, size);
}

void psp_audio_backend_wait_job(void)
{
	psp_me_dispatch_wait_job();
}
