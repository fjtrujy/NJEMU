/*****************************************************************************

	emumain.c

	Emulation Core

******************************************************************************/

#include <unistd.h>
#include <limits.h>
#include "emumain.h"


#define FRAMESKIP_LEVELS	12


/******************************************************************************
	Global Variables
******************************************************************************/

char game_name[16];
char parent_name[16];

char game_dir[PATH_MAX];
#if USE_CACHE
char cache_dir[PATH_MAX];
char cache_parent_name[16];
#endif

int option_showfps;
int option_speedlimit;
int option_autoframeskip;
int option_frameskip;
int option_vsync;
int option_stretch;

int option_sound_enable;
int option_samplerate;
int option_sound_volume;

int machine_driver_type;
int machine_init_type;
int machine_input_type;
int machine_screen_type;
int machine_sound_type;

uint32_t frames_displayed;
int fatal_error;

char launchDir[PATH_MAX] = {0};
char screenshotDir[PATH_MAX] = {0};
bool systembuttons_available;
void *platform_data = NULL;
void *power_data = NULL;

/******************************************************************************
	Local Variables
******************************************************************************/

static int frameskip;
static int frameskipadjust;
static int frameskip_counter;

static uint64_t last_skipcount0_time;
static uint64_t this_frame_base;
static int warming_up;

static int frames_since_last_fps;
static int rendered_frames_since_last_fps;
static float game_speed_percent;
static float frames_per_second;

static int snap_no = -1;

static char fatal_error_message[256];

static const uint8_t skiptable[FRAMESKIP_LEVELS][FRAMESKIP_LEVELS] =
{
	{ 0,0,0,0,0,0,0,0,0,0,0,0 },
	{ 0,0,0,0,0,0,0,0,0,0,0,1 },
	{ 0,0,0,0,0,1,0,0,0,0,0,1 },
	{ 0,0,0,1,0,0,0,1,0,0,0,1 },
	{ 0,0,1,0,0,1,0,0,1,0,0,1 },
	{ 0,1,0,0,1,0,1,0,0,1,0,1 },
	{ 0,1,0,1,0,1,0,1,0,1,0,1 },
	{ 0,1,0,1,1,0,1,0,1,1,0,1 },
	{ 0,1,1,0,1,1,0,1,1,0,1,1 },
	{ 0,1,1,1,0,1,1,1,0,1,1,1 },
	{ 0,1,1,1,1,1,0,1,1,1,1,1 },
	{ 0,1,1,1,1,1,1,1,1,1,1,1 }
};

static int show_frames_each_second = 0;

/******************************************************************************
	Global Variables/Structures
******************************************************************************/

uint8_t ALIGN16_DATA gulist[GULIST_SIZE];
RECT full_rect = { 0, 0, SCR_WIDTH, SCR_HEIGHT };

/******************************************************************************
	Global Variables
******************************************************************************/

volatile int Loop;
volatile int Sleep;

/******************************************************************************
	Local Functions
******************************************************************************/

/*--------------------------------------------------------
	FPS Display
--------------------------------------------------------*/

static void show_fps(void)
{
	size_t sx;
	char buf[32];

	sprintf(buf, "%s%2d %.2f%% %.2ffps",
		option_autoframeskip ? "auto" : "fskp",
		frameskip,
		game_speed_percent,
		frames_per_second);

	sx = SCR_WIDTH - (strlen(buf) << 3);
	printf("%s\n", buf);
	small_font_print((int)sx, 0, buf, 1);
}


/*--------------------------------------------------------
	Battery Low Warning Display
--------------------------------------------------------*/

static void show_battery_warning(void)
{
	if (!power_driver->isBatteryCharging(power_data))
	{
		int bat = power_driver->batteryLifePercent(power_data);

		if (bat < 10)
		{
			static uint32_t counter = 0;

			counter++;
			if ((counter % 120) < 80)
			{
				char warning[128];

				boxfill_alpha(0, 254, SCR_WIDTH-1, SCR_HEIGHT-1, COLOR_BLACK, 12);
				sprintf(warning, TEXT(WARNING_BATTERY_IS_LOW_PLEASE_CHARGE_BATTERY), bat);
				uifont_print_center(256, UI_COLOR(UI_PAL_WARNING), warning);
			}
		}
	}
}


/******************************************************************************
	Global Functions
******************************************************************************/

/*--------------------------------------------------------
	Start Emulation
--------------------------------------------------------*/

void emu_main(void)
{
#if defined(ADHOC) && (EMU_SYSTEM == MVS)
	int save_neogeo_bios = neogeo_bios;
#endif

	snap_no = -1;

	sound_thread_init();
	machine_main();
	sound_thread_exit();

#if defined(ADHOC) && (EMU_SYSTEM == MVS)
	if (adhoc_enable)
		neogeo_bios = save_neogeo_bios;
#endif
}


/*--------------------------------------------------------
	Initialize Frameskip
--------------------------------------------------------*/

void autoframeskip_reset(void)
{
	frameskip = option_autoframeskip ? 0 : option_frameskip;
	frameskipadjust = 0;
	frameskip_counter = 0;

	rendered_frames_since_last_fps = 0;
	frames_since_last_fps = 0;

	game_speed_percent = 100;
	frames_per_second = REFRESH_RATE;
	frames_displayed = 0;

	warming_up = 1;
}


/*--------------------------------------------------------
	Frameskip Table
--------------------------------------------------------*/

uint8_t skip_this_frame(void)
{
	return skiptable[frameskip][frameskip_counter];
}


/*--------------------------------------------------------
	Screen Update
--------------------------------------------------------*/

void update_screen(void)
{
	uint8_t skipped_it = skiptable[frameskip][frameskip_counter];

	if (show_frames_each_second && (frames_displayed % 60) == 0)
	{
		show_fps();
	}

	if (!skipped_it)
	{
		if (option_showfps) show_fps();
		draw_volume_status(1);
		show_battery_warning();
		ui_show_popup(1);
	}
	else
	{
		draw_volume_status(0);
		ui_show_popup(0);
	}

	if (warming_up)
	{
		video_driver->waitVsync(video_data);
		last_skipcount0_time = ticker_driver->currentUs(ticker_data) - (int)((float)FRAMESKIP_LEVELS * TICKS_PER_FRAME);
		warming_up = 0;
	}

	if (frameskip_counter == 0)
		this_frame_base = last_skipcount0_time + (int)((float)FRAMESKIP_LEVELS * TICKS_PER_FRAME);

	frames_displayed++;
	frames_since_last_fps++;

	if (!skipped_it)
	{
		uint64_t curr = ticker_driver->currentUs(ticker_data);
		int flip = 0;

		if (option_speedlimit)
		{
			uint64_t target = this_frame_base + (int)((float)frameskip_counter * TICKS_PER_FRAME);

			if (option_vsync)
			{
				if (curr < target - 100)
				{
					video_driver->flipScreen(video_data, 1);
					flip = 1;
				}
			}

			if (target > curr) usleep(target - curr);
			curr = ticker_driver->currentUs(ticker_data);
		}
		if (!flip) video_driver->flipScreen(video_data, 0);

		rendered_frames_since_last_fps++;

		if (frameskip_counter == 0)
		{
			float seconds_elapsed = (float)(curr - last_skipcount0_time)/ 1000000.0;
			float frames_per_sec = (float)frames_since_last_fps / seconds_elapsed;

			frames_per_second = ((float)rendered_frames_since_last_fps / seconds_elapsed);
			game_speed_percent = (frames_per_second / (float)FPS) * 100;

			last_skipcount0_time = curr;
			frames_since_last_fps = 0;
			rendered_frames_since_last_fps = 0;

			if (option_autoframeskip)
			{
				if (option_speedlimit && frames_displayed > 2 * FRAMESKIP_LEVELS)
				{
					if (game_speed_percent >= 99)
					{
						frameskipadjust++;

						if (frameskipadjust >= 3)
						{
							frameskipadjust = 0;
							if (frameskip > 0) frameskip--;
						}
					}
					else
					{
						if (game_speed_percent < 80)
						{
							frameskipadjust -= (90 - game_speed_percent) / 5;
						}
						else if (frameskip < 8)
						{
							frameskipadjust--;
						}

						while (frameskipadjust <= -2)
						{
							frameskipadjust += 2;
							if (frameskip < FRAMESKIP_LEVELS - 1)
								frameskip++;
						}
					}
				}
			}
		}
	}

	frameskip_counter = (frameskip_counter + 1) % FRAMESKIP_LEVELS;
}


/*--------------------------------------------------------
	Fatal Error Message
--------------------------------------------------------*/

void fatalerror(const char *text, ...)
{
	va_list arg;

	va_start(arg, text);
	vsprintf(fatal_error_message, text, arg);
	va_end(arg);

	fatal_error = 1;
	Loop = LOOP_BROWSER;
}


/*--------------------------------------------------------
	Display Fatal Error Message
--------------------------------------------------------*/

void show_fatal_error(void)
{
	if (fatal_error)
	{
		int sx, sy, ex, ey;
		int width = uifont_get_string_width(fatal_error_message);
		int update = 1;

		sx = (SCR_WIDTH - width) >> 1;
		sy = (SCR_HEIGHT - FONTSIZE) >> 1;
		ex = sx + width;
		ey = sy + (FONTSIZE - 1);

		load_background(WP_LOGO);

		while (Loop != LOOP_EXIT)
		{
			if (update)
			{
				show_background();
				small_icon_shadow(6, 3, UI_COLOR(UI_PAL_TITLE), ICON_SYSTEM);
				uifont_print_shadow(32, 5, UI_COLOR(UI_PAL_TITLE), TEXT(FATAL_ERROR));
				draw_dialog(sx - FONTSIZE/2, sy - FONTSIZE/2, ex + FONTSIZE/2, ey + FONTSIZE/2);
				uifont_print_shadow_center(sy, UI_COLOR(UI_PAL_SELECT), fatal_error_message);

				update = draw_battery_status(1);
				update |= draw_volume_status(1);
				video_driver->flipScreen(video_data, 1);
			}
			else
			{
				update = draw_battery_status(0);
				update |= draw_volume_status(0);
				video_driver->waitVsync(video_data);
			}

			pad_update();

			if (pad_pressed_any())
				break;
		}

		pad_wait_clear();

		fatal_error = 0;
	}
}


/*------------------------------------------------------
	Save Screenshot
------------------------------------------------------*/

void save_snapshot(void)
{
	char path[PATH_MAX];

	sound_mute(1);
#if (EMU_SYSTEM == NCDZ)
	mp3_pause(1);
#endif
#if USE_CACHE
	cache_sleep(1);
#endif

	if (snap_no == -1)
	{
		FILE *fp;

		snap_no = 1;

		while (1)
		{
			sprintf(path, "%s/%s_%02d.png", screenshotDir, game_name, snap_no);
			if ((fp = fopen(path, "rb")) == NULL) break;
			fclose(fp);
			snap_no++;
		}
	}

	sprintf(path, "%s/%s_%02d.png", screenshotDir, game_name, snap_no);
	if (save_png(path))
		ui_popup(TEXT(SNAPSHOT_SAVED_AS_x_PNG), game_name, snap_no++);

#if USE_CACHE
	cache_sleep(0);
#endif
#if (EMU_SYSTEM == NCDZ)
	mp3_pause(0);
#endif
	sound_mute(0);
}


int main(int argc, char *argv[]) {
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
#if !defined(GUI)
	// Some default values
	option_speedlimit = 1;
	option_vsync = 0;
	option_showfps = 0;
	option_sound_enable = 1;
	option_samplerate = 2;
	option_sound_volume = 10;
	option_stretch = 0;
	show_frames_each_second = 0;
#if defined(BUILD_NCDZ)
	option_mp3_enable = 1;
	option_mp3_volume = 10;
#endif

#if defined(BUILD_MVS) || defined(BUILD_NCDZ) || defined(BUILD_CPS1) || defined(BUILD_CPS2)
	input_map[P1_UP] = PLATFORM_PAD_UP;
	input_map[P1_DOWN] = PLATFORM_PAD_DOWN;
	input_map[P1_LEFT] = PLATFORM_PAD_LEFT;
	input_map[P1_RIGHT] = PLATFORM_PAD_RIGHT;
#if defined(BUILD_MVS) || defined(BUILD_NCDZ)
	input_map[P1_BUTTONA] = PLATFORM_PAD_B1;
	input_map[P1_BUTTONB] = PLATFORM_PAD_B2;
	input_map[P1_BUTTONC] = PLATFORM_PAD_B3;
	input_map[P1_BUTTOND] = PLATFORM_PAD_B4;
	input_map[P1_START] = PLATFORM_PAD_START;
#if defined(BUILD_MVS)
	input_map[P1_COIN] = PLATFORM_PAD_SELECT;
#else
	input_map[P1_SELECT] = PLATFORM_PAD_SELECT;
#endif
#endif
#if defined(BUILD_CPS1) || defined(BUILD_CPS2)
	input_map[P1_BUTTON1] = PLATFORM_PAD_B1;
	input_map[P1_BUTTON2] = PLATFORM_PAD_B2;
	input_map[P1_BUTTON3] = PLATFORM_PAD_B3;
	input_map[P1_BUTTON4] = PLATFORM_PAD_B4;
	input_map[P1_DIAL_L] = PLATFORM_PAD_L;
	input_map[P1_DIAL_R] = PLATFORM_PAD_R;
	input_map[P1_START] = PLATFORM_PAD_START;
	input_map[P1_COIN] = PLATFORM_PAD_SELECT;
#endif
#endif
#endif

    // Init process
	platform_data = platform_driver->init();
	ticker_data = ticker_driver->init();
	power_data = power_driver->init();
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);

	getcwd(launchDir, PATH_MAX - 1);
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
	strcat(launchDir, "/");
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);

	memset(screenshotDir, 0x00, sizeof(screenshotDir));

	// Call main platform-specific entry point
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
	platform_driver->main(platform_data, argc, argv);

	mkdir(screenshotDir,0777); // Create screenshot folder

	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
	power_driver->setLowestCpuClock(power_data);
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
	ui_text_data = ui_text_driver->init();
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
	pad_init();
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);

	video_data = video_driver->init(emu_layer_textures, emu_layer_textures_count, &emu_clut_info);

	ui_init();

	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
	// Platform system buttom
	systembuttons_available = platform_driver->startSystemButtons(platform_data);

	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
	file_browser();
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
	video_driver->free(video_data);
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
	ui_text_driver->free(ui_text_data);
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
	pad_exit();
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);

	// Platform exit
	power_driver->free(power_data);
	ticker_driver->free(ticker_data);
	platform_driver->free(platform_data);

	return 0;
}
