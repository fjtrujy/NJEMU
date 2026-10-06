/******************************************************************************

	timer.c

	Timer Management

******************************************************************************/

#include "cps2.h"
#include "common/cps2_me_sound_shadow.h"


/******************************************************************************
	Local Structures
******************************************************************************/

typedef struct timer_t
{
	float expire;
	int enable;
	int param;
	void (*callback)(int param);
} TIMER;

static TIMER ALIGN16_DATA timer[MAX_TIMER];


/******************************************************************************
	Local Variables
******************************************************************************/

static float time_slice;
static float base_time;
static float frame_base;
static float timer_ticks;
static float timer_left;

static int z80_suspended;
static uint64_t z80_sound_cycles;


/******************************************************************************
	Local Functions
******************************************************************************/

/*------------------------------------------------------
	Raster Interrupt
------------------------------------------------------*/

static void timer_set_raster_interrupt(int which, int scanline)
{
	int param = (which << 16) | scanline;

	timer_set(which, USECS_PER_SCANLINE * (float)scanline, param, cps2_raster_interrupt);
}


static void timer_set_vblank_interrupt(void)
{
	timer_set(VBLANK_INTERRUPT, USECS_PER_SCANLINE * 256, 0, cps2_vblank_interrupt);
}


/*------------------------------------------------------
	Sound Interrupt
------------------------------------------------------*/

static TIMER_CALLBACK( qsound_interrupt )
{
	if (!cps2_me_sound_irq(HOLD_LINE, cps2_timer_sound_time_us()))
		z80_set_irq_line(0, HOLD_LINE);
	timer_set(QSOUND_INTERRUPT, TIME_IN_HZ(251), 0, qsound_interrupt);
}


/******************************************************************************
	Global Functions
******************************************************************************/

/*------------------------------------------------------
	Reset Z80
------------------------------------------------------*/

void z80_set_reset_line(int state)
{
	bool changed = false;

	if (z80_suspended & SUSPEND_REASON_RESET)
	{
		if (state == CLEAR_LINE)
		{
			z80_suspended &= ~SUSPEND_REASON_RESET;
			changed = true;
		}
	}
	else if (state == ASSERT_LINE)
	{
		z80_suspended |= SUSPEND_REASON_RESET;
		changed = true;
	}

	if (!changed)
		return;
	if (!cps2_me_sound_z80_reset_line(state, cps2_timer_sound_time_us()))
	{
		/* A failed ME command can recover the pre-command worker snapshot,
		 * including its previous suspend flag. Reapply the requested reset-line
		 * state locally so CPU fallback observes the same transition. */
		cps2_timer_restore_z80_suspended(state == ASSERT_LINE);
		if (state == ASSERT_LINE)
			z80_reset();
	}
}

uint64_t cps2_timer_sound_time_us(void)
{
	return z80_sound_cycles / 8u;
}

bool cps2_timer_z80_suspended(void)
{
	return (z80_suspended & SUSPEND_REASON_RESET) != 0;
}

void cps2_timer_restore_z80_suspended(bool suspended)
{
	if (suspended)
		z80_suspended |= SUSPEND_REASON_RESET;
	else
		z80_suspended &= ~SUSPEND_REASON_RESET;
}


/*------------------------------------------------------
	Reset Timer
------------------------------------------------------*/

void timer_reset(void)
{
	memset(&timer, 0, sizeof(timer));

	base_time        = 0;
	frame_base       = 0;
	z80_suspended    = 0;
	z80_sound_cycles = 0;

	time_slice = 1000000.0 / FPS;

	timer_set(QSOUND_INTERRUPT, TIME_IN_HZ(251), 0, qsound_interrupt);
}


/*------------------------------------------------------
	Set Timer
------------------------------------------------------*/

void timer_set(int which, float duration, int param, void (*callback)(int param))
{
	timer[which].expire   = (base_time + frame_base) + duration;
	timer[which].param    = param;
	timer[which].enable   = 1;
	timer[which].callback = callback;
}


/*------------------------------------------------------
	Update CPU
------------------------------------------------------*/

void timer_update_cpu(void)
{
	int i;
	float time;
	extern int scanline1;
	extern int scanline2;

	frame_base = 0;
	timer_left = time_slice;

	if (scanline1 != RASTER_LINES) timer_set_raster_interrupt(RASTER_INTERRUPT1, scanline1);
	if (scanline2 != RASTER_LINES) timer_set_raster_interrupt(RASTER_INTERRUPT2, scanline2);
	timer_set_vblank_interrupt();
	if (driver->flags & 2) cps2_build_palette();

	while (timer_left > 0)
	{
		bool me_sound_slice;

		timer_ticks = timer_left;
		time = base_time + frame_base;

		for (i = 0; i < MAX_TIMER; i++)
		{
			if (timer[i].enable)
			{
				if (timer[i].expire - time <= 0)
				{
					timer[i].enable = 0;
					timer[i].callback(timer[i].param);
				}
			}
			if (timer[i].enable)
			{
				if (timer[i].expire - time < timer_ticks)
					timer_ticks = timer[i].expire - time;
			}
		}

		me_sound_slice = cps2_me_sound_main_slice_begin();
		m68000_execute((int)(timer_ticks * (11800000.0 / 1000000.0)));

		if (!z80_suspended)
		{
			int z80_cycles = (int)(timer_ticks * (8000000.0 / 1000000.0));
			uint64_t end_time = (z80_sound_cycles + (uint32_t)z80_cycles) / 8u;
			bool me_executed;

			if (me_sound_slice)
				me_executed = cps2_me_sound_main_slice_finish(
					(uint32_t)z80_cycles, end_time, true);
			else
				me_executed = cps2_me_sound_advance((uint32_t)z80_cycles, end_time);
			if (!me_executed)
				z80_execute(z80_cycles);
			z80_sound_cycles += (uint32_t)z80_cycles;
		}
		else if (me_sound_slice)
			(void)cps2_me_sound_main_slice_finish(0, cps2_timer_sound_time_us(), false);

		frame_base += timer_ticks;
		timer_left -= timer_ticks;
	}

	base_time += time_slice;
	if (base_time >= 1000000.0)
	{
		base_time -= 1000000.0;

		for (i = 0; i < MAX_TIMER; i++)
		{
			if (timer[i].enable)
				timer[i].expire -= 1000000.0;
		}
	}
}


/******************************************************************************
	Save/Load State
******************************************************************************/

#ifdef SAVE_STATE

STATE_SAVE( timer )
{
	int i;

	state_save_float(&base_time, 1);
	state_save_long(&z80_suspended, 1);

	for (i = 0; i < MAX_TIMER; i++)
	{
		state_save_float(&timer[i].expire, 1);
		state_save_long(&timer[i].enable, 1);
		state_save_long(&timer[i].param, 1);
	}
}

STATE_LOAD( timer )
{
	int i;

	state_load_float(&base_time, 1);
	state_load_long(&z80_suspended, 1);

	for (i = 0; i < MAX_TIMER; i++)
	{
		state_load_float(&timer[i].expire, 1);
		state_load_long(&timer[i].enable, 1);
		state_load_long(&timer[i].param, 1);
	}

	timer_left  = 0;
	timer_ticks = 0;
	frame_base  = 0;

	timer[QSOUND_INTERRUPT].callback  = qsound_interrupt;
	timer[VBLANK_INTERRUPT].callback  = cps2_vblank_interrupt;
	timer[RASTER_INTERRUPT1].callback = cps2_raster_interrupt;
	timer[RASTER_INTERRUPT2].callback = cps2_raster_interrupt;
}

#endif /* SAVE_STATE */
