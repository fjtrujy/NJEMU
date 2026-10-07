#include <stdio.h>

#include "common/cps2_sound_offload.h"
#include "psp/psp_audio_backend.h"
#include "psp/psp_cps2_me_sound.h"
#include "psp/psp_me_dispatch.h"

void psp_audio_backend_init(psp_audio_backend_available_fn available,
	void *opaque)
{
	(void)available;
	(void)opaque;
	psp_me_dispatch_reset_state();
	if (!psp_cps2_me_sound_sync_init())
		printf("[PSP_ME_AUDIO] QSound worker synchronization unavailable\n");
}

void psp_audio_backend_shutdown(void)
{
	psp_cps2_me_sound_stop();
	psp_cps2_me_sound_sync_shutdown();
	psp_me_dispatch_shutdown();
}

bool psp_audio_backend_enable(const char *context, const char *mode_name)
{
	const psp_me_qsound_worker_dispatch_t dispatch = {
		psp_me_dispatch_worker_start,
		psp_me_dispatch_worker_wait,
		NULL,
	};

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
			return false;
		}
	}
	if (!psp_me_dispatch_enable(context, mode_name))
		return false;
	if (!psp_cps2_me_sound_bootstrap(&dispatch))
	{
		printf("[PSP_ME_AUDIO] %s: persistent QSound worker bootstrap failed; using Main CPU\n",
			context);
		return false;
	}
	return true;
}

psp_audio_backend_reset_action_t psp_audio_backend_reset(
	bool mode_enabled, bool available, bool suspended)
{
	(void)available;
	if (!mode_enabled)
	{
		psp_cps2_me_sound_stop();
		return PSP_AUDIO_BACKEND_RESET_KEEP_CPU;
	}
	if (suspended)
		return PSP_AUDIO_BACKEND_RESET_KEEP_CPU;
	if (!psp_cps2_me_sound_running())
		return PSP_AUDIO_BACKEND_RESET_REENABLE;
	return psp_cps2_me_sound_reset_generation() ?
		PSP_AUDIO_BACKEND_RESET_KEEP_AVAILABLE :
		PSP_AUDIO_BACKEND_RESET_KEEP_CPU;
}

void psp_audio_backend_suspend(void)
{
	(void)cps2_sound_offload_prepare_cpu_state();
	psp_cps2_me_sound_stop();
}

bool psp_audio_backend_resume_after_enable(void)
{
	if (cps2_sound_offload_snapshot_from_cpu())
		return true;

	printf("[PSP_ME_AUDIO] resume: CPS2 sound snapshot failed; using Main CPU\n");
	psp_cps2_me_sound_stop();
	return false;
}

bool psp_audio_backend_can_run_jobs(bool available)
{
	(void)available;
	return false;
}

void *psp_audio_backend_acquire_job_buffer(uint32_t size, uint32_t alignment)
{
	(void)size;
	(void)alignment;
	return NULL;
}

bool psp_audio_backend_submit_job(audio_producer_job_fn job, void *data,
	uint32_t size)
{
	(void)job;
	(void)data;
	(void)size;
	return false;
}

void psp_audio_backend_wait_job(void)
{
}
