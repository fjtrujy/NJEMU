/***************************************************************************

	2610intf.c

	YM2610 interface

***************************************************************************/

#include "sound/2610intf.h"
#include "common/sound.h"
#include "common/neogeo_sound_io.h"
#include <limits.h>

#if (EMU_SYSTEM == MVS)
#include "mvs/me_sound_profile.h"
#include "mvs/me_sound_shadow.h"
#include "mvs/memintrf.h"
#include "mvs/timer.h"
#elif (EMU_SYSTEM == NCDZ)
#include "ncdz/memintrf.h"
#include "ncdz/timer.h"
#endif


/***************************************************************************
	Function prototypes
 **************************************************************************/

static void TimerHandler(int channel, int count, double stepTime);
#if (EMU_SYSTEM == MVS)
static void YM2610_shadow_update(int32_t **buffer, int length);
#endif

#if (EMU_SYSTEM == MVS)
static void YM2610_shadow_update(int32_t **buffer, int length)
{
	bool shadow_started = mvs_me_sound_shadow_ym_render_begin((uint32_t)length,
		timer_get_time_us());

	YM2610Update(buffer, length);
	if (shadow_started)
		mvs_me_sound_shadow_ym_render_completed(buffer, (uint32_t)length,
			YM2610Read(2));
}
#endif


/*------------------------------------------------------
	Start YM2610 emulation
 -----------------------------------------------------*/

void YM2610_sh_start(void)
{
#if (EMU_SYSTEM == MVS)
	void *pcmbufa, *pcmbufb;
	int pcmsizea, pcmsizeb;

	pcmbufa = (void *)memory_region_sound1;
	pcmsizea = memory_length_sound1;

	if (memory_length_sound2)
	{
		pcmbufb = (void *)memory_region_sound2;
		pcmsizeb = memory_length_sound2;
	}
	else
	{
		pcmbufb = (void *)memory_region_sound1;
		pcmsizeb = memory_length_sound1;
	}

	YM2610Init(8000000, pcmbufa, pcmsizea, pcmbufb, pcmsizeb, TimerHandler, neogeo_sound_irq);
	sound->callback = YM2610_shadow_update;
#else
	YM2610Init(8000000, memory_region_sound1, memory_length_sound1, TimerHandler, neogeo_sound_irq);
#endif
}


/*------------------------------------------------------
	Stop YM2610 emulation
 -----------------------------------------------------*/

void YM2610_sh_stop(void)
{
}


/*------------------------------------------------------
	Reset YM2610 emulation
 -----------------------------------------------------*/

void YM2610_sh_reset(void)
{
	YM2610Reset();
}

#if (EMU_SYSTEM == MVS)
bool YM2610_restore_from_pcm_window_context(const ym2610_context_t *source,
	const uint64_t timer_remaining[2], const uint8_t timer_enabled[2])
{
	uint32_t channel;

	if (!source || !timer_remaining || !timer_enabled)
		return false;
	for (channel = 0; channel < 2u; channel++)
	{
		if (timer_enabled[channel] && timer_remaining[channel] > (uint64_t)INT_MAX)
			return false;
	}
	if (!YM2610DefaultRestoreFromPcmWindow(source))
		return false;
	for (channel = 0; channel < 2u; channel++)
	{
		(void)timer_enable((int)channel, 0);
		if (timer_enabled[channel])
		{
			(void)timer_enable((int)channel, 1);
			timer_adjust((int)channel, (int)timer_remaining[channel],
				(int)channel, timer_callback_2610);
		}
	}
	return true;
}
#endif


/*------------------------------------------------------
	Read YM2610 status port A
 -----------------------------------------------------*/

READ8_HANDLER( YM2610_status_port_A_r )
{
	return YM2610Read(0);
}


/*------------------------------------------------------
	Read YM2610 status port B
 -----------------------------------------------------*/

READ8_HANDLER( YM2610_status_port_B_r )
{
	return YM2610Read(2);
}

READ8_HANDLER( YM2610_read_port_r )
{
	return YM2610Read(1);
}


/*------------------------------------------------------
	Write YM2610 control port A
 -----------------------------------------------------*/

WRITE8_HANDLER( YM2610_control_port_A_w )
{
	YM2610Write(0, data);
}


/*------------------------------------------------------
	Write YM2610 control port B
 -----------------------------------------------------*/

WRITE8_HANDLER( YM2610_control_port_B_w )
{
	YM2610Write(2, data);
}


/*------------------------------------------------------
	Write YM2610 data port A
 -----------------------------------------------------*/

WRITE8_HANDLER( YM2610_data_port_A_w )
{
	YM2610Write(1, data);
}


/*------------------------------------------------------
	Write YM2610 data port B
 -----------------------------------------------------*/

WRITE8_HANDLER( YM2610_data_port_B_w )
{
	YM2610Write(3, data);
}


/*------------------------------------------------------
	Timer callback function
 -----------------------------------------------------*/

void timer_callback_2610(int param)
{
#if (EMU_SYSTEM == MVS)
	mvs_me_sound_profile_event(param == 0 ? MVS_ME_SOUND_PROFILE_YM_TIMER_A :
		MVS_ME_SOUND_PROFILE_YM_TIMER_B);
	mvs_me_sound_shadow_ym_timer((uint32_t)param, timer_get_time_us());
#endif
	YM2610TimerOver(param);
#if (EMU_SYSTEM == MVS)
	mvs_me_sound_shadow_ym_timer_completed();
#endif
}


/*------------------------------------------------------
	YM2610 timer handler
 -----------------------------------------------------*/

static void TimerHandler(int channel, int count, double stepTime)
{
	if (count == 0)
	{
		/* Reset FM Timer */
		timer_enable(channel, 0);
	}
	else
	{
		/* Start FM Timer */
#if 0
		float time_usec = (float)((double)count * SEC_TO_USEC(stepTime));
#else
		// For convenience, use int instead of float
		int time_usec = count * SEC_TO_USEC(stepTime);
#endif

		if (!timer_enable(channel, 1))
			timer_adjust(channel, time_usec, channel, timer_callback_2610);
	}
}
