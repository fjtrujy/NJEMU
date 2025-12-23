#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/audio_driver.h"

#include <SDL.h>

/*
 * Desktop Audio Driver
 * 
 * Uses SDL2 for audio output. Supports two modes:
 * 1. Main audio (YM2610) via chSRCReserve - uses SDL audio stream for sample rate conversion
 * 2. MP3 audio via chReserve - direct 44.1kHz stereo output for CDDA playback
 */

typedef struct desktop_audio {
    SDL_AudioDeviceID device;
    SDL_AudioSpec spec;
    SDL_AudioStream *stream;
    bool is_mp3_channel;
    uint16_t samples;
} desktop_audio_t;

static void *desktop_init(void) {
	desktop_audio_t *desktop = (desktop_audio_t*)calloc(1, sizeof(desktop_audio_t));
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        SDL_InitSubSystem(SDL_INIT_AUDIO);
    }
	return desktop;
}

static void desktop_free(void *data) {
	desktop_audio_t *desktop = (desktop_audio_t*)data;
    
    if (desktop->stream) {
        SDL_FreeAudioStream(desktop->stream);
        desktop->stream = NULL;
    }
    
    if (desktop->device) {
        SDL_CloseAudioDevice(desktop->device);
        desktop->device = 0;
    }
	
    free(desktop);
}

static int32_t desktop_volumeMax(void *data) {
	return 32767;
}

static bool desktop_chSRCReserve(void *data, uint16_t samples, int32_t frequency, uint8_t channels) {
    desktop_audio_t *desktop = (desktop_audio_t*)data;

    SDL_AudioSpec desired, obtained;
    SDL_memset(&desired, 0, sizeof(desired));
    desired.freq = frequency;
    desired.format = AUDIO_S16SYS;
    desired.channels = channels;
    desired.samples = samples;
    desired.callback = NULL;

    desktop->device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (desktop->device <= 0) {
        fprintf(stderr, "Failed to open audio device: %s\n", SDL_GetError());
        return false;
    }

    desktop->spec = obtained;
    desktop->is_mp3_channel = false;
    desktop->samples = samples;
    
    /* Create an audio stream for sample rate conversion */
    desktop->stream = SDL_NewAudioStream(AUDIO_S16SYS, channels, frequency,
                                        obtained.format, obtained.channels, obtained.freq);
    if (!desktop->stream) {
        fprintf(stderr, "Failed to create audio stream: %s\n", SDL_GetError());
        SDL_CloseAudioDevice(desktop->device);
        desktop->device = 0;
        return false;
    }
    
    SDL_PauseAudioDevice(desktop->device, 0);
    return true;
}

static bool desktop_chReserve(void *data, uint16_t samplecount, uint8_t channels) {
    desktop_audio_t *desktop = (desktop_audio_t*)data;
    
    /* MP3 channel - 44.1kHz stereo 16-bit */
    SDL_AudioSpec desired, obtained;
    SDL_memset(&desired, 0, sizeof(desired));
    desired.freq = 44100;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = samplecount;
    desired.callback = NULL;
    
    desktop->device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (desktop->device <= 0) {
        fprintf(stderr, "Failed to open MP3 audio device: %s\n", SDL_GetError());
        return false;
    }
    
    desktop->spec = obtained;
    desktop->is_mp3_channel = true;
    desktop->samples = samplecount;
    desktop->stream = NULL;
    
    SDL_PauseAudioDevice(desktop->device, 0);
    
    printf("Desktop Audio: MP3 channel initialized (%d samples, %d Hz)\n", samplecount, obtained.freq);
    return true;
}

static void desktop_release(void *data) {
    desktop_audio_t *desktop = (desktop_audio_t*)data;

    if (desktop->stream) {
        SDL_FreeAudioStream(desktop->stream);
        desktop->stream = NULL;
    }

    if (desktop->device) {
        SDL_CloseAudioDevice(desktop->device);
        desktop->device = 0;
    }
}

static void desktop_srcOutputBlocking(void *data, int32_t volume, void *buffer, uint32_t size) {
    desktop_audio_t *desktop = (desktop_audio_t*)data;

    if (!desktop->device || !desktop->stream) {
        fprintf(stderr, "Audio device or stream not initialized\n");
        return;
    }
    
    /* Add data to the stream for conversion */
    if (SDL_AudioStreamPut(desktop->stream, buffer, size) < 0) {
        fprintf(stderr, "Failed to queue audio stream data: %s\n", SDL_GetError());
        return;
    }

    int available = SDL_AudioStreamAvailable(desktop->stream);
    if (available > 0) {
        void *out_buffer = malloc(available);
        int len = SDL_AudioStreamGet(desktop->stream, out_buffer, available);
        if (len > 0) {
            SDL_QueueAudio(desktop->device, out_buffer, len);
        }
        free(out_buffer);
    }

    /* Wait if too much audio is queued to prevent runaway buffering */
    while (SDL_GetQueuedAudioSize(desktop->device) > size * 8) {
        SDL_Delay(1);
    }
}

static void desktop_outputPannedBlocking(void *data, int leftvol, int rightvol, void *buffer, uint32_t size) {
    desktop_audio_t *desktop = (desktop_audio_t*)data;
    int16_t *src = (int16_t*)buffer;
    uint32_t num_samples = size / sizeof(int16_t);
    int32_t max_vol = desktop_volumeMax(data);
    
    if (!desktop->device) {
        fprintf(stderr, "MP3 audio device not initialized\n");
        return;
    }
    
    /* Apply volume to samples in-place */
    for (uint32_t i = 0; i < num_samples; i += 2) {
        /* Left channel */
        int32_t sample = ((int32_t)src[i] * leftvol) / max_vol;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        src[i] = (int16_t)sample;
        
        /* Right channel */
        sample = ((int32_t)src[i + 1] * rightvol) / max_vol;
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;
        src[i + 1] = (int16_t)sample;
    }
    
    /* Queue audio for playback */
    SDL_QueueAudio(desktop->device, src, size);
    
    /* Wait if too much audio is queued */
    while (SDL_GetQueuedAudioSize(desktop->device) > size * 4) {
        SDL_Delay(1);
    }
}

audio_driver_t audio_desktop = {
	"desktop",
	desktop_init,
	desktop_free,
	desktop_volumeMax,
	desktop_chSRCReserve,
	desktop_chReserve,
	desktop_srcOutputBlocking,
	desktop_outputPannedBlocking,
	desktop_release,
};
