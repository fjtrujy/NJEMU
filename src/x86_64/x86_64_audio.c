#include <stdio.h>
#include <stdlib.h>

#include "common/audio_driver.h"

#include <SDL.h>

typedef struct x86_64_audio {
    SDL_AudioDeviceID device;
    SDL_AudioSpec spec;
    SDL_AudioStream *stream;
} x86_64_audio_t;

static void *x86_64_init(void) {
	x86_64_audio_t *x86_64 = (x86_64_audio_t*)calloc(1, sizeof(x86_64_audio_t));
    SDL_InitSubSystem(SDL_INIT_AUDIO);
	return x86_64;
}

static void x86_64_free(void *data) {
	x86_64_audio_t *x86_64 = (x86_64_audio_t*)data;
    if (x86_64->device) {
        SDL_CloseAudioDevice(x86_64->device);
    }
	
    free(x86_64);
}

static int32_t x86_64_volumeMax(void *data) {
	return 32767;
}

static bool x86_64_chSRCReserve(void *data, uint16_t samples, int32_t frequency, uint8_t channels) {
    x86_64_audio_t *x86_64 = (x86_64_audio_t*)data;

    SDL_AudioSpec desired, obtained;
    SDL_memset(&desired, 0, sizeof(desired));
    desired.freq = frequency;
    desired.format = AUDIO_S16SYS; // Floating-point audio format
    desired.channels = channels;
    desired.samples = samples; //frequency/10000 * 1024;
    desired.callback = NULL; // No callback; we use blocking audio

    x86_64->device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (x86_64->device <= 0) {
        fprintf(stderr, "Failed to open audio device: %s\n", SDL_GetError());
        return false;
    }

    x86_64->spec = obtained;
    
    // Create an audio stream for sample conversion
    x86_64->stream = SDL_NewAudioStream(AUDIO_S16SYS, channels, frequency,
                                        obtained.format, obtained.channels, obtained.freq);
    if (!x86_64->stream) {
        fprintf(stderr, "Failed to create audio stream: %s\n", SDL_GetError());
        SDL_CloseAudioDevice(x86_64->device);
        return false;
    }
    
    SDL_PauseAudioDevice(x86_64->device, 0); // Start audio playback
    return true;
}

static bool x86_64_chReserve(void *data, uint16_t samplecount, uint8_t channels) {
    // This function may be redundant if `x86_64_chSRCReserve` handles initialization
    return true;
}

static void x86_64_release(void *data) {
    x86_64_audio_t *x86_64 = (x86_64_audio_t*)data;

    if (x86_64->stream) {
        SDL_FreeAudioStream(x86_64->stream);
        x86_64->stream = NULL;
    }

    if (x86_64->device) {
        SDL_CloseAudioDevice(x86_64->device);
        x86_64->device = 0;
    }
}

static void x86_64_srcOutputBlocking(void *data, int32_t volume, void *buffer, uint32_t size) {
    x86_64_audio_t *x86_64 = (x86_64_audio_t*)data;

    if (!x86_64->device || !x86_64->stream) {
        fprintf(stderr, "Audio device or stream not initialized\n");
        return;
    }
    
    // Add data to the stream for conversion
    if (SDL_AudioStreamPut(x86_64->stream, buffer, size) < 0) {
        fprintf(stderr, "Failed to queue audio stream data: %s\n", SDL_GetError());
        return;
    }

    int available = SDL_AudioStreamAvailable(x86_64->stream);
    void *out_buffer = malloc(available);

    int len = SDL_AudioStreamGet(x86_64->stream, out_buffer, available);
    if (len > 0) {
        SDL_QueueAudio(x86_64->device, out_buffer, len);
    }
    free(out_buffer);

    // SDL queuing is not limited, so make sure to wait until is it empty enough again
    while (SDL_GetQueuedAudioSize(x86_64->device) > size * 8)
        SDL_Delay(100);
}

static void x86_64_outputPannedBlocking(void *data, int leftvol, int rightvol, void *buffer, uint32_t size) {
    // In a stereo configuration, pan the audio manually
    x86_64_audio_t *x86_64 = (x86_64_audio_t*)data;

    if (!x86_64->device || x86_64->spec.channels < 2) {
        fprintf(stderr, "Audio device not initialized or not stereo\n");
        return;
    }

    float *float_buffer = (float*)buffer;
    int samples = x86_64->spec.samples * x86_64->spec.channels;
    for (int i = 0; i < samples; i += 2) {
        float_buffer[i] *= (leftvol / 100.0f);
        float_buffer[i + 1] *= (rightvol / 100.0f);
    }

    SDL_QueueAudio(x86_64->device, float_buffer, samples * sizeof(float));

    // SDL queuing is not limited, so make sure to wait until is it empty enough again
    while (SDL_GetQueuedAudioSize(x86_64->device) > samples*sizeof(int16_t) * 8)
        SDL_Delay(100);
}

audio_driver_t audio_x86_64 = {
	"x86_64",
	x86_64_init,
	x86_64_free,
	x86_64_volumeMax,
	x86_64_chSRCReserve,
	x86_64_chReserve,
	x86_64_srcOutputBlocking,
	x86_64_outputPannedBlocking,
	x86_64_release,
};
