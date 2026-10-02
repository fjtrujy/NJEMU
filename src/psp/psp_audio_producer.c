#include <stdio.h>
#include <me-safe-task/me-stask.h>
#include "common/audio_producer_driver.h"

static bool me_available;
static bool me_module_loaded;

static bool psp_audio_producer_init(void)
{
	int result;

	me_available = false;
	me_module_loaded = false;

	result = meSafeTaskInitDispatcher();
	if (result < 0)
	{
		printf("[PSP_ME_AUDIO] ME dispatcher unavailable (%d); using CPU producer\n",
			result);
		return true;
	}

	/* Classic me-safe-task is the best-tested upstream dispatch path on both
	 * Phat and Slim. Loading AVCODEC exercises the patched ME EDRAM path while
	 * leaving ordinary System Controller ME syscalls available. */
	meSafeTaskLoadModule();
	me_module_loaded = true;
	me_available = true;
	printf("[PSP_ME_AUDIO] ME dispatcher initialized; CPU producer retained as fallback\n");

	return true;
}

static void psp_audio_producer_shutdown(void)
{
	if (me_available)
		meSafeTaskWaitReady();

	if (me_module_loaded)
		meSafeTaskUnloadModule();

	me_module_loaded = false;
	me_available = false;
}

static void psp_audio_producer_reset(void)
{
	if (me_available)
		meSafeTaskWaitReady();
}

static void psp_audio_producer_render(audio_producer_render_fn cpu_render,
	int16_t *buffer)
{
	cpu_render(buffer);
}

static bool psp_audio_producer_isAvailable(void)
{
	return me_available;
}

static const audio_producer_driver_t audio_producer_psp = {
	"psp-me",
	psp_audio_producer_init,
	psp_audio_producer_shutdown,
	psp_audio_producer_reset,
	psp_audio_producer_render,
	psp_audio_producer_isAvailable,
};

const audio_producer_driver_t *const audio_producer_driver = &audio_producer_psp;
