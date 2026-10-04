/******************************************************************************

	sound.h

	Sound Thread

******************************************************************************/

#ifndef COMMON_SOUND_H
#define COMMON_SOUND_H

#include <stdint.h>
#include "emucfg.h"

#define SOUND_SAMPLES_24000	(400*2)
#define SOUND_SAMPLES_44100	(736*2)
#define SOUND_SAMPLES_48000	(800)

/* All platform audio backends consume interleaved stereo PCM.  The `channels`
 * member below describes the chip-side synthesis layout: classic CPS1 YM2151
 * mixes to one channel and sndintrf.c expands that mono stream to L/R before
 * handing it to the platform. */
#define SOUND_OUTPUT_CHANNELS	2

#if (EMU_SYSTEM == CPS2)
#define SOUND_BUFFER_SIZE	((400*2)*2)	// 24KHz Fixed
#else
#define SOUND_BUFFER_SIZE	((736*2)*2)
#endif


struct sound_t
{
	int stack;
	int channels; /* synthesis channels, not platform output channels */
	int frequency;
	int samples;
	void (*update)(int16_t *buffer);
	void (*callback)(int32_t **buffer, int length);
};

static inline uint32_t sound_output_sample_count(const struct sound_t *info)
{
	return (uint32_t)info->samples * SOUND_OUTPUT_CHANNELS;
}

static inline uint32_t sound_output_buffer_bytes(const struct sound_t *info)
{
	return sound_output_sample_count(info) * sizeof(int16_t);
}

#define MAXOUT		(+32767)
#define MINOUT		(-32768)

#define Limit(val, max, min)			\
{										\
	if (val > max) val = max;			\
	else if (val < min) val = min;		\
}

extern struct sound_t *sound;

void sound_thread_init(void);
void sound_thread_exit(void);
void sound_thread_enable(int enable);
void sound_thread_pause(int pause);
void sound_thread_set_volume(void);
void sound_thread_reset_producer(void);
void sound_thread_notify_power_event(int suspended);
int sound_thread_start(void);
void sound_thread_stop(void);

#endif /* COMMON_SOUND_H */
