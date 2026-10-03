/******************************************************************************

	mvs.c

	MVS Emulation Core

******************************************************************************/

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include "mvs.h"
#ifdef ADHOC
#include "common/adhoc.h"
#include "common/adhoc_transport.h"
#endif
#include "common/cache.h"
#ifdef COMMAND_LIST
#include "common/cmdlist.h"
#endif
#include "common/emulator_options.h"
#include "common/emulator_runtime.h"
#include "common/runtime_paths.h"
#include "common/path_utils.h"
#include "common/ui_defs.h"
#include "common/ui_text_driver.h"
#include "common/video_driver.h"
#include "common/ui.h"
#include "common/filer.h"
#include "common/config.h"
#include "me_sound_shadow.h"

static void byte_swap_pairs_in_place(uint8_t *data, size_t length)
{
	size_t i;
	for (i = 0; i + 1 < length; i += 2)
	{
		uint8_t tmp = data[i];
		data[i] = data[i + 1];
		data[i + 1] = tmp;
	}
}

/******************************************************************************
	Global Variables
******************************************************************************/

int neogeo_bios;
int neogeo_region;
int neogeo_save_sound_flag;

layer_texture_info_t emu_layer_textures[] =
{
	{ 512, 512, 1 }, // TEX_SPR0
	{ 512, 512, 1 }, // TEX_SPR1
	{ 512, 512, 1 }, // TEX_SPR2
	{ 512, 512, 1 }, // TEX_FIX
};
uint8_t emu_layer_textures_count = TEXTURE_LAYER_COUNT;

/* CLUT configuration for MVS/Neo Geo:
 * - 2 palette banks (switched during gameplay)
 * - 4096 colors per bank (256 palettes × 16 colors)
 */
clut_info_t emu_clut_info = {
	.base = (uint16_t *)video_palettebank,
	.entries_per_bank = PALETTE_BANK_SIZE,
	.bank_count = PALETTE_BANKS
};

/******************************************************************************
	Local Variables
******************************************************************************/

#ifdef ADHOC
static const char *bios[] =
{
	"EURO2",
	"EURO1",
	"USA2",
	"USA1",
	"ASIA3N",
	"ASIA3",
	"JPNJ3",
	"JPN3",
	"JPN2",
	"JPN1",
	"NEOGIT",
	"ASIAES",
	"JPNAES"
};
#endif


/******************************************************************************
	Local Functions
******************************************************************************/

/*--------------------------------------------------------
	MVS Emulator Initialization
--------------------------------------------------------*/

static int neogeo_init(void)
{
	int32_t fd;
	char path[PATH_MAX];

#ifdef ADHOC
	if (!adhoc_enable)
#endif
	{
		if (!path_format(path, sizeof(path), "%smemcard/%s.bin", launchDir, game_name)) return 0;
		if ((fd = open(path, O_RDONLY, 0777)) >= 0)
		{
			{ ssize_t io_result = read(fd, neogeo_memcard, 0x800); (void)io_result; }
			close(fd);
		}

		if (!path_format(path, sizeof(path), "%snvram/%s.nv", launchDir, game_name)) return 0;
		if ((fd = open(path, O_RDONLY, 0777)) >= 0)
		{
			{ ssize_t io_result = read(fd, neogeo_sram16, 0x2000); (void)io_result; }
			close(fd);
			byte_swap_pairs_in_place((uint8_t *)neogeo_sram16, 0x2000);
		}
	}

	neogeo_driver_init();
	neogeo_video_init();

	msg_printf(TEXT(DONE2));
	msg_screen_clear();

	video_driver->clearScreen(video_data);

#ifdef ADHOC
	if (adhoc_enable)
	{
		sprintf(adhoc_matching, "%s_%s_%s", TARGET_STR, game_name, bios[neogeo_bios]);

		if (adhocInit(adhoc_matching) == 0)
		{
			if ((adhoc_server = adhocSelect()) >= 0)
			{
				video_driver->clearScreen(video_data);

				if (adhoc_server)
				{
					option_controller = INPUT_PLAYER1;

					return adhoc_send_state(NULL);
				}
				else
				{
					option_controller = INPUT_PLAYER2;

					return adhoc_recv_state(NULL);
				}
			}
		}

		Loop = LOOP_BROWSER;
		return 0;
	}
#endif

	return 1;
}


/*--------------------------------------------------------
	MVS Emulator Reset
--------------------------------------------------------*/

static void neogeo_reset(void)
{
	cz80_state_t z80_state;
	uint32_t z80_banks[4];
	uint8_t sound_code;
	uint8_t pending_command;
	uint8_t result_code;

	video_driver->clearScreen(video_data);

	timer_reset();
	input_reset();

	neogeo_driver_reset();
	neogeo_video_reset();

	sound_reset();
	Cz80_Get_State(&CZ80, &z80_state);
	neogeo_get_z80_shadow_state(z80_banks, &sound_code, &pending_command,
		&result_code);
	(void)mvs_me_sound_shadow_z80_snapshot(&z80_state, memory_region_cpu2,
		memory_region_cpu2, memory_length_cpu2, z80_banks, sound_code,
		pending_command, result_code);
	blit_clear_all_sprite();
	autoframeskip_reset();

	Loop = LOOP_EXEC;
}


/*--------------------------------------------------------
	MVS Emulator Exit
--------------------------------------------------------*/

static void neogeo_exit(void)
{
	int32_t fd;
	char path[PATH_MAX];

	video_driver->clearScreen(video_data);

	ui_popup_reset();

	video_driver->clearScreen(video_data);
	msg_screen_init(WP_LOGO, ICON_SYSTEM, TEXT(EXIT_EMULATION2));

	msg_printf(TEXT(PLEASE_WAIT2));

#ifdef ADHOC
	if (!adhoc_enable)
#endif
	{
		if (!path_format(path, sizeof(path), "%smemcard/%s.bin", launchDir, game_name)) return;
		if ((fd = open(path, O_WRONLY|O_CREAT, 0777)) >= 0)
		{
			{ ssize_t io_result = write(fd, neogeo_memcard, 0x800); (void)io_result; }
			close(fd);
		}

		if (!path_format(path, sizeof(path), "%snvram/%s.nv", launchDir, game_name)) return;
		if ((fd = open(path, O_WRONLY|O_CREAT, 0777)) >= 0)
		{
			byte_swap_pairs_in_place((uint8_t *)neogeo_sram16, 0x2000);
			{ ssize_t io_result = write(fd, neogeo_sram16, 0x2000); (void)io_result; }
			close(fd);
		}


#ifdef COMMAND_LIST
		free_commandlist();
#endif

		if (neogeo_save_sound_flag) option_sound_enable = 1;
		save_gamecfg(game_name);
	}

	msg_printf(TEXT(DONE2));

#ifdef ADHOC
	if (adhoc_enable) adhocTerm();
#endif

	show_exit_screen();
}

/*--------------------------------------------------------
	cheats
--------------------------------------------------------*/

extern int cheat_num;
extern gamecheat_t* gamecheat[];

static void apply_cheat()
{
	gamecheat_t *a_cheat = NULL;
	cheat_option_t *a_cheat_option = NULL;
	cheat_value_t *a_cheat_value = NULL;
	int c,j;

   for( c = 0; c < cheat_num; c++)
   { //arreglo de cheats
	a_cheat = gamecheat[c];
    if( a_cheat == NULL)
		break; //seguro

    if( a_cheat->curr_option == 0)//se asume que el option 0 es el disable
		continue;

    //Se busca cual es el option habilitado
    a_cheat_option = a_cheat->cheat_option[ a_cheat->curr_option];
    if( a_cheat_option == NULL)
		break; //seguro

	//Se ejecutan todos los value del cheat option
	for(  j = 0; j< a_cheat_option->num_cheat_values; j++)
	{
	a_cheat_value = a_cheat_option->cheat_value[j];
		if( a_cheat_value == NULL)
			break;//seguro
		m68000_write_memory_8(a_cheat_value->address,  a_cheat_value->value);

	}
    }
}

/*--------------------------------------------------------
	MVS Emulator Run
--------------------------------------------------------*/

static void neogeo_run(void)
{
	while (Loop >= LOOP_RESET)
	{
		neogeo_reset();

		while (Loop == LOOP_EXEC)
		{
			if (Sleep)
			{
#if USE_CACHE
				cache_sleep(1);
#endif

				do
				{
					usleep(EMULATOR_SLEEP_POLL_US);
				} while (Sleep);

#if USE_CACHE
				cache_sleep(0);
#endif
				autoframeskip_reset();
			}

			apply_cheat();//davex
			
			timer_update_cpu();
			update_screen();
			update_inputport();
		}

		video_driver->clearScreen(video_data);
		sound_mute(1);
	}
}


/******************************************************************************
	Global Functions
******************************************************************************/

/*--------------------------------------------------------
	MVS Emulator Main
--------------------------------------------------------*/

void neogeo_main(void)
{
	Loop = LOOP_RESET;

	while (Loop >= LOOP_RESTART)
	{
		Loop = LOOP_EXEC;

		ui_popup_reset();

		fatal_error = 0;

		video_driver->clearScreen(video_data);

		if (memory_init())
		{
			if (sound_init())
			{
				if (input_init())
				{
					if (neogeo_init())
					{
						if (emu_test_exit_after_init())
							Loop = LOOP_EXIT;
						else
							neogeo_run();
					}
					neogeo_exit();
				}
				input_shutdown();
			}
			sound_exit();
		}
		memory_shutdown();
		show_fatal_error();
	}
}
