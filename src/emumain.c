/*****************************************************************************

	emumain.c

	Emulation Core

******************************************************************************/

#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "emucfg.h"
#include "common/ui_draw.h"
#include "common/ui_layout.h"
#include "common/ui.h"
#include "common/png_io.h"
#include "common/emulator_options.h"
#include "common/emulator_runtime.h"
#include "common/emulator_video.h"
#include "common/filer.h"
#include "common/frame_pacing.h"
#include "common/input_driver.h"
#include "common/platform_driver.h"
#include "common/platform_memory_info.h"
#include "common/power_driver.h"
#include "common/runtime_paths.h"
#include "common/sound.h"
#include "common/thread_driver.h"
#include "common/ticker_driver.h"
#include "common/ui_defs.h"
#include "common/ui_text_driver.h"
#include "common/video_driver.h"
#include "common/video_geometry.h"
#ifdef ADHOC
#include "common/adhoc.h"
#endif
#if USE_CACHE
#include "common/cache.h"
#endif
#if (EMU_SYSTEM == NCDZ)
#include "common/mp3.h"
#endif

#if (EMU_SYSTEM == CPS1)
#include "cps1/cps1.h"
#elif (EMU_SYSTEM == CPS2)
#include "cps2/cps2.h"
#elif (EMU_SYSTEM == MVS)
#include "mvs/mvs.h"
#elif (EMU_SYSTEM == NCDZ)
#include "ncdz/ncdz.h"
#endif


#define FRAMESKIP_LEVELS	12


/******************************************************************************
	Global Variables
******************************************************************************/

char game_name[16];
char parent_name[16];

char game_dir[PATH_MAX];
/* Phase 2b.5-prep: always declared so CPS2 doesn't need a duplicate
 * declaration in cps2/memintrf.c. Unused on CPS1/NCDZ where USE_CACHE=0
 * (small bytes-of-bss cost). */
char cache_dir[PATH_MAX];
char cache_parent_name[16];

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
void *platform_data = NULL;

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

#ifdef PSP
uint8_t ALIGN16_DATA gulist[GULIST_SIZE];
#endif
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

static void show_fps(bool draw)
{
	size_t sx;
	char buf[32];

	sprintf(buf, "%s%2d %.2f%% %.2ffps",
		option_autoframeskip ? "auto" : "fskp",
		frameskip,
		game_speed_percent,
		frames_per_second);

#if !defined(GUI)
	printf("%s\n", buf);
#endif
	if (!draw)
		return;

	sx = (size_t)ui_layout_get()->logical_width - (strlen(buf) << 3);
	small_font_print((int)sx, 0, buf, 1);
}


/*--------------------------------------------------------
	Battery Low Warning Display
--------------------------------------------------------*/

static void show_battery_warning(void)
{
	power_battery_status_t battery;

	if (power_query_battery_status(&battery) && !battery.charging)
	{
		int bat = battery.percent;

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

bool emu_test_exit_after_init(void)
{
#if defined(DESKTOP)
	const char *value = getenv("NJEMU_TEARDOWN_TEST");
	return value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
#else
	return false;
#endif
}

static uint32_t emu_test_frame_limit(void)
{
#if defined(DESKTOP)
	static bool initialized;
	static uint32_t frame_limit;

	if (!initialized)
	{
		const char *value = getenv("NJEMU_TEST_FRAME_LIMIT");
		char *end = NULL;
		unsigned long parsed = 0;

		if (value != NULL && value[0] != '\0')
		{
			parsed = strtoul(value, &end, 10);
			if (end == value || *end != '\0' || parsed > UINT32_MAX)
				parsed = 0;
		}

		frame_limit = (uint32_t)parsed;
		initialized = true;
	}

	return frame_limit;
#else
	return 0;
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

	if (show_frames_each_second && !option_showfps &&
		(frames_displayed % 60) == 0)
	{
		show_fps(false);
	}

	if (!skipped_it)
	{
		/* Target rendering has already completed its frame at this point. UI
		 * primitives (notably PSP GU draws) require their own valid backend
		 * frame, so submit the optional FPS HUD as a small overlay pass. */
		if (option_showfps)
		{
			video_driver->beginFrame(video_data);
			show_fps(true);
			video_driver->endFrame(video_data);
		}
		show_battery_warning();
		ui_show_popup(1);
	}
	else
	{
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
	{
		uint32_t test_frame_limit = emu_test_frame_limit();
		if (test_frame_limit != 0 && frames_displayed >= test_frame_limit)
			Loop = LOOP_EXIT;
	}

	if (!skipped_it)
	{
		uint64_t curr = ticker_driver->currentUs(ticker_data);
		uint64_t target = this_frame_base +
			(int)((float)frameskip_counter * TICKS_PER_FRAME);
		bool sync_flip = frame_pacing_should_sync_flip(
			option_speedlimit != 0, option_vsync != 0, curr, target);
		bool scheduler_blocked = sync_flip;

		/* With software pacing but no useful VBlank wait, reach the emulation
		 * deadline before presenting. If VSync is useful, present first: waiting
		 * for VBlank may consume most/all of the remaining budget. */
		if (option_speedlimit && !sync_flip)
		{
			uint64_t delay = frame_pacing_sleep_us(true, curr, target);
			if (delay != 0)
			{
				usleep(delay);
				scheduler_blocked = true;
			}
		}

		video_driver->flipScreen(video_data, sync_flip);
		curr = ticker_driver->currentUs(ticker_data);

		/* A synchronous flip blocks until VBlank. Re-sample the clock before
		 * applying the software limit so that VSync time is never counted twice. */
		if (option_speedlimit && sync_flip)
		{
			uint64_t delay = frame_pacing_sleep_us(true, curr, target);
			if (delay != 0)
			{
				usleep(delay);
				scheduler_blocked = true;
				curr = ticker_driver->currentUs(ticker_data);
			}
		}

		/* Falling behind the frame deadline can remove every natural blocking
		 * point even with the limiter enabled. Yield explicitly in that case. */
		if (!scheduler_blocked)
			thread_driver->yieldThread();

		rendered_frames_since_last_fps++;

		if (frameskip_counter == 0)
		{
			float seconds_elapsed = (float)(curr - last_skipcount0_time)/ 1000000.0;

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
				video_driver->flipScreen(video_data, 1);
			}
			else
			{
				update = draw_battery_status(0);
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
		snap_no = 1;

		while (1)
		{
			int fd;
			sprintf(path, "%s/%s_%02d.png", screenshotDir, game_name, snap_no);
			fd = open(path, O_RDONLY);
			if (fd < 0) break;
			close(fd);
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
		if (platform_data == NULL) {
			printf("Failed to initialize platform driver\n");
			return 1;
		}
		if (platform_driver->queryMemoryInfo != NULL) {
			platform_memory_info_t memory_info;
			if (platform_driver->queryMemoryInfo(platform_data, &memory_info)) {
				platform_memory_info_apply_env_overrides(&memory_info);
				platform_memory_info_log(&memory_info);
			}
		}
		ticker_data = ticker_driver->init();
		if (ticker_data == NULL) {
			printf("Failed to initialize ticker driver\n");
			goto cleanup_platform;
		}
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);

		if (getcwd(launchDir, sizeof(launchDir)) == NULL) {
			printf("Failed to determine launch directory\n");
			goto cleanup_ticker;
		}
		printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
		{
			size_t launch_len = strlen(launchDir);
			if (launch_len == 0 || launchDir[launch_len - 1] != '/') {
				if (launch_len + 1 >= sizeof(launchDir)) {
					printf("Launch directory path is too long\n");
					goto cleanup_ticker;
				}
				launchDir[launch_len++] = '/';
				launchDir[launch_len] = '\0';
			}
		}
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);

	memset(screenshotDir, 0x00, sizeof(screenshotDir));

	// Call main platform-specific entry point
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
	platform_driver->main(platform_data, argc, argv);

	mkdir(screenshotDir,0777); // Create screenshot folder

	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
		power_set_lowest_performance_level();
		printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
		ui_text_data = ui_text_driver->init();
		if (ui_text_data == NULL) {
			printf("Failed to initialize UI text driver\n");
			goto cleanup_ticker;
		}
		printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
		if (!pad_init()) {
			printf("Failed to initialize input driver\n");
			goto cleanup_ui_text;
		}
		printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
	
		video_data = video_driver->init(emu_layer_textures, emu_layer_textures_count, &emu_clut_info);
		if (video_data == NULL) {
			printf("Failed to initialize video driver\n");
			goto cleanup_input;
		}

// #if defined(GUI) && defined(PS2)
// 	while(1) {
// 		printf("==> emumain: before beginFrame\n");
// 		video_driver->beginFrame(video_data);
// 		video_driver->fillFrame(video_data, COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER, 0x00FF0000);
// 		printf("==> emumain: before endFrame\n");
// 		video_driver->endFrame(video_data);
// 		printf("==> emumain: before flipScreen\n");
// 		video_driver->flipScreen(video_data, 1);
// 		printf("==> emumain: after flipScreen, sleeping\n");
// 	}
// #endif

		if (!ui_init()) {
			printf("Failed to initialize UI draw driver\n");
			goto cleanup_video;
		}

	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
		printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
		file_browser();
		ui_exit();
		printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
		video_driver->free(video_data);
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
	ui_text_driver->free(ui_text_data);
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);
	pad_exit();
	printf("===> %s, %s:%i\n", __FUNCTION__, __FILE__, __LINE__);

	// Platform exit
	ticker_driver->free(ticker_data);
		platform_driver->free(platform_data);
	
		return 0;

cleanup_video:
		video_driver->free(video_data);
cleanup_input:
		pad_exit();
cleanup_ui_text:
		ui_text_driver->free(ui_text_data);
cleanup_ticker:
		ticker_driver->free(ticker_data);
cleanup_platform:
		platform_driver->free(platform_data);
		return 1;
}
