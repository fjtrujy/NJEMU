#include <stdio.h>
#include <stdlib.h>
#include <pspaudio.h>
#include <pspthreadman.h>
#include "common/audio_driver.h"

typedef struct psp_audio {
	int32_t channel;
	bool src_channel;
} psp_audio_t;

static void *psp_init(void) {
	psp_audio_t *psp = (psp_audio_t*)calloc(1, sizeof(psp_audio_t));
	if (psp)
		psp->channel = -1;
	return psp;
}

static void psp_free(void *data) {
	psp_audio_t *psp = (psp_audio_t*)data;
	free(psp);
}

static int32_t psp_volumeMax(void *data) {
	(void)data;
	return PSP_AUDIO_VOLUME_MAX;
}

static bool psp_chSRCReserve(void *data, uint16_t samples, int32_t frequency, uint8_t channels) {
	psp_audio_t *psp = (psp_audio_t*)data;
	psp->channel = sceAudioSRCChReserve(samples, frequency, channels);
	psp->src_channel = psp->channel >= 0;
	return psp->channel >= 0;
}

static bool psp_chReserve(void *data, uint16_t samplecount, uint8_t channels) {
	psp_audio_t *psp = (psp_audio_t*)data;
	int32_t format = channels == 1 ? PSP_AUDIO_FORMAT_MONO : PSP_AUDIO_FORMAT_STEREO;
	psp->channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, samplecount, format);
	psp->src_channel = false;
	return psp->channel >= 0;
}

static void psp_release(void *data) {
	psp_audio_t *psp = (psp_audio_t*)data;

	if (psp->channel < 0)
		return;

	if (psp->src_channel)
	{
		/* SRC owns two DMA descriptors. A blocking output can return while a
		 * descriptor is still playing, and sceAudioSRCChRelease() refuses to
		 * release an armed channel. Output2 shares this hardware channel and its
		 * rest-sample query is the public way to observe pending SRC DMA work. */
		while (sceAudioOutput2GetRestSample() > 0)
			sceKernelDelayThread(1000);
		sceAudioSRCChRelease();
	}
	else
		sceAudioChRelease(psp->channel);

	psp->channel = -1;
	psp->src_channel = false;
}

static void psp_srcOutputBlocking(void *data, int32_t volume, void *buffer, uint32_t size) {
	(void)data;
	(void)size;
	sceAudioSRCOutputBlocking(volume, buffer);
}

static void psp_outputPannedBlocking(void *data, int leftvol, int rightvol, void *buffer, uint32_t size) {
	(void)size;
	psp_audio_t *psp = (psp_audio_t*)data;
	sceAudioOutputPannedBlocking(psp->channel, leftvol, rightvol, buffer);
}

audio_driver_t audio_psp = {
	"psp",
	psp_init,
	psp_free,
	psp_volumeMax,
	psp_chSRCReserve,
	psp_chReserve,
	psp_srcOutputBlocking,
	psp_outputPannedBlocking,
	psp_release,
	NULL,
};
