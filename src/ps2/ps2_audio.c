#include <stdio.h>
#include <stdlib.h>

#include <kernel.h>
#include <audsrv.h>
#include <ps2_audio_driver.h>

#include "common/audio_driver.h"

typedef struct ps2_audio {
	int32_t channel;
} ps2_audio_t;

static void *ps2_init(void) {
	if(init_audio_driver() < 0)
		return NULL;

	ps2_audio_t *ps2 = (ps2_audio_t*)calloc(1, sizeof(ps2_audio_t));
	return ps2;
}

static void ps2_free(void *data) {
	ps2_audio_t *ps2 = (ps2_audio_t*)data;

	deinit_audio_driver();

	free(ps2);
}

static int32_t ps2_volumeMax(void *data) {
	return MAX_VOLUME;
}

static bool ps2_chSRCReserve(void *data, int32_t samples, int32_t freqency, int32_t channels) {
	ps2_audio_t *ps2 = (ps2_audio_t*)data;
	struct audsrv_fmt_t format;
    format.bits = 16;
    format.freq = freqency;
    format.channels = channels;

    ps2->channel = audsrv_set_format(&format);
    audsrv_set_volume(MAX_VOLUME);
	return ps2->channel >= 0;
}

static bool ps2_chReserve(void *data, int32_t samplecount, int32_t channels) {
	ps2_audio_t *ps2 = (ps2_audio_t*)data;
	// int32_t format = channels == 1 ? PS2_AUDIO_FORMAT_MONO : PS2_AUDIO_FORMAT_STEREO;
	// ps2->channel = sceAudioChReserve(PS2_AUDIO_NEXT_CHANNEL, samplecount, format);
	// return ps2->channel >= 0;
	return false;
}

static void ps2_release(void *data) {
	audsrv_stop_audio();
}

static void ps2_srcOutputBlocking(void *data, int32_t volume, void *buffer) {
	// sceAudioSRCOutputBlocking(volume, buffer);
}

static void ps2_outputPannedBlocking(void *data, int leftvol, int rightvol, void *buf) {
	ps2_audio_t *ps2 = (ps2_audio_t*)data;
	// sceAudioOutputPannedBlocking(ps2->channel, leftvol, rightvol, buf);
}

audio_driver_t audio_ps2 = {
	"ps2",
	ps2_init,
	ps2_free,
	ps2_volumeMax,
	ps2_chSRCReserve,
	ps2_chReserve,
	ps2_srcOutputBlocking,
	ps2_outputPannedBlocking,
	ps2_release,
};