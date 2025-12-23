#include <stdio.h>
#include <stdlib.h>

#include "common/audio_driver.h"

#include <SDL.h>

typedef struct desktop_audio {
    SDL_AudioDeviceID device;
    SDL_AudioSpec spec;
    SDL_AudioStream *stream;
} desktop_audio_t;

static void *desktop_init(void) {
	desktop_audio_t *desktop = (desktop_audio_t*)calloc(1, sizeof(desktop_audio_t));
    SDL_InitSubSystem(SDL_INIT_AUDIO);
	return desktop;
}

static void desktop_free(void *data) {
	desktop_audio_t *desktop = (desktop_audio_t*)data;
    if (desktop->device) {
        SDL_CloseAudioDevice(desktop->device);
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
    desired.format = AUDIO_S16SYS; // Floating-point audio format
    desired.channels = channels;
    desired.samples = samples; //frequency/10000 * 1024;
    desired.callback = NULL; // No callback; we use blocking audio

    desktop->device = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, SDL_AUDIO_ALLOW_ANY_CHANGE);
    if (desktop->device <= 0) {
        fprintf(stderr, "Failed to open audio device: %s\n", SDL_GetError());
        return false;
    }

    desktop->spec = obtained;
    
    // Create an audio stream for sample conversion
    desktop->stream = SDL_NewAudioStream(AUDIO_S16SYS, channels, frequency,
                                        obtained.format, obtained.channels, obtained.freq);
    if (!desktop->stream) {
        fprintf(stderr, "Failed to create audio stream: %s\n", SDL_GetError());
        SDL_CloseAudioDevice(desktop->device);
        return false;
    }
    
    SDL_PauseAudioDevice(desktop->device, 0); // Start audio playback
    return true;
}

static bool desktop_chReserve(void *data, uint16_t samplecount, uint8_t channels) {
    // This function may be redundant if `desktop_chSRCReserve` handles initialization
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
    
    // Add data to the stream for conversion
    if (SDL_AudioStreamPut(desktop->stream, buffer, size) < 0) {
        fprintf(stderr, "Failed to queue audio stream data: %s\n", SDL_GetError());
        return;
    }

    int available = SDL_AudioStreamAvailable(desktop->stream);
    void *out_buffer = malloc(available);

    int len = SDL_AudioStreamGet(desktop->stream, out_buffer, available);
    if (len > 0) {
        SDL_QueueAudio(desktop->device, out_buffer, len);
    }
    free(out_buffer);

    // SDL queuing is not limited, so make sure to wait until is it empty enough again
    while (SDL_GetQueuedAudioSize(desktop->device) > size * 8)
        SDL_Delay(100);
}

static void desktop_outputPannedBlocking(void *data, int leftvol, int rightvol, void *buffer, uint32_t size) {
    // In a stereo configuration, pan the audio manually
    desktop_audio_t *desktop = (desktop_audio_t*)data;

    if (!desktop->device || desktop->spec.channels < 2) {
        fprintf(stderr, "Audio device not initialized or not stereo\n");
        return;
    }

    float *float_buffer = (float*)buffer;
    int samples = desktop->spec.samples * desktop->spec.channels;
    for (int i = 0; i < samples; i += 2) {
        float_buffer[i] *= (leftvol / 100.0f);
        float_buffer[i + 1] *= (rightvol / 100.0f);
    }

    SDL_QueueAudio(desktop->device, float_buffer, samples * sizeof(float));

    // SDL queuing is not limited, so make sure to wait until is it empty enough again
    while (SDL_GetQueuedAudioSize(desktop->device) > samples*sizeof(int16_t) * 8)
        SDL_Delay(100);
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
