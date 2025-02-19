#include <stdio.h>
#include <stdlib.h>

#include <kernel.h>
#include <audsrv.h>
#include <ps2_audio_driver.h>

#include "common/audio_driver.h"

typedef struct ps2_audio {
	int32_t channel;
	uint16_t samples;
} ps2_audio_t;

static void *ps2_init(void) {
	ps2_audio_t *ps2 = (ps2_audio_t*)calloc(1, sizeof(ps2_audio_t));

	init_audio_driver();

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

static bool ps2_chSRCReserve(void *data, uint16_t samples, int32_t frequency, uint8_t channels) {
	ps2_audio_t *ps2 = (ps2_audio_t*)data;
	struct audsrv_fmt_t format;
    format.bits = 16;
    format.freq = frequency;
    format.channels = channels;

    ps2->channel = audsrv_set_format(&format);
	ps2->samples = samples;
    audsrv_set_volume(MAX_VOLUME);
	return ps2->channel >= 0;
}

static bool ps2_chReserve(void *data, uint16_t samplecount, uint8_t channels) {
	ps2_audio_t *ps2 = (ps2_audio_t*)data;
	// struct audsrv_fmt_t format;
    // format.bits = 16;
    // format.freq = frequency;
    // format.channels = channels;

    // ps2->channel = audsrv_set_format(&format);
	// ps2->samples = samples;
    // audsrv_set_volume(MAX_VOLUME);
	// return ps2->channel >= 0;

	return ps2->channel >= 0;
}

static void ps2_release(void *data) {
	audsrv_stop_audio();
}

static void ps2_srcOutputBlocking(void *data, int32_t volume, void *buffer, uint32_t size) {
	ps2_audio_t *ps2 = (ps2_audio_t*)data;
	
	audsrv_wait_audio(size);
	audsrv_play_audio(buffer, size);
}

static void ps2_outputPannedBlocking(void *data, int leftvol, int rightvol, void *buf) {
	ps2_audio_t *ps2 = (ps2_audio_t*)data;
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