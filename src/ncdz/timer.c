/******************************************************************************

	timer.c

	Timer Management

	For high performance, all processing uses int type to avoid speed penalty.
	Using float/double would significantly degrade performance in Crossword.
	(YM2610 timer interrupt count would become extremely slow)

******************************************************************************/

#include "ncdz.h"


#define CPU_NOTACTIVE	-1


/******************************************************************************
	Macros
******************************************************************************/

/*------------------------------------------------------
	Get CPU elapsed time (unit: microseconds)
------------------------------------------------------*/

#define cpu_elapsed_time(cpunum)	\
	(cpu[cpunum].cycles - *cpu[cpunum].icount) / cpu[cpunum].cycles_per_usec


/******************************************************************************
	Local Structures
******************************************************************************/

typedef struct timer_t
{
	int expire;
	int enable;
	int param;
	void (*callback)(int param);
} TIMER;

typedef struct cpuinfo_t
{
	int  (*execute)(int cycles);
	int32_t  *icount;
	int  cycles_per_usec;
	int  cycles;
	int  suspended;
} CPUINFO;


static TIMER ALIGN16_DATA timer[MAX_TIMER];
static CPUINFO ALIGN16_DATA cpu[MAX_CPU];


/******************************************************************************
	Local Variables
******************************************************************************/

static int global_offset;
static int base_time;
static int frame_base;
static int timer_ticks;
static int timer_left;
static int active_cpu;
static int scanline;


/******************************************************************************
	Prototypes
******************************************************************************/

void (*timer_update_cpu)(void);
static void timer_update_cpu_normal(void);
static void timer_update_cpu_raster(void);


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
		cpu[cpunum].cycles = timer_ticks * cpu[cpunum].cycles_per_usec;
		cpu[cpunum].execute(cpu[cpunum].cycles);
		active_cpu = CPU_NOTACTIVE;
	}
}


/*------------------------------------------------------
	CPU spin trigger
------------------------------------------------------*/

static void cpu_spin_trigger(int param)
{
	timer_suspend_cpu(param, 1, SUSPEND_REASON_SPIN);
}


/*------------------------------------------------------
	Get current time below seconds (unit: microseconds)
------------------------------------------------------*/

static int getabsolutetime(void)
{
	int time = base_time + frame_base;

	if (active_cpu != CPU_NOTACTIVE)
		time += cpu_elapsed_time(active_cpu);

	return time;
}


/******************************************************************************
	Global Functions
******************************************************************************/

/*------------------------------------------------------
	Set Z80 reset line
------------------------------------------------------*/

void z80_set_reset_line(int state)
{
	if (cpu[CPU_Z80].suspended & SUSPEND_REASON_RESET)
	{
		if (state == CLEAR_LINE)
			cpu[CPU_Z80].suspended &= ~SUSPEND_REASON_RESET;
	}
	else if (state == ASSERT_LINE)
	{
		cpu[CPU_Z80].suspended |= SUSPEND_REASON_RESET;
		z80_reset();
	}
}


/*------------------------------------------------------
	Reset timer
------------------------------------------------------*/

void timer_reset(void)
{
	global_offset = 0;
	base_time = 0;
	frame_base = 0;

	active_cpu = CPU_NOTACTIVE;
	memset(&timer, 0, sizeof(timer));

	cpu[CPU_M68000].execute   = m68000_execute;
	cpu[CPU_M68000].icount    = &C68K.ICount;
	cpu[CPU_M68000].cycles    = 0;
	cpu[CPU_M68000].suspended = 0;
	cpu[CPU_M68000].cycles_per_usec = 12;

	cpu[CPU_Z80].execute   = z80_execute;
	cpu[CPU_Z80].icount    = &CZ80.ICount;
	cpu[CPU_Z80].cycles    = 0;
	cpu[CPU_Z80].suspended = 0;
	cpu[CPU_Z80].cycles_per_usec = 6;
}


/*------------------------------------------------------
	Set CPU update handler
------------------------------------------------------*/

void timer_set_update_handler(void)
{
	if (neogeo_driver_type == NORMAL)
		timer_update_cpu = timer_update_cpu_normal;
	else
		timer_update_cpu = timer_update_cpu_raster;
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
	Enable/disable timer
------------------------------------------------------*/

int timer_enable(int which, int enable)
{
	int old = timer[which].enable;

	timer[which].enable = enable;
	return old;
}


/*------------------------------------------------------
	Set timer
------------------------------------------------------*/

void timer_adjust(int which, int duration, int param, void (*callback)(int param))
{
	int time = getabsolutetime();

	timer[which].expire = time + duration;
	timer[which].param = param;
	timer[which].callback = callback;

	if (active_cpu != CPU_NOTACTIVE)
	{
		// If CPU is executing, discard remaining cycles
		int cycles_left = *cpu[active_cpu].icount;
		int time_left = cycles_left / cpu[active_cpu].cycles_per_usec;

		if (duration < timer_left)
		{
			timer_ticks -= time_left;
			cpu[active_cpu].cycles -= cycles_left;
			*cpu[active_cpu].icount = 0;

			if (active_cpu == CPU_Z80)
			{
				// If CPU2, stop CPU1 and adjust CPU1's remaining cycles
				if (!timer[CPUSPIN_TIMER].enable)
				{
					timer_suspend_cpu(CPU_M68000, 0, SUSPEND_REASON_SPIN);
					timer[CPUSPIN_TIMER].enable = 1;
					timer[CPUSPIN_TIMER].expire = time + time_left;
					timer[CPUSPIN_TIMER].param = CPU_M68000;
					timer[CPUSPIN_TIMER].callback = cpu_spin_trigger;
				}
			}
		}
	}
}


/*------------------------------------------------------
	Set timer
------------------------------------------------------*/

void timer_set(int which, int duration, int param, void (*callback)(int param))
{
	timer[which].enable = 1;
	timer_adjust(which, duration, param, callback);
}


/*------------------------------------------------------
	Get current emulation time (unit: seconds)
------------------------------------------------------*/

float timer_get_time(void)
{
	int time = getabsolutetime();

	return (float)global_offset + (float)time / 1000000.0;
}


/*------------------------------------------------------
	Get current scanline
------------------------------------------------------*/

int timer_getscanline(void)
{
	if (neogeo_driver_type == NORMAL)
		return 1 + (frame_base >> 6);
	else
		return scanline;
}


/*------------------------------------------------------
	Update CPU
------------------------------------------------------*/

static void timer_update_cpu_normal(void)
{
	int i, time;

	frame_base = 0;
	timer_left = TICKS_PER_FRAME;

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

		if (Loop != LOOP_EXEC) return;

		for (i = 0; i < MAX_CPU; i++)
			cpu_execute(i);

		frame_base += timer_ticks;
		timer_left -= timer_ticks;
	}

	neogeo_interrupt();

	base_time += TICKS_PER_FRAME;
	if (base_time >= 1000000)
	{
		global_offset++;
		base_time -= 1000000;

		for (i = 0; i < MAX_TIMER; i++)
		{
			if (timer[i].enable)
				timer[i].expire -= 1000000;
		}
	}

	if (!skip_this_frame()) neogeo_screenrefresh();
}


/*------------------------------------------------------
	Update CPU (for raster driver)
------------------------------------------------------*/

static void timer_update_cpu_raster(void)
{
	int i, time;

	frame_base = 0;
	timer_left = 0;

	for (scanline = 1; scanline <= RASTER_LINES; scanline++)
	{
		timer_left += USECS_PER_SCANLINE;

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

			if (Loop != LOOP_EXEC) return;

			cpu_execute(CPU_M68000);
			cpu_execute(CPU_Z80);

			frame_base += timer_ticks;
			timer_left -= timer_ticks;
		}

		neogeo_raster_interrupt(scanline);
	}

	base_time += TICKS_PER_FRAME;
	if (base_time >= 1000000)
	{
		global_offset++;
		base_time -= 1000000;

		for (i = 0; i < MAX_TIMER; i++)
		{
			if (timer[i].enable)
				timer[i].expire -= 1000000;
		}
	}

	if (!skip_this_frame()) neogeo_screenrefresh();
}


/*------------------------------------------------------
	Update sub CPU only (used during loading screen)
------------------------------------------------------*/

void timer_update_subcpu(void)
{
	int i, time;

	frame_base = 0;
	timer_left = TICKS_PER_FRAME;

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

		cpu_execute(CPU_Z80);

		frame_base += timer_ticks;
		timer_left -= timer_ticks;
	}

	base_time += TICKS_PER_FRAME;
	if (base_time >= 1000000)
	{
		global_offset++;
		base_time -= 1000000;

		for (i = 0; i < MAX_TIMER; i++)
		{
			if (timer[i].enable)
				timer[i].expire -= 1000000;
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

	state_save_long(&global_offset, 1);
	state_save_long(&base_time, 1);

	state_save_long(&cpu[0].suspended, 1);
	state_save_long(&cpu[1].suspended, 1);

	for (i = 0; i < MAX_TIMER; i++)
	{
		state_save_long(&timer[i].expire, 1);
		state_save_long(&timer[i].enable, 1);
		state_save_long(&timer[i].param, 1);
	}
}

STATE_LOAD( timer )
{
	int i;

	state_load_long(&global_offset, 1);
	state_load_long(&base_time, 1);

	state_load_long(&cpu[0].suspended, 1);
	state_load_long(&cpu[1].suspended, 1);

	for (i = 0; i < MAX_TIMER; i++)
	{
		state_load_long(&timer[i].expire, 1);
		state_load_long(&timer[i].enable, 1);
		state_load_long(&timer[i].param, 1);
	}

	timer_left  = 0;
	timer_ticks = 0;
	frame_base  = 0;
	active_cpu = CPU_NOTACTIVE;

	timer[YM2610_TIMERA].callback    = timer_callback_2610;
	timer[YM2610_TIMERB].callback    = timer_callback_2610;
	timer[SOUNDLATCH_TIMER].callback = neogeo_sound_write;
	timer[CPUSPIN_TIMER].callback    = cpu_spin_trigger;
}

#endif /* SAVE_STATE */
