/******************************************************************************

	sound.c

	Sound Thread

******************************************************************************/

#include <assert.h>
#include "common/emulator_options.h"
#include "common/emulator_runtime.h"
#include "common/sound.h"
#include "common/ui_text_driver.h"
#include <string.h>
#include <unistd.h>
#include "thread_driver.h"
#include "audio_driver.h"
#include "audio_profile.h"
#include "audio_producer_driver.h"


/******************************************************************************
	Local Variables
******************************************************************************/

static volatile int sound_active;
static void *sound_thread;
static int sound_volume;
static volatile int sound_enable;
static int16_t ALIGN16_DATA sound_buffer[2][SOUND_BUFFER_SIZE];
static volatile uint32_t power_suspend_generation;
static volatile uint32_t power_resume_generation;

static struct sound_t sound_info;
static void *game_audio;


/******************************************************************************
	Global Variables
******************************************************************************/

struct sound_t *sound = &sound_info;


/******************************************************************************
	Local Functions
******************************************************************************/

/*--------------------------------------------------------
	Sound Update Thread
--------------------------------------------------------*/

static int32_t sound_update_thread(uint32_t args, void *argp)
{
	int flip = 0;
	uint64_t last_loop_start = 0;
	uint32_t handled_suspend_generation = 0;
	uint32_t handled_resume_generation = 0;
	(void)args;
	(void)argp;

	while (sound_active)
	{
		uint64_t loop_start = audio_profile_now_us();
		uint64_t start;
		uint32_t suspend_generation = power_suspend_generation;
		uint32_t resume_generation;

		if (last_loop_start != 0)
			audio_profile_add(AUDIO_PROFILE_LOOP_PERIOD, loop_start - last_loop_start);
		last_loop_start = loop_start;

		if (suspend_generation != handled_suspend_generation)
		{
			audio_producer_driver->suspend();
			handled_suspend_generation = suspend_generation;
		}

		if (Sleep)
		{
			do
			{
				usleep(EMULATOR_SLEEP_POLL_US);
			} while (Sleep && sound_active);
		}

		resume_generation = power_resume_generation;
		if (resume_generation != handled_resume_generation)
		{
			audio_producer_driver->resume();
			handled_resume_generation = resume_generation;
		}
		if (!sound_active)
			break;

		if (sound_enable)
		{
			start = audio_profile_now_us();
			audio_producer_driver->render(sound->update, sound_buffer[flip]);
			audio_profile_add(AUDIO_PROFILE_PRODUCER, audio_profile_now_us() - start);
			}
			else
				memset(sound_buffer[flip], 0, sound_output_buffer_bytes(sound));

			start = audio_profile_now_us();
			audio_driver->srcOutputBlocking(game_audio, sound_volume, sound_buffer[flip],
				sound_output_buffer_bytes(sound));
		audio_profile_add(AUDIO_PROFILE_OUTPUT_BLOCK, audio_profile_now_us() - start);
		audio_profile_buffer_completed();
		flip ^= 1;
	}

	thread_driver->exitThread(sound_thread, 0);

	return 0;
}


/******************************************************************************
	Global Functions
******************************************************************************/

/*--------------------------------------------------------
	Sound Initialization
--------------------------------------------------------*/

void sound_thread_init(void)
{
	sound_active = 0;
	sound_thread = NULL;
	sound_volume = 0;
	sound_enable = 0;
	power_suspend_generation = 0;
	power_resume_generation = 0;
}


/*--------------------------------------------------------
	Sound Shutdown
--------------------------------------------------------*/

void sound_thread_exit(void)
{
	sound_thread_stop();
}


/*--------------------------------------------------------
	Sound Enable/Disable Toggle
--------------------------------------------------------*/

void sound_thread_enable(int enable)
{
	if (sound_active)
	{
		sound_enable = enable;

		if (sound_enable)
			sound_thread_set_volume();
		else
			sound_volume = 0;
	}
}


/*--------------------------------------------------------
	Sound Stream Pause/Resume
--------------------------------------------------------*/

void sound_thread_pause(int pause)
{
	if (sound_active && audio_driver->setPaused != NULL)
		audio_driver->setPaused(game_audio, pause != 0);
}


/*--------------------------------------------------------
	Sound Volume Setting
--------------------------------------------------------*/

void sound_thread_set_volume(void)
{
	sound_volume = audio_driver->volumeMax(game_audio) * (option_sound_volume * 10) / 100;
}


/*--------------------------------------------------------
	Synchronize/reset the producer backend
--------------------------------------------------------*/

void sound_thread_reset_producer(void)
{
	if (sound_thread)
		audio_producer_driver->reset();
}


/*--------------------------------------------------------
	Queue a platform power transition for the sound thread
--------------------------------------------------------*/

void sound_thread_notify_power_event(int suspended)
{
	if (suspended)
		power_suspend_generation++;
	else
		power_resume_generation++;
}


/*--------------------------------------------------------
	Sound Thread Start
--------------------------------------------------------*/

int sound_thread_start(void)
{
	/* The synthesis callback may be mono, but every platform receives stereo. */
	assert(sound_output_sample_count(sound) <= SOUND_BUFFER_SIZE);

	sound_active = 0;
	sound_thread = NULL;
	sound_volume = 0;
	sound_enable = 0;
	game_audio = NULL;

	memset(sound_buffer[0], 0, sizeof(sound_buffer[0]));
	memset(sound_buffer[1], 0, sizeof(sound_buffer[1]));
	audio_profile_configure((uint32_t)sound->samples, (uint32_t)sound->frequency,
		SOUND_OUTPUT_CHANNELS);

	if (!audio_producer_driver->init())
		return 0;
	if (Sleep)
		audio_producer_driver->suspend();

	game_audio = audio_driver->init();

	if (!audio_driver->chSRCReserve(game_audio, sound->samples, sound->frequency,
		SOUND_OUTPUT_CHANNELS))
	{
		fatalerror(TEXT(COULD_NOT_RESERVE_AUDIO_CHANNEL_FOR_SOUND));
		audio_driver->free(game_audio);
		game_audio = NULL;
		audio_producer_driver->shutdown();
		return 0;
	}

	sound_thread = thread_driver->init();
	if (!thread_driver->createThread(sound_thread, "Sound thread", sound_update_thread, 0x08, sound->stack))
	{
		fatalerror(TEXT(COULD_NOT_START_SOUND_THREAD));
		audio_driver->release(game_audio);
		audio_driver->free(game_audio);
		thread_driver->free(sound_thread);
		sound_thread = NULL;
		game_audio = NULL;
		audio_producer_driver->shutdown();
		return 0;
	}

	sound_active = 1;
	thread_driver->startThread(sound_thread);

	sound_thread_set_volume();

	return 1;
}


/*--------------------------------------------------------
	Sound Thread Stop
--------------------------------------------------------*/

void sound_thread_stop(void)
{
	if (sound_thread)
	{
		sound_volume = 0;
		sound_enable = 0;

		sound_active = 0;
		thread_driver->waitThreadEnd(sound_thread);
		thread_driver->deleteThread(sound_thread);
		thread_driver->free(sound_thread);
		sound_thread = NULL;

		audio_producer_driver->shutdown();

		audio_driver->release(game_audio);
		audio_driver->free(game_audio);
		game_audio = NULL;
	}
}
