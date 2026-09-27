/******************************************************************************

	psvita_audio.c

	PS Vita audio output using SceAudio

******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/audioout.h>
#include "common/audio_driver.h"

#define AUDIO_CHANNELS 2

typedef struct psvita_audio {
	int port;
	int16_t *audio_buffer;
	uint32_t buffered_samples;
	uint32_t buffer_capacity;
	uint16_t port_samples;
	uint8_t channels;
} psvita_audio_t;

static void *psvita_audio_init(void) {
	psvita_audio_t *psvita = (psvita_audio_t*)calloc(1, sizeof(psvita_audio_t));
	
	psvita->port = -1;
	psvita->audio_buffer = NULL;
	psvita->buffered_samples = 0;
	psvita->buffer_capacity = 0;
	psvita->port_samples = 0;
	psvita->channels = AUDIO_CHANNELS;
	
	return psvita;
}

static void psvita_audio_free(void *data) {
	psvita_audio_t *psvita = (psvita_audio_t*)data;
	
	if (psvita->port >= 0) {
		sceAudioOutReleasePort(psvita->port);
		psvita->port = -1;
	}
	
	if (psvita->audio_buffer) {
		free(psvita->audio_buffer);
		psvita->audio_buffer = NULL;
	}
	
	free(psvita);
}

static int32_t psvita_audio_volumeMax(void *data) {
	return SCE_AUDIO_VOLUME_0DB;
}

static bool psvita_audio_ensure_capacity(psvita_audio_t *psvita, uint32_t samples)
{
	if (samples <= psvita->buffer_capacity)
		return true;

	uint32_t new_capacity = psvita->buffer_capacity ? psvita->buffer_capacity : 64;
	while (new_capacity < samples)
		new_capacity *= 2;

	int16_t *new_buffer = realloc(psvita->audio_buffer,
		(size_t)new_capacity * psvita->channels * sizeof(int16_t));
	if (new_buffer == NULL)
		return false;

	psvita->audio_buffer = new_buffer;
	psvita->buffer_capacity = new_capacity;
	return true;
}

static bool psvita_audio_append(psvita_audio_t *psvita, const void *buffer,
	uint32_t size)
{
	uint32_t samples = size / (sizeof(int16_t) * psvita->channels);
	if (samples == 0)
		return true;

	if (!psvita_audio_ensure_capacity(psvita, psvita->buffered_samples + samples))
		return false;

	memcpy(psvita->audio_buffer + (size_t)psvita->buffered_samples * psvita->channels,
		buffer, (size_t)samples * psvita->channels * sizeof(int16_t));
	psvita->buffered_samples += samples;
	return true;
}

static void psvita_audio_drain(psvita_audio_t *psvita, int leftvol, int rightvol)
{
	int vols[2] = { leftvol, rightvol };
	sceAudioOutSetVolume(psvita->port,
		SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);

	while (psvita->buffered_samples >= psvita->port_samples) {
		sceAudioOutOutput(psvita->port, psvita->audio_buffer);

		psvita->buffered_samples -= psvita->port_samples;
		if (psvita->buffered_samples != 0) {
			memmove(psvita->audio_buffer,
				psvita->audio_buffer + (size_t)psvita->port_samples * psvita->channels,
				(size_t)psvita->buffered_samples * psvita->channels * sizeof(int16_t));
		}
	}
}

static bool psvita_audio_chSRCReserve(void *data, uint16_t samples, int32_t frequency, uint8_t channels) {
	psvita_audio_t *psvita = (psvita_audio_t*)data;

	/*
	 * SceAudio requires a multiple-of-64 hardware block. Do not round the
	 * emulation block up and pad it with silence, which would slow the audio
	 * clock down: use the next block down and keep the exact incoming stream in
	 * a FIFO, so that the long-term sample rate is preserved.
	 */
	uint16_t port_samples = samples & ~63u;
	if (port_samples < SCE_AUDIO_MIN_LEN)
		port_samples = SCE_AUDIO_MIN_LEN;
	if (port_samples > SCE_AUDIO_MAX_LEN)
		port_samples = SCE_AUDIO_MAX_LEN;

	psvita->port_samples = port_samples;
	psvita->channels = channels;
	psvita->buffered_samples = 0;

	// The BGM port accepts the emulated rates (44.1 kHz); the MAIN port is 48 kHz only.
	psvita->port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
		port_samples,
		frequency,
		(channels == 2) ? SCE_AUDIO_OUT_MODE_STEREO : SCE_AUDIO_OUT_MODE_MONO);

	if (psvita->port < 0) {
		printf("Failed to open audio port: 0x%08X\n", psvita->port);
		return false;
	}
	
	// Allow one full input update plus the residual from the previous one.
	psvita->buffer_capacity = 0;
	if (!psvita_audio_ensure_capacity(psvita, (uint32_t)samples + port_samples)) {
		sceAudioOutReleasePort(psvita->port);
		psvita->port = -1;
		return false;
	}

	return true;
}

static bool psvita_audio_chReserve(void *data, uint16_t samplecount, uint8_t channels) {
	// Use default frequency of 44100 Hz
	return psvita_audio_chSRCReserve(data, samplecount, 44100, channels);
}

static void psvita_audio_srcOutputBlocking(void *data, int32_t volume, void *buffer, uint32_t size) {
	psvita_audio_t *psvita = (psvita_audio_t*)data;

	if (psvita->port < 0 || !psvita->audio_buffer) {
		return;
	}

	if (!psvita_audio_append(psvita, buffer, size))
		return;
	psvita_audio_drain(psvita, volume, volume);
}

static void psvita_audio_outputPannedBlocking(void *data, int leftvol, int rightvol, void *buffer, uint32_t size) {
	psvita_audio_t *psvita = (psvita_audio_t*)data;

	if (psvita->port < 0 || !psvita->audio_buffer) {
		return;
	}

	if (!psvita_audio_append(psvita, buffer, size))
		return;
	psvita_audio_drain(psvita, leftvol, rightvol);
}

static void psvita_audio_release(void *data) {
	psvita_audio_t *psvita = (psvita_audio_t*)data;
	
	if (psvita->port >= 0) {
		sceAudioOutReleasePort(psvita->port);
		psvita->port = -1;
	}
	
	if (psvita->audio_buffer) {
		free(psvita->audio_buffer);
		psvita->audio_buffer = NULL;
	}
	psvita->buffered_samples = 0;
	psvita->buffer_capacity = 0;
	psvita->port_samples = 0;
}

audio_driver_t audio_psvita = {
	"psvita",
	psvita_audio_init,
	psvita_audio_free,
	psvita_audio_volumeMax,
	psvita_audio_chSRCReserve,
	psvita_audio_chReserve,
	psvita_audio_srcOutputBlocking,
	psvita_audio_outputPannedBlocking,
	psvita_audio_release,
	NULL, /* setPaused: the blocking output drains the silent buffers of a pause */
};
