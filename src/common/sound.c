/******************************************************************************

	sound.c

	Sound Thread

******************************************************************************/

#include "emumain.h"
#include "thread_driver.h"
#include "audio_driver.h"


/******************************************************************************
	Local Variables
******************************************************************************/

static volatile int sound_active;
static void *sound_thread;
static int sound_volume;
static int sound_enable;
static int16_t ALIGN16_DATA sound_buffer[2][SOUND_BUFFER_SIZE];

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

	while (sound_active)
	{
		if (Sleep)
		{
			do
			{
				usleep(5000000);
			} while (Sleep);
		}

		if (sound_enable)
			(*sound->update)(sound_buffer[flip]);
		else
			memset(sound_buffer[flip], 0, SOUND_BUFFER_SIZE * 2);

		audio_driver->srcOutputBlocking(game_audio, sound_volume, sound_buffer[flip], SOUND_BUFFER_SIZE * sizeof(int16_t));
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
	Sound Volume Setting
--------------------------------------------------------*/

void sound_thread_set_volume(void)
{
	sound_volume = audio_driver->volumeMax(game_audio) * (option_sound_volume * 10) / 100;
}


/*--------------------------------------------------------
	Sound Thread Start
--------------------------------------------------------*/

int sound_thread_start(void)
{
	sound_active = 0;
	sound_thread = NULL;
	sound_volume = 0;
	sound_enable = 0;
	game_audio = NULL;

	memset(sound_buffer[0], 0, sizeof(sound_buffer[0]));
	memset(sound_buffer[1], 0, sizeof(sound_buffer[1]));

	game_audio = audio_driver->init();

	if (!audio_driver->chSRCReserve(game_audio, sound->samples, sound->frequency, 2))
	{
		fatalerror(TEXT(COULD_NOT_RESERVE_AUDIO_CHANNEL_FOR_SOUND));
		audio_driver->free(game_audio);
		game_audio = NULL;
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

		audio_driver->release(game_audio);
		audio_driver->free(game_audio);
		game_audio = NULL;
	}
}
