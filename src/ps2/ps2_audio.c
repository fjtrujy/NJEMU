#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kernel.h>
#include <audsrv.h>

#include "common/audio_driver.h"

/*
 * PS2 Audio Driver with Software MP3 Mixing
 * 
 * Since audsrv only supports a single audio stream, we implement software
 * mixing for NCDZ's MP3 (CDDA) playback alongside the main YM2610 audio.
 * 
 * Architecture:
 * - Main sound thread outputs YM2610 audio via srcOutputBlocking
 * - MP3 thread writes decoded audio to a ring buffer via outputPannedBlocking
 * - srcOutputBlocking mixes the MP3 ring buffer with game audio before output
 * 
 * Synchronization:
 * - A semaphore is used to block the MP3 thread when the buffer is full
 * - The sound thread signals the semaphore after consuming samples
 */

/* Ring buffer size - must be power of 2 for efficient masking */
#define MP3_RING_BUFFER_SIZE    (8192 * 2)  /* Stereo samples */
#define MP3_RING_BUFFER_MASK    (MP3_RING_BUFFER_SIZE - 1)

/* Threshold for signaling - signal when this much space becomes available */
#define MP3_SIGNAL_THRESHOLD    (736 * 2 * 2)  /* About one MP3 frame worth */

typedef struct ps2_audio {
	int32_t channel;
	uint16_t samples;
	bool is_mp3_channel;
} ps2_audio_t;

/* Global MP3 mixing state - shared between main audio and MP3 instances */
static int16_t *g_mp3_ring_buffer = NULL;
static volatile uint32_t g_mp3_write_pos = 0;
static volatile uint32_t g_mp3_read_pos = 0;
static volatile int g_mp3_left_vol = 0;
static volatile int g_mp3_right_vol = 0;
static volatile bool g_mp3_active = false;

/* Semaphore for MP3 thread synchronization */
static int32_t g_mp3_sema_id = -1;
static volatile uint32_t g_mp3_last_read_pos = 0;

static void *ps2_init(void) {
	ps2_audio_t *ps2 = (ps2_audio_t*)calloc(1, sizeof(ps2_audio_t));
	ps2->is_mp3_channel = false;
	return ps2;
}

static void ps2_free(void *data) {
	ps2_audio_t *ps2 = (ps2_audio_t*)data;
	
	if (ps2->is_mp3_channel) {
		/* Clean up MP3 resources */
		g_mp3_active = false;
		
		if (g_mp3_sema_id >= 0) {
			DeleteSema(g_mp3_sema_id);
			g_mp3_sema_id = -1;
		}
		
		if (g_mp3_ring_buffer) {
			free(g_mp3_ring_buffer);
			g_mp3_ring_buffer = NULL;
		}
	}
	
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

	printf("PS2 Audio: Reserving SRC channel - samples=%d, freq=%d, channels=%d\n", 
	       samples, frequency, channels);

	ps2->channel = audsrv_set_format(&format);
	ps2->samples = samples;
	ps2->is_mp3_channel = false;
	
	printf("PS2 Audio: audsrv_set_format returned channel=%d\n", ps2->channel);
	
	if (ps2->channel >= 0) {
		audsrv_set_volume(MAX_VOLUME);
		printf("PS2 Audio: Audio initialized successfully\n");
		return true;
	} else {
		printf("PS2 Audio: ERROR - Failed to initialize audio channel\n");
		return false;
	}
}

static bool ps2_chReserve(void *data, uint16_t samplecount, uint8_t channels) {
	ps2_audio_t *ps2 = (ps2_audio_t*)data;
	ee_sema_t sema;
	
	/* This is called for MP3 channel - set up the ring buffer for mixing */
	ps2->is_mp3_channel = true;
	ps2->samples = samplecount;
	
	/* Allocate global MP3 ring buffer if not already allocated */
	if (g_mp3_ring_buffer == NULL) {
		g_mp3_ring_buffer = (int16_t*)malloc(MP3_RING_BUFFER_SIZE * sizeof(int16_t));
		if (g_mp3_ring_buffer == NULL) {
			printf("PS2 Audio: Failed to allocate MP3 ring buffer\n");
			return false;
		}
		memset(g_mp3_ring_buffer, 0, MP3_RING_BUFFER_SIZE * sizeof(int16_t));
	}
	
	/* Create semaphore for synchronization */
	if (g_mp3_sema_id < 0) {
		sema.init_count = 1;  /* Start with buffer available */
		sema.max_count = 1;
		sema.option = 0;
		g_mp3_sema_id = CreateSema(&sema);
		if (g_mp3_sema_id < 0) {
			printf("PS2 Audio: Failed to create MP3 semaphore\n");
			free(g_mp3_ring_buffer);
			g_mp3_ring_buffer = NULL;
			return false;
		}
	}
	
	/* Reset ring buffer state */
	g_mp3_write_pos = 0;
	g_mp3_read_pos = 0;
	g_mp3_last_read_pos = 0;
	g_mp3_left_vol = 0;
	g_mp3_right_vol = 0;
	g_mp3_active = true;
	
	printf("PS2 Audio: MP3 mixing channel initialized (%d samples)\n", samplecount);
	return true;
}

static void ps2_release(void *data) {
	ps2_audio_t *ps2 = (ps2_audio_t*)data;

	if (ps2->is_mp3_channel) {
		/* Clear MP3 buffer on release */
		g_mp3_active = false;
		g_mp3_left_vol = 0;
		g_mp3_right_vol = 0;
		
		/* Signal semaphore to unblock any waiting thread */
		if (g_mp3_sema_id >= 0) {
			SignalSema(g_mp3_sema_id);
		}
		
		if (g_mp3_ring_buffer) {
			memset(g_mp3_ring_buffer, 0, MP3_RING_BUFFER_SIZE * sizeof(int16_t));
		}
		g_mp3_write_pos = 0;
		g_mp3_read_pos = 0;
		g_mp3_last_read_pos = 0;
	} else {
		audsrv_stop_audio();
	}
}

/*
 * Mix MP3 audio into the output buffer
 * Called from srcOutputBlocking before sending to audsrv
 */
static void mix_mp3_audio(int16_t *buffer, uint32_t num_samples) {
	uint32_t i;
	uint32_t available;
	uint32_t read_pos;
	uint32_t consumed;
	int32_t mixed;
	
	if (!g_mp3_active || !g_mp3_ring_buffer) {
		return;
	}
	
	/* Calculate available samples in ring buffer */
	read_pos = g_mp3_read_pos;
	available = (g_mp3_write_pos - read_pos) & MP3_RING_BUFFER_MASK;
	
	if (available == 0) {
		return;
	}
	
	/* Mix available MP3 samples into output buffer */
	/* num_samples is the number of stereo sample pairs */
	consumed = 0;
	for (i = 0; i < num_samples * 2 && consumed < available; i++) {
		/* Mix with saturation */
		mixed = (int32_t)buffer[i] + (int32_t)g_mp3_ring_buffer[read_pos];
		
		/* Clamp to 16-bit range */
		if (mixed > 32767) mixed = 32767;
		if (mixed < -32768) mixed = -32768;
		
		buffer[i] = (int16_t)mixed;
		read_pos = (read_pos + 1) & MP3_RING_BUFFER_MASK;
		consumed++;
	}
	
	g_mp3_read_pos = read_pos;
	
	/* Signal MP3 thread if we've freed up enough space */
	if (g_mp3_sema_id >= 0 && consumed >= MP3_SIGNAL_THRESHOLD) {
		SignalSema(g_mp3_sema_id);
	}
}

static void ps2_srcOutputBlocking(void *data, int32_t volume, void *buffer, uint32_t size) {
	uint32_t num_samples = size / sizeof(int16_t) / 2; /* Stereo samples */
	
	/* Mix MP3 audio into the buffer before output */
	mix_mp3_audio((int16_t*)buffer, num_samples);
	
	audsrv_wait_audio(size);
	audsrv_play_audio(buffer, size);
}

static void ps2_outputPannedBlocking(void *data, int leftvol, int rightvol, void *buffer, uint32_t size) {
	int16_t *src = (int16_t*)buffer;
	uint32_t num_samples = size / sizeof(int16_t);
	uint32_t i;
	uint32_t write_pos;
	uint32_t available_space;
	int32_t sample;
	
	if (!g_mp3_ring_buffer || !g_mp3_active) {
		/* No buffer available, nothing to do */
		return;
	}
	
	/* Store volume levels */
	g_mp3_left_vol = leftvol;
	g_mp3_right_vol = rightvol;
	
	/* Calculate available space in ring buffer */
	write_pos = g_mp3_write_pos;
	available_space = MP3_RING_BUFFER_SIZE - ((write_pos - g_mp3_read_pos) & MP3_RING_BUFFER_MASK) - 1;
	
	/* Wait for space if buffer is too full */
	while (available_space < num_samples && g_mp3_active) {
		/* Wait on semaphore - will be signaled when samples are consumed */
		if (g_mp3_sema_id >= 0) {
			WaitSema(g_mp3_sema_id);
		}
		
		/* Check if we were released */
		if (!g_mp3_active) {
			return;
		}
		
		/* Recalculate available space */
		available_space = MP3_RING_BUFFER_SIZE - ((write_pos - g_mp3_read_pos) & MP3_RING_BUFFER_MASK) - 1;
	}
	
	/* Write samples to ring buffer with volume applied */
	/* MP3 is stereo: L, R, L, R, ... */
	for (i = 0; i < num_samples; i += 2) {
		/* Left channel */
		sample = ((int32_t)src[i] * leftvol) / MAX_VOLUME;
		if (sample > 32767) sample = 32767;
		if (sample < -32768) sample = -32768;
		g_mp3_ring_buffer[write_pos] = (int16_t)sample;
		write_pos = (write_pos + 1) & MP3_RING_BUFFER_MASK;
		
		/* Right channel */
		sample = ((int32_t)src[i + 1] * rightvol) / MAX_VOLUME;
		if (sample > 32767) sample = 32767;
		if (sample < -32768) sample = -32768;
		g_mp3_ring_buffer[write_pos] = (int16_t)sample;
		write_pos = (write_pos + 1) & MP3_RING_BUFFER_MASK;
	}
	
	g_mp3_write_pos = write_pos;
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
