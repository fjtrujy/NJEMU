#include <stdio.h>

#include "psp/psp_audio_backend.h"
#include "psp/psp_me_dispatch.h"
#include "psp/psp_me_sound_lifecycle.h"
#include "psp/psp_neogeo_me_sound.h"

void psp_audio_backend_init(psp_audio_backend_available_fn available,
	void *opaque)
{
	psp_me_dispatch_reset_state();
	if (!psp_neogeo_me_sound_sync_init(available, opaque))
		printf("[PSP_ME_AUDIO] sound worker synchronization unavailable\n");
}

void psp_audio_backend_shutdown(void)
{
	psp_neogeo_me_sound_stop();
	psp_neogeo_me_sound_sync_shutdown();
	psp_me_dispatch_shutdown();
}

bool psp_audio_backend_enable(const char *context, const char *mode_name)
{
	const psp_me_sound_worker_dispatch_t dispatch = {
		psp_me_dispatch_worker_start,
		psp_me_dispatch_worker_wait,
		NULL,
	};

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
			return false;
		}
	}
	if (!psp_me_dispatch_enable(context, mode_name))
		return false;
	if (!psp_neogeo_me_sound_bootstrap(&dispatch))
	{
		printf("[PSP_ME_AUDIO] %s: persistent sound worker bootstrap failed; using Main CPU\n",
			context);
		return false;
	}
	return true;
}

psp_audio_backend_reset_action_t psp_audio_backend_reset(
	bool mode_enabled, bool available, bool suspended)
{
	psp_me_sound_reset_action_t action = psp_me_sound_reset_action(
		mode_enabled, available, psp_neogeo_me_sound_running(), suspended);

	switch (action)
	{
	case PSP_ME_SOUND_RESET_STOP_WORKER:
		psp_neogeo_me_sound_stop();
		return PSP_AUDIO_BACKEND_RESET_KEEP_CPU;
	case PSP_ME_SOUND_RESET_RESET_WORKER:
		return psp_neogeo_me_sound_reset_generation() ?
			PSP_AUDIO_BACKEND_RESET_KEEP_AVAILABLE :
			PSP_AUDIO_BACKEND_RESET_KEEP_CPU;
	case PSP_ME_SOUND_RESET_START_WORKER:
		return PSP_AUDIO_BACKEND_RESET_REENABLE;
	case PSP_ME_SOUND_RESET_RESTART_WORKER:
		psp_neogeo_me_sound_stop();
		return PSP_AUDIO_BACKEND_RESET_REENABLE;
	case PSP_ME_SOUND_RESET_DEFER_SUSPENDED:
	case PSP_ME_SOUND_RESET_KEEP_CPU:
	default:
		return PSP_AUDIO_BACKEND_RESET_KEEP_CPU;
	}
}

void psp_audio_backend_suspend(void)
{
	psp_neogeo_me_sound_stop();
}

bool psp_audio_backend_resume_after_enable(void)
{
	return true;
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
