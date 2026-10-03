/***************************************************************************

  ym2610.h

  Header file for software emulation for YAMAHA YM-2610 sound generator

***************************************************************************/

#ifndef _YM2610_H_
#define _YM2610_H_

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
int YM2610ContextWrite(ym2610_context_t *context, int addr, uint8_t value);
uint8_t YM2610ContextRead(ym2610_context_t *context, int addr);
int YM2610ContextTimerOver(ym2610_context_t *context, int channel);

void YM2610Init(int baseclock, void *pcmroma, int pcmsizea,
#if (EMU_SYSTEM == MVS)
				void *pcmromb, int pcmsizeb,
#endif
				FM_TIMERHANDLER TimerHandler,
				FM_IRQHANDLER IRQHandler);

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
