/******************************************************************************

	timer.c

	Timer Management

******************************************************************************/

#include "cps1.h"


#define CPU_NOTACTIVE	-1


/******************************************************************************
	Macros
******************************************************************************/

/*------------------------------------------------------
	Get CPU Elapsed Time (unit: microseconds)
------------------------------------------------------*/

#define cpu_elapsed_time(cpunum)	\
	(float)(cpu[cpunum].cycles - *cpu[cpunum].icount) * cpu[cpunum].cycles_to_usec


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

typedef struct cpuinfo_t
{
	int (*execute)(int cycles);
	int32_t *icount;
	float usec_to_cycles;
	float cycles_to_usec;
	int cycles;
	int suspended;
} CPUINFO;


static TIMER timer[MAX_TIMER];
static CPUINFO cpu[MAX_CPU];


/******************************************************************************
	Local Variables
******************************************************************************/

static float time_slice;
static float base_time;
static float frame_base;
static float timer_ticks;
static float timer_left;
static int active_cpu;
static uint32_t current_frame;


/******************************************************************************
	Local Functions
******************************************************************************/

/*------------------------------------------------------
	Execute CPU
------------------------------------------------------*/

static void cpu_execute(int cpunum)
{
	if (!cpu[cpunum].suspended)
	{
		active_cpu = cpunum;
		cpu[cpunum].cycles = (int)(timer_ticks * cpu[cpunum].usec_to_cycles);
		cpu[cpunum].execute(cpu[cpunum].cycles);
		active_cpu = CPU_NOTACTIVE;
	}
}


/*------------------------------------------------------
	Release CPU Spin (Trigger)
------------------------------------------------------*/

static void cpu_spin_trigger(int param)
{
	timer_suspend_cpu(param, 1, SUSPEND_REASON_SPIN);
}


/*------------------------------------------------------
	Get Current Sub-second Time (unit: microseconds)
------------------------------------------------------*/

static float getabsolutetime(void)
{
	float time = base_time + frame_base;

	if (active_cpu != CPU_NOTACTIVE)
		time += cpu_elapsed_time(active_cpu);

	return time;
}


/*------------------------------------------------------
	VBlank Interrupt
------------------------------------------------------*/

static void timer_set_vblank_interrupt(void)
{
	timer_set(VBLANK_INTERRUPT, USECS_PER_SCANLINE * 256, 0, cps1_vblank_interrupt);
}


/*------------------------------------------------------
	Sound Interrupt
------------------------------------------------------*/

static TIMER_CALLBACK( qsound_interrupt )
{
	z80_set_irq_line(0, HOLD_LINE);
	timer_set(QSOUND_INTERRUPT, (int)TIME_IN_HZ(250), 0, qsound_interrupt);
}


/******************************************************************************
	Global Functions
******************************************************************************/

/*------------------------------------------------------
	Timer Reset
------------------------------------------------------*/

void timer_reset(void)
{
	base_time = 0;
	frame_base = 0;
	current_frame = 0;

	active_cpu = CPU_NOTACTIVE;
	memset(&timer, 0, sizeof(timer));

	time_slice = 1000000.0 / FPS;

	cpu[CPU_M68000].execute   = m68000_execute;
	cpu[CPU_M68000].icount    = &C68K.ICount;
	cpu[CPU_M68000].cycles    = 0;
	cpu[CPU_M68000].suspended = 0;
	cpu[CPU_M68000].usec_to_cycles = 10000000.0/1000000.0;
	cpu[CPU_M68000].cycles_to_usec = 1000000.0/10000000.0;

	cpu[CPU_Z80].execute   = z80_execute;
	cpu[CPU_Z80].icount    = &CZ80.ICount;
	cpu[CPU_Z80].cycles    = 0;
	cpu[CPU_Z80].suspended = 0;
	cpu[CPU_Z80].usec_to_cycles = 3579545.0/1000000.0;
	cpu[CPU_Z80].cycles_to_usec = 1000000.0/3579545.0;

	if (machine_sound_type == SOUND_QSOUND)
	{
		cpu[CPU_Z80].usec_to_cycles = 8000000.0/1000000.0;
		cpu[CPU_Z80].cycles_to_usec = 1000000.0/8000000.0;
		timer_set(QSOUND_INTERRUPT, (int)TIME_IN_HZ(250), 0, qsound_interrupt);
	}
}


/*------------------------------------------------------
	Suspend CPU
------------------------------------------------------*/

void timer_suspend_cpu(int cpunum, int state, int reason)
{
	if (state == 0)
		cpu[cpunum].suspended |= reason;
	else
		cpu[cpunum].suspended &= ~reason;
}


/*------------------------------------------------------
	Reset CPU
------------------------------------------------------*/

void timer_set_resetline(int cpunum, int state)
{
	if (state == ASSERT_LINE)
		cpu[cpunum].suspended |= SUSPEND_REASON_RESET;
	else
		cpu[cpunum].suspended &= ~SUSPEND_REASON_RESET;
}


/*------------------------------------------------------
	Get CPU Suspend Status
------------------------------------------------------*/

int timer_get_cpu_status(int cpunum)
{
	return cpu[cpunum].suspended;
}


/*------------------------------------------------------
	Enable/Disable Timer
------------------------------------------------------*/

int timer_enable(int which, int enable)
{
	int old = timer[which].enable;

	timer[which].enable = enable;
	return old;
}


/*------------------------------------------------------
	Adjust Timer
------------------------------------------------------*/

void timer_adjust(int which, float duration, int param, void (*callback)(int param))
{
	float time = getabsolutetime();

	timer[which].expire = time + duration;
	timer[which].param = param;
	timer[which].callback = callback;

	if (active_cpu != CPU_NOTACTIVE)
	{
		// If CPU is running, discard remaining cycles
		int cycles_left = *cpu[active_cpu].icount;
		float time_left = cycles_left * cpu[active_cpu].cycles_to_usec;

		if (duration < timer_left)
		{
			timer_ticks -= time_left;
			cpu[active_cpu].cycles -= cycles_left;
			*cpu[active_cpu].icount = 0;

			if (active_cpu == CPU_Z80)
			{
				// For CPU2, stop CPU1 and adjust the extra cycles consumed by CPU1
				if (!timer[CPU1_SPIN_TIMER].enable)
				{
					timer_suspend_cpu(CPU_M68000, 0, SUSPEND_REASON_SPIN);
					timer[CPU1_SPIN_TIMER].enable = 1;
					timer[CPU1_SPIN_TIMER].expire = time + time_left;
					timer[CPU1_SPIN_TIMER].param = CPU_M68000;
					timer[CPU1_SPIN_TIMER].callback = cpu_spin_trigger;
				}
			}
		}
	}
}


/*------------------------------------------------------
	Set Timer
------------------------------------------------------*/

void timer_set(int which, float duration, int param, void (*callback)(int param))
{
	timer[which].enable = 1;
	timer_adjust(which, duration, param, callback);
}


/*------------------------------------------------------
	Get Current Frame
------------------------------------------------------*/

uint32_t timer_get_currentframe(void)
{
	return current_frame;
}


/*------------------------------------------------------
	Update CPU
------------------------------------------------------*/

void timer_update_cpu(void)
{
	int i;
	float time;

	frame_base = 0;
	timer_left = time_slice;

	timer_set_vblank_interrupt();

	while (timer_left > 0)
	{
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

		for (i = 0; i < MAX_CPU; i++)
			cpu_execute(i);

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

	current_frame++;
}


/******************************************************************************
	Save/Load State
******************************************************************************/

#ifdef SAVE_STATE

STATE_SAVE( timer )
{
	int i;

	state_save_float(&base_time, 1);
	state_save_long(&current_frame, 1);

	state_save_long(&cpu[0].suspended, 1);
	state_save_long(&cpu[1].suspended, 1);

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
	state_load_long(&current_frame, 1);

	state_load_long(&cpu[0].suspended, 1);
	state_load_long(&cpu[1].suspended, 1);

	for (i = 0; i < MAX_TIMER; i++)
	{
		state_load_float(&timer[i].expire, 1);
		state_load_long(&timer[i].enable, 1);
		state_load_long(&timer[i].param, 1);
	}

	timer_left  = 0;
	timer_ticks = 0;
	frame_base  = 0;
	active_cpu = CPU_NOTACTIVE;

	if (machine_sound_type == SOUND_QSOUND)
	{
		timer[QSOUND_INTERRUPT].callback = qsound_interrupt;
	}
	else
	{
		timer[YM2151_TIMERA].callback = timer_callback_2151_a;
		timer[YM2151_TIMERB].callback = timer_callback_2151_b;
	}
	timer[CPU1_SPIN_TIMER].callback  = cpu_spin_trigger;
	timer[CPU2_SPIN_TIMER].callback  = cpu_spin_trigger;
	timer[VBLANK_INTERRUPT].callback = cps1_vblank_interrupt;
}

#endif /* SAVE_STATE */
