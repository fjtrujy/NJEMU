#include <stdio.h>
#include <string.h>
#include <pspkernel.h>
#include <me-safe-task/me-stask.h>
#include "common/audio_producer_driver.h"

static bool me_available;
static bool me_module_loaded;
static uint32_t me_probe_data[16] __attribute__((aligned(64)));

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
