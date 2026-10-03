/******************************************************************************

	timer.c

	Timer Management

	For high performance, all processing uses int type to avoid speed penalty.
	Using float/double would significantly degrade performance in Crossword.
	(YM2610 timer interrupt count would become extremely slow)

******************************************************************************/

#include "mvs.h"
#include "common/emulator_runtime.h"
#include "include/cpuintrf.h"
#include "me_sound_profile.h"
#include "me_sound_shadow.h"


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
	int (*execute)(int cycles);
	int32_t *icount;
	int cycles_per_usec;
	int cycles;
	int suspended;
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
static uint32_t sound_poll_pc;
static uint16_t sound_poll_status;
static uint8_t sound_poll_reads;
static int sound_poll_slice_ended;


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
		uint64_t start = mvs_me_sound_profile_now_us();
		uint64_t z80_end_time = 0;

		if (cpunum == CPU_Z80)
			mvs_me_sound_shadow_z80_slice_begin();

		if (cpunum == CPU_M68000)
		{
			sound_poll_pc = 0;
			sound_poll_status = 0;
			sound_poll_reads = 0;
			sound_poll_slice_ended = 0;
		}
		active_cpu = cpunum;
		cpu[cpunum].cycles = timer_ticks * cpu[cpunum].cycles_per_usec;
		cpu[cpunum].execute(cpu[cpunum].cycles);
		if (cpunum == CPU_Z80)
			z80_end_time = timer_get_time_us();
		active_cpu = CPU_NOTACTIVE;
		mvs_me_sound_profile_add_time(
			cpunum == CPU_M68000 ? MVS_ME_SOUND_PROFILE_M68000 : MVS_ME_SOUND_PROFILE_Z80,
			mvs_me_sound_profile_now_us() - start);

		if (cpunum == CPU_Z80)
		{
			cz80_state_t state;
			uint32_t banks[4];

			Cz80_Get_State(&CZ80, &state);
			neogeo_get_z80_shadow_state(banks, NULL, NULL, NULL);
			mvs_me_sound_shadow_z80_slice_completed((uint32_t)cpu[cpunum].cycles,
				z80_end_time, &state, banks, memory_region_cpu2);
		}
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
	Reset timer
------------------------------------------------------*/

void timer_reset(void)
{
	mvs_me_sound_profile_reset();
	global_offset = 0;
	base_time     = 0;
	frame_base    = 0;

	active_cpu = CPU_NOTACTIVE;
	sound_poll_pc = 0;
	sound_poll_status = 0;
	sound_poll_reads = 0;
	sound_poll_slice_ended = 0;
	memset(&timer, 0, sizeof(timer));

	cpu[CPU_M68000].execute   = m68000_execute;
	cpu[CPU_M68000].icount    = &C68K.ICount;
	cpu[CPU_M68000].cycles    = 0;
	cpu[CPU_M68000].suspended = 0;
	cpu[CPU_M68000].cycles_per_usec = 12000000 / 1000000;

	cpu[CPU_Z80].execute   = z80_execute;
	cpu[CPU_Z80].icount    = &CZ80.ICount;
	cpu[CPU_Z80].cycles    = 0;
	cpu[CPU_Z80].suspended = 0;
	cpu[CPU_Z80].cycles_per_usec = 4000000 / 1000000;
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

void timer_interleave_sound_poll(uint32_t pc, uint16_t status)
{
	if (active_cpu != CPU_M68000 || sound_poll_slice_ended)
		return;

	if (pc != sound_poll_pc || status != sound_poll_status)
	{
		sound_poll_pc = pc;
		sound_poll_status = status;
		sound_poll_reads = 1;
		return;
	}

	if (sound_poll_reads != 0xffu)
		sound_poll_reads++;
	if (sound_poll_reads < 8u || *cpu[CPU_M68000].icount <= 0)
		return;

	/* The 68000 is polling an unchanged sound status at the same PC. End the
	 * current CPU slice as idle time; the scheduler still advances the sound
	 * CPU and all timers across the complete timer_ticks interval. */
	*cpu[CPU_M68000].icount = 0;
	sound_poll_slice_ended = 1;
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
			if (which == YM2610_TIMERA || which == YM2610_TIMERB)
				mvs_me_sound_profile_event(MVS_ME_SOUND_PROFILE_YM_TIMER_PREEMPT);
			timer_ticks -= time_left;
			cpu[active_cpu].cycles -= cycles_left;
			*cpu[active_cpu].icount = 0;

			if (active_cpu == CPU_Z80)
			{
				if (which == YM2610_TIMERA || which == YM2610_TIMERB)
					mvs_me_sound_profile_event(MVS_ME_SOUND_PROFILE_Z80_TIMER_PREEMPT_MAIN);
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

uint64_t timer_get_time_us(void)
{
	return (uint64_t)(uint32_t)global_offset * 1000000ULL +
		(uint64_t)(uint32_t)getabsolutetime();
}


/*------------------------------------------------------
	Get current scanline
------------------------------------------------------*/

int timer_getscanline(void)
{
	if (neogeo_driver_type == RASTER)
		return scanline;
	else
		return 1 + (frame_base >> 6);
}


/*------------------------------------------------------
	Update CPU
------------------------------------------------------*/

static void timer_update_cpu_normal(void)
{
	int i, time;
	uint64_t frame_end_time;
	uint64_t scheduler_start = mvs_me_sound_profile_now_us();

	frame_base = 0;
	timer_left = TICKS_PER_FRAME;

	while (timer_left > 0)
	{
		mvs_me_sound_profile_event(MVS_ME_SOUND_PROFILE_TIMER_SLICE);
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

		if (Loop != LOOP_EXEC)
		{
			mvs_me_sound_profile_add_time(MVS_ME_SOUND_PROFILE_SCHEDULER,
				mvs_me_sound_profile_now_us() - scheduler_start);
			return;
		}

		mvs_me_sound_profile_add_time(MVS_ME_SOUND_PROFILE_SCHEDULER,
			mvs_me_sound_profile_now_us() - scheduler_start);
		cpu_execute(CPU_M68000);
		cpu_execute(CPU_Z80);
		scheduler_start = mvs_me_sound_profile_now_us();

		frame_base += timer_ticks;
		timer_left -= timer_ticks;
	}

	neogeo_vblank_interrupt();
	frame_end_time = timer_get_time_us();

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

	mvs_me_sound_profile_add_time(MVS_ME_SOUND_PROFILE_SCHEDULER,
		mvs_me_sound_profile_now_us() - scheduler_start);
	if (!skip_this_frame()) neogeo_screenrefresh();
	mvs_me_sound_shadow_frame_completed(frame_end_time);
	mvs_me_sound_profile_frame_completed();
}


/*------------------------------------------------------
	Update CPU (for raster driver)
------------------------------------------------------*/

static void timer_update_cpu_raster(void)
{
	int i, time;
	uint64_t frame_end_time;
	uint64_t scheduler_start = mvs_me_sound_profile_now_us();

	frame_base = 0;
	timer_left = 0;

	for (scanline = 1; scanline <= RASTER_LINES; scanline++)
	{
		timer_left += USECS_PER_SCANLINE;

		while (timer_left > 0)
		{
			mvs_me_sound_profile_event(MVS_ME_SOUND_PROFILE_TIMER_SLICE);
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

			if (Loop != LOOP_EXEC)
			{
				mvs_me_sound_profile_add_time(MVS_ME_SOUND_PROFILE_SCHEDULER,
					mvs_me_sound_profile_now_us() - scheduler_start);
				return;
			}

			mvs_me_sound_profile_add_time(MVS_ME_SOUND_PROFILE_SCHEDULER,
				mvs_me_sound_profile_now_us() - scheduler_start);
			cpu_execute(CPU_M68000);
			cpu_execute(CPU_Z80);
			scheduler_start = mvs_me_sound_profile_now_us();

			frame_base += timer_ticks;
			timer_left -= timer_ticks;
		}

		neogeo_raster_interrupt(scanline);
	}
	frame_end_time = timer_get_time_us();

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

	mvs_me_sound_profile_add_time(MVS_ME_SOUND_PROFILE_SCHEDULER,
		mvs_me_sound_profile_now_us() - scheduler_start);
	if (!skip_this_frame()) neogeo_screenrefresh();
	mvs_me_sound_shadow_frame_completed(frame_end_time);
	mvs_me_sound_profile_frame_completed();
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
	timer[WATCHDOG_TIMER].callback   = watchdog_callback;
}

#endif /* SAVE_STATE */
