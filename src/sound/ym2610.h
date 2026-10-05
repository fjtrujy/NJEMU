/***************************************************************************

  ym2610.h

  Header file for software emulation for YAMAHA YM-2610 sound generator

***************************************************************************/

#ifndef _YM2610_H_
#define _YM2610_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "emucfg.h"
#include "common/state.h"

/* for busy flag emulation , function FM_GET_TIME_NOW() should be */
/* return the present time in second unit with (double) value     */
/* in timer.c */
#define FM_GET_TIME_NOW() timer_get_time()

typedef int16_t FMSAMPLE;
typedef int32_t FMSAMPLE_MIX;

typedef void (*FM_TIMERHANDLER)(int channel, int count, double stepTime);
typedef void (*FM_IRQHANDLER)(int irq);

typedef struct ym2610_context ym2610_context_t;
typedef void (*YM2610_CONTEXT_TIMERHANDLER)(void *opaque, int channel,
	int count, double stepTime);
typedef void (*YM2610_CONTEXT_IRQHANDLER)(void *opaque, int irq);

#if (EMU_SYSTEM == MVS)
#define YM2610_PCM_WINDOW_ADPCMA_CHANNELS 6u
#define YM2610_PCM_WINDOW_ADPCMB_SEGMENTS 2u
#define YM2610_PCM_WINDOW_SEGMENT_BYTES 512u

typedef struct ym2610_pcm_window_segment
{
	uint32_t base_byte;
	uint16_t size;
	uint16_t reserved;
	uint8_t data[YM2610_PCM_WINDOW_SEGMENT_BYTES];
} ym2610_pcm_window_segment_t;

typedef struct ym2610_pcm_window
{
	uint32_t samples;
	uint32_t adpcmb_segment_count;
	ym2610_pcm_window_segment_t adpcma[YM2610_PCM_WINDOW_ADPCMA_CHANNELS];
	ym2610_pcm_window_segment_t adpcmb[YM2610_PCM_WINDOW_ADPCMB_SEGMENTS];
} ym2610_pcm_window_t;
#endif

size_t YM2610ContextSize(void);
size_t YM2610ContextAlignment(void);
void YM2610ContextInit(ym2610_context_t *context, int baseclock, int samplerate,
	void *pcmroma, int pcmsizea,
#if (EMU_SYSTEM == MVS)
	void *pcmromb, int pcmsizeb,
#endif
	YM2610_CONTEXT_TIMERHANDLER TimerHandler,
	YM2610_CONTEXT_IRQHANDLER IRQHandler, void *opaque);
void YM2610ContextSetCallbacks(ym2610_context_t *context,
	YM2610_CONTEXT_TIMERHANDLER TimerHandler,
	YM2610_CONTEXT_IRQHANDLER IRQHandler, void *opaque);
void YM2610ContextReset(ym2610_context_t *context);
void YM2610ContextUpdate(ym2610_context_t *context, int32_t **buffer, int length);
#if defined(YM2610_CONTEXT_TEST_REFERENCE)
void YM2610ContextSetForceFullFmForTest(bool enabled);
void YM2610ContextSetForceDisabledLfoAdvanceForTest(bool enabled);
void YM2610ContextSetForcePairedTlTableForTest(bool enabled);
#endif
#if (EMU_SYSTEM == MVS)
void YM2610ContextEnablePcmWindowSource(ym2610_context_t *context,
	uint32_t pcmsizea, uint32_t pcmsizeb);
bool YM2610ContextPreparePcmWindow(ym2610_context_t *context, uint32_t length,
	ym2610_pcm_window_t *window);
bool YM2610DefaultFillPcmWindow(ym2610_pcm_window_t *window);
bool YM2610DefaultPreparePcmWindow(uint32_t length, ym2610_pcm_window_t *window);
bool YM2610ContextCloneForPcmWindow(ym2610_context_t *destination,
	const ym2610_context_t *source);
bool YM2610DefaultCloneForPcmWindow(ym2610_context_t *destination);
bool YM2610ContextRestoreFromPcmWindow(ym2610_context_t *destination,
	const ym2610_context_t *source);
bool YM2610DefaultRestoreFromPcmWindow(const ym2610_context_t *source);
bool YM2610ContextUpdatePcmWindow(ym2610_context_t *context, int32_t **buffer,
	int length, const ym2610_pcm_window_t *window);
#endif
int YM2610ContextWrite(ym2610_context_t *context, int addr, uint8_t value);
uint8_t YM2610ContextRead(ym2610_context_t *context, int addr);
int YM2610ContextTimerOver(ym2610_context_t *context, int channel);

void YM2610Init(int baseclock, void *pcmroma, int pcmsizea,
#if (EMU_SYSTEM == MVS)
				void *pcmromb, int pcmsizeb,
#endif
				FM_TIMERHANDLER TimerHandler,
				FM_IRQHANDLER IRQHandler);

void YM2610Update(int32_t **buffer, int length);
void YM2610Reset(void);
int YM2610Write(int addr, uint8_t value);
uint8_t YM2610Read(int addr);
int YM2610TimerOver(int channel);
void YM2610_set_samplerate(void);

#ifdef SAVE_STATE
STATE_SAVE( ym2610 );
STATE_LOAD( ym2610 );
#endif

#endif /* _YM2610_H_ */
