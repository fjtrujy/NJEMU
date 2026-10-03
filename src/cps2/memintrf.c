/******************************************************************************

	memintrf.c

	CPS2 Memory Interface Functions

******************************************************************************/

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cps2.h"
#ifdef ADHOC
#include "common/adhoc.h"
#endif
#include "common/cache.h"
#include "common/cmdlist.h"
#include "common/memory_plan.h"
#include "common/memory_sizes.h"
#include "common/emulator_options.h"
#include "common/emulator_runtime.h"
#include "common/game_database.h"
#include "common/input_driver.h"
#include "common/loadrom.h"
#include "common/power_driver.h"
#include "common/runtime_paths.h"
#include "common/path_utils.h"
#include "common/ui_defs.h"
#include "common/ui_text_driver.h"
#include "common/video_driver.h"
#include "common/ui.h"
#include "common/config.h"


#define M68K_AMASK M68K_ADDR_MASK
#define Z80_AMASK Z80_ADDR_MASK

#define READ_BYTE(mem, offset)			mem[offset ^ 1]
#define READ_WORD(mem, offset)			*(uint16_t *)&mem[offset]
#define WRITE_BYTE(mem, offset, data)	mem[offset ^ 1] = data
#define WRITE_WORD(mem, offset, data)	*(uint16_t *)&mem[offset] = data

#define MAX_CPU1ROM		8
#define MAX_CPU2ROM		8
#define MAX_GFX1ROM		32
#define MAX_SND1ROM		8


/******************************************************************************
	Global Variables
******************************************************************************/

uint8_t *memory_region_cpu1;
uint8_t *memory_region_cpu2;
uint8_t *memory_region_gfx1;
uint8_t *memory_region_sound1;
uint8_t *memory_region_user1;

uint32_t memory_length_cpu1;
uint32_t memory_length_cpu2;
uint32_t memory_length_gfx1;
uint32_t memory_length_sound1;
uint32_t memory_length_user1;

uint32_t gfx_total_elements[3];
uint8_t *gfx_pen_usage[3];

uint8_t  ALIGN16_DATA cps1_ram[CPS1_RAM_SIZE];
uint8_t  ALIGN16_DATA cps2_ram[CPS2_RAM_SIZE + 2];
uint16_t ALIGN16_DATA cps1_gfxram[CPS1_GFXRAM_SIZE >> 1];
uint16_t ALIGN16_DATA cps1_output[CPS1_OUTPUT_SIZE >> 1];

uint16_t ALIGN16_DATA cps2_objram[2][CPS2_OBJRAM_SIZE >> 1];
uint16_t ALIGN16_DATA cps2_output[CPS2_OUTPUT_SIZE >> 1];

uint8_t *qsound_sharedram1;
uint8_t *qsound_sharedram2;

/* cache_parent_name is now declared unconditionally in emumain.c */

static memory_plan_t cps2_memory_plan;
static int cps2_memory_plan_valid = 0;
static memory_allocation_shape_t cps2_memory_shape;


/******************************************************************************
	Local Structures/Variables
******************************************************************************/

static struct rom_t cpu1rom[MAX_CPU1ROM];
static struct rom_t cpu2rom[MAX_CPU2ROM];
static struct rom_t gfx1rom[MAX_GFX1ROM];
static struct rom_t snd1rom[MAX_SND1ROM];

static int num_cpu1rom;
static int num_cpu2rom;
static int num_gfx1rom;
static int num_snd1rom;

static uint8_t *static_ram1;
static uint8_t *static_ram2;
static uint8_t *static_ram3;
static uint8_t *static_ram4;
static uint8_t *static_ram5;
static uint8_t *static_ram6;

#if !RELEASE
static int phoenix_edition;
#endif

static int copy_database_rom(const game_database_rom_t *source, struct rom_t *dest)
{
	if (source == NULL || dest == NULL)
		return 0;
	dest->type = source->type;
	dest->offset = source->offset;
	dest->length = source->length;
	dest->crc = source->crc;
	dest->group = source->group;
	dest->skip = source->skip;
	if (strlen(source->name) >= sizeof(dest->name))
		return 0;
	strcpy(dest->name, source->name);
	return 1;
}

static int configure_database_policy(const game_database_game_t *game)
{
	if (game->core_flags & GAME_DATABASE_CPS2_CACHE_PARENT_OVERRIDE)
	{
		if (game->aux_name[0] == '\0' || strlen(game->aux_name) >= sizeof(cache_parent_name))
			return 0;
		strcpy(cache_parent_name, game->aux_name);
	}
	else if (game->core_flags & GAME_DATABASE_CPS2_CACHE_INDEPENDENT)
	{
		cache_parent_name[0] = '\0';
	}
	else
	{
		strcpy(cache_parent_name, parent_name);
	}

#if !RELEASE
	phoenix_edition = (game->core_flags & GAME_DATABASE_CPS2_PHOENIX) != 0;
#endif
	if (game->core_flags & GAME_DATABASE_CPS2_PHOENIX)
	{
		cps2_clear_decryption_key();
	}
	else
	{
		if (game->data[0] == 0 && game->data[1] == 0)
			return 0;
		cps2_set_decryption_key(game->data[0], game->data[1], game->data[2]);
	}
	return 1;
}


/******************************************************************************
	ROM Loading
******************************************************************************/

/*--------------------------------------------------------
	CPU1 (M68000 program ROM / encrypted)
--------------------------------------------------------*/

static int load_rom_cpu1(void)
{
	int i, res;
	char fname[32], *parent;

	if ((memory_region_cpu1 = malloc(memory_length_cpu1)) == NULL)
	{
		error_memory("REGION_CPU1");
		return 0;
	}
	memset(memory_region_cpu1, 0, memory_length_cpu1);

	parent = strlen(parent_name) ? parent_name : NULL;

	for (i = 0; i < num_cpu1rom; )
	{
		strcpy(fname, cpu1rom[i].name);
		if ((res = file_open(game_name, parent, cpu1rom[i].crc, fname)) < 0)
		{
			if (res == ROM_FILE_OPEN_NOT_FOUND)
				error_file(fname);
			else
				error_crc(fname);
			return 0;
		}

		msg_printf(TEXT(LOADING), fname);

		i = rom_load(cpu1rom, memory_region_cpu1, i, num_cpu1rom);

		file_close();
	}

	return 1;
}


/*--------------------------------------------------------
	CPU2 (Z80 program ROM)
--------------------------------------------------------*/

static int load_rom_cpu2(void)
{
	int i, res;
	char fname[32], *parent;

	if ((memory_region_cpu2 = malloc(memory_length_cpu2)) == NULL)
	{
		error_memory("REGION_CPU2");
		return 0;
	}
	memset(memory_region_cpu2, 0, memory_length_cpu2);

	parent = strlen(parent_name) ? parent_name : NULL;

	for (i = 0; i < num_cpu2rom; )
	{
		strcpy(fname, cpu2rom[i].name);
		if ((res = file_open(game_name, parent, cpu2rom[i].crc, fname)) < 0)
		{
			if (res == ROM_FILE_OPEN_NOT_FOUND)
				error_file(fname);
			else
				error_crc(fname);
			return 0;
		}

		msg_printf(TEXT(LOADING), fname);

		i = rom_load(cpu2rom, memory_region_cpu2, i, num_cpu2rom);

		file_close();
	}

	return 1;
}


/*--------------------------------------------------------
	GFX1 (graphic ROM)
--------------------------------------------------------*/

static int load_rom_gfx1_full_resident(void)
{
	int i, res;
	char fname[32], *parent;

	memset(memory_region_gfx1, 0, memory_length_gfx1);
	memset(gfx_pen_usage[TILE08], 0, gfx_total_elements[TILE08]);
	memset(gfx_pen_usage[TILE16], 0, gfx_total_elements[TILE16]);
	memset(gfx_pen_usage[TILE32], 0, gfx_total_elements[TILE32]);

	parent = strlen(parent_name) ? parent_name : NULL;

	for (i = 0; i < num_gfx1rom; )
	{
		strcpy(fname, gfx1rom[i].name);
		if ((res = file_open(game_name, parent, gfx1rom[i].crc, fname)) < 0)
		{
			if (res == ROM_FILE_OPEN_NOT_FOUND)
				error_file(fname);
			else
				error_crc(fname);
			return 0;
		}

		msg_printf(TEXT(LOADING), fname);
		i = rom_load(gfx1rom, memory_region_gfx1, i, num_gfx1rom);
		file_close();
	}

	msg_printf(TEXT(DECODING_GFX));
	cps2_gfx_decode();
	msg_printf(TEXT(CACHE_USAGE_GFX), memory_length_gfx1 / 1024,
		memory_length_gfx1 / 1024);

	return 1;
}

static int load_rom_gfx1(void)
{
	uint32_t planned_gfx_length = memory_length_gfx1;
#if USE_CACHE
	memory_plan_t streaming_plan;
#endif
	memory_probe_constraints_t constraints;
	game_memory_requirements_t requirements;

	gfx_total_elements[TILE08] = (memory_length_gfx1 - 0x800000) >> 6;
	gfx_total_elements[TILE16] = memory_length_gfx1 >> 7;
	gfx_total_elements[TILE32] = (memory_length_gfx1 - 0x800000) >> 9;

	if (gfx_total_elements[TILE08] > 0x10000) gfx_total_elements[TILE08] = 0x10000;
	if (gfx_total_elements[TILE32] > 0x10000) gfx_total_elements[TILE32] = 0x10000;

	if ((gfx_pen_usage[TILE08] = malloc(gfx_total_elements[TILE08])) == NULL)
	{
		error_memory("GFX_PEN_USAGE (tile8)");
		return 0;
	}
	if ((gfx_pen_usage[TILE16] = malloc(gfx_total_elements[TILE16])) == NULL)
	{
		error_memory("GFX_PEN_USAGE (tile16)");
		return 0;
	}
	if ((gfx_pen_usage[TILE32] = malloc(gfx_total_elements[TILE32])) == NULL)
	{
		error_memory("GFX_PEN_USAGE (tile32)");
		return 0;
	}

	/* All mandatory CPS2 regions are resident at this point. Probe the allocator
	 * itself and retain the successful GFX allocation so planning and ownership
	 * cannot diverge between two malloc calls. */
	memory_probe_constraints_default(&constraints);
	memset(&requirements, 0, sizeof(requirements));
	requirements.core = MEMORY_PLAN_CORE_CPS2;
	requirements.gfx_or_crom_bytes = planned_gfx_length;
	cps2_memory_plan_valid = memory_plan_allocate_shape(&requirements, &constraints,
		&cps2_memory_shape);

#if USE_CACHE
	/* A partial CPS2 allocation backs the compact streaming cache, not the full
	 * decoded GFX image. Re-probe against that real payload ceiling if necessary. */
	if (cps2_memory_plan_valid && !cps2_memory_shape.plan.gfx_fully_resident &&
		cps2_memory_shape.plan.gfx_cache_bytes > driver->cache_size)
	{
		memory_allocation_shape_release(&cps2_memory_shape);
		requirements.gfx_or_crom_bytes = driver->cache_size;
		cps2_memory_plan_valid = memory_plan_allocate_shape(&requirements, &constraints,
			&cps2_memory_shape);
	}
#endif

	if (cps2_memory_plan_valid)
	{
		cps2_memory_plan = cps2_memory_shape.plan;
		memory_plan_log(&cps2_memory_plan);
	}
	else
	{
		printf("[memory_plan] CPS2 empirical probe has no viable cache shape\n");
	}

	if (!cps2_memory_plan_valid)
	{
		msg_printf(TEXT(MEMORY_NOT_ENOUGH));
		Loop = LOOP_BROWSER;
		return 0;
	}

	if (cps2_memory_plan.gfx_fully_resident &&
		cps2_memory_plan.gfx_cache_bytes >= planned_gfx_length)
	{
		memory_allocation_shape_release_reserve(&cps2_memory_shape);
		memory_region_gfx1 = (uint8_t *)cps2_memory_shape.gfx_memory;
		cps2_memory_shape.gfx_memory = NULL;
		if (memory_region_gfx1 != NULL)
		{
			if (!load_rom_gfx1_full_resident())
				return 0;
			return 1;
		}

		msg_printf(TEXT(COULD_NOT_ALLOCATE_MEMORY_FOR_SPRITE_DATA));
		msg_printf(TEXT(TRY_TO_USE_SPRITE_CACHE));
	}

#if USE_CACHE
	/* Streaming fallback. If a full-resident allocation failed, clamp the
	 * request to the compact cache-file payload; cache_start() may still retry
	 * down in 64 KiB blocks if fragmentation prevents that target. */
	streaming_plan = cps2_memory_plan;
	streaming_plan.gfx_fully_resident = false;
	memory_length_gfx1 = driver->cache_size;
	memory_allocation_shape_release_reserve(&cps2_memory_shape);

	{
		void *gfx_memory = cps2_memory_shape.gfx_memory;
		cps2_memory_shape.gfx_memory = NULL;
		if (cache_start(&streaming_plan, gfx_memory, NULL) == 0)
		{
			msg_printf(TEXT(PRESS_ANY_BUTTON2));
			pad_wait_press(PAD_WAIT_INFINITY);
			Loop = LOOP_BROWSER;
			return 0;
		}
	}

	return 1;
#else
	/* A no-cache build has no streaming fallback. The complete decoded GFX
	 * region must fit in the allocation selected above. */
	memory_allocation_shape_release(&cps2_memory_shape);
	msg_printf(TEXT(MEMORY_NOT_ENOUGH));
	Loop = LOOP_BROWSER;
	return 0;
#endif
}


/*--------------------------------------------------------
	SOUND1 (Q-SOUND PCM ROM)
--------------------------------------------------------*/

static int load_rom_sound1(void)
{
	int i, res;
	char fname[32], *parent;

	if ((memory_region_sound1 = malloc(memory_length_sound1)) == NULL)
	{
		error_memory("REGION_SOUND1");
		return 0;
	}
	memset(memory_region_sound1, 0, memory_length_sound1);

	parent = strlen(parent_name) ? parent_name : NULL;

	for (i = 0; i < num_snd1rom; )
	{
		strcpy(fname, snd1rom[i].name);
		if ((res = file_open(game_name, parent, snd1rom[i].crc, fname)) < 0)
		{
			if (res == ROM_FILE_OPEN_NOT_FOUND)
				error_file(fname);
			else
				error_crc(fname);
			return 0;
		}

		msg_printf(TEXT(LOADING), fname);

		i = rom_load(snd1rom, memory_region_sound1, i, num_snd1rom);

		file_close();
	}

	return 1;
}


/*--------------------------------------------------------
	USER1 (MC68000 ROM decrypt region)
--------------------------------------------------------*/

static int load_rom_user1(void)
{
	if (memory_length_user1)
	{
		if ((memory_region_user1 = malloc(memory_length_user1)) == NULL)
		{
			error_memory("REGION_USER1");
			return 0;
		}
		memset(memory_region_user1, 0, memory_length_user1);
	}

	return 1;
}


/*--------------------------------------------------------
	Load selected-game topology and policy from database
--------------------------------------------------------*/

static int load_game_database(const char *game_name)
{
	char path[PATH_MAX];
	game_database_t database = {0};
	game_database_game_t game;
	game_database_error_t error;
	uint32_t i;

	num_cpu1rom = 0;
	num_cpu2rom = 0;
	num_gfx1rom = 0;
	num_snd1rom = 0;
	memory_length_cpu1 = 0;
	memory_length_cpu2 = 0;
	memory_length_gfx1 = 0;
	memory_length_sound1 = 0;
	memory_length_user1 = 0;
	machine_driver_type = 0;
	machine_input_type = 0;
	machine_init_type = 0;
	machine_screen_type = 0;

	if (!path_format(path, sizeof(path), "%s%s", launchDir, game_database_filename()))
		return 3;
	error = game_database_open(&database, path, GAME_DATABASE_CORE_CPS2);
	if (error != GAME_DATABASE_OK)
	{
		printf("game database: cannot open %s: %s\n", path,
			game_database_error_string(error));
		return 3;
	}
	error = game_database_find_game(&database, game_name, &game);
	if (error == GAME_DATABASE_ERROR_NOT_FOUND)
	{
		game_database_close(&database);
		return 2;
	}
	if (error != GAME_DATABASE_OK)
	{
		printf("game database: cannot read CPS2 set %s: %s\n", game_name,
			game_database_error_string(error));
		game_database_close(&database);
		return 3;
	}

	strcpy(parent_name, game.parent_name);
	machine_driver_type = game.machine;
	machine_input_type = game.input;
	machine_init_type = game.init;
	machine_screen_type = game.rotation;

	for (i = 0; i < game.region_count; i++)
	{
		game_database_region_t region;
		uint32_t j;
		struct rom_t *dest = NULL;
		int *count = NULL;
		int capacity = 0;

		error = game_database_get_region(&database, &game, i, &region);
		if (error != GAME_DATABASE_OK)
			goto database_error;
		switch (region.type)
		{
		case GAME_DATABASE_REGION_CPU1:
			memory_length_cpu1 = region.size;
			dest = cpu1rom;
			count = &num_cpu1rom;
			capacity = MAX_CPU1ROM;
			break;
		case GAME_DATABASE_REGION_CPU2:
			memory_length_cpu2 = region.size;
			dest = cpu2rom;
			count = &num_cpu2rom;
			capacity = MAX_CPU2ROM;
			break;
		case GAME_DATABASE_REGION_GFX1:
			memory_length_gfx1 = region.size;
			dest = gfx1rom;
			count = &num_gfx1rom;
			capacity = MAX_GFX1ROM;
			break;
		case GAME_DATABASE_REGION_SOUND1:
			memory_length_sound1 = region.size;
			dest = snd1rom;
			count = &num_snd1rom;
			capacity = MAX_SND1ROM;
			break;
		case GAME_DATABASE_REGION_USER1:
			memory_length_user1 = region.size;
			break;
		default:
			goto database_error;
		}

		if (dest == NULL)
		{
			if (region.rom_count != 0)
				goto database_error;
			continue;
		}
		if (region.rom_count > (uint32_t)capacity)
			goto database_error;
		for (j = 0; j < region.rom_count; j++)
		{
			game_database_rom_t rom;
			error = game_database_get_rom(&database, &region, j, &rom);
			if (error != GAME_DATABASE_OK || !copy_database_rom(&rom, &dest[j]))
				goto database_error;
		}
		*count = region.rom_count;
	}

	if (!configure_database_policy(&game))
		goto database_error;
	game_database_close(&database);
	return 0;

database_error:
	printf("game database: invalid CPS2 record for %s\n", game_name);
	game_database_close(&database);
	return 3;
}


/******************************************************************************
	Memory Interface Functions
******************************************************************************/

/*------------------------------------------------------
	Memory Interface Initialization
-----------------------------------------------------*/

int memory_init(void)
{
	int i, res;

	cps2_clear_decryption_key();
#if !RELEASE
	phoenix_edition = 0;
#endif
	cache_parent_name[0] = '\0';

	memory_region_cpu1   = NULL;
	memory_region_cpu2   = NULL;
	memory_region_gfx1   = NULL;
	memory_region_sound1 = NULL;
	memory_region_user1  = NULL;

	memory_length_cpu1   = 0;
	memory_length_cpu2   = 0;
	memory_length_gfx1   = 0;
	memory_length_sound1 = 0;
	memory_length_user1  = 0;

	gfx_pen_usage[TILE08] = NULL;
	gfx_pen_usage[TILE16] = NULL;
	gfx_pen_usage[TILE32] = NULL;

	cps2_memory_plan_valid = 0;
	memset(&cps2_memory_shape, 0, sizeof(cps2_memory_shape));

#if USE_CACHE
	cache_init();
#endif
	pad_wait_clear();
	video_driver->clearScreen(video_data);
	msg_screen_init(WP_LOGO, ICON_SYSTEM, TEXT(LOAD_ROM));

	msg_printf(TEXT(CHECKING_ROM_INFO));

	if ((res = load_game_database(game_name)) != 0)
	{
		switch (res)
		{
		case 1: msg_printf(TEXT(THIS_GAME_NOT_SUPPORTED)); break;
		case 2: msg_printf(TEXT(ROM_NOT_FOUND)); break;
		case 3: msg_printf(TEXT(ROMINFO_NOT_FOUND_CPS2)); break;
		}
		msg_printf(TEXT(PRESS_ANY_BUTTON2));
		pad_wait_press(PAD_WAIT_INFINITY);
		Loop = LOOP_BROWSER;
		return 0;
	}

	i = 0;
	driver = NULL;
	while (CPS2_driver[i].name)
	{
		if (!strcmp(game_name, CPS2_driver[i].name) || !strcmp(cache_parent_name, CPS2_driver[i].name))
		{
			driver = &CPS2_driver[i];
			break;
		}
		i++;
	}
	if (!driver)
	{
		msg_printf(TEXT(DRIVER_FOR_x_NOT_FOUND), game_name);
		msg_printf(TEXT(PRESS_ANY_BUTTON2));
		pad_wait_press(PAD_WAIT_INFINITY);
		Loop = LOOP_BROWSER;
		return 0;
	}

	if (parent_name[0])
		msg_printf(TEXT(ROMSET_x_PARENT_x), game_name, parent_name);
	else
		msg_printf(TEXT(ROMSET_x), game_name);

	load_gamecfg(game_name);
#ifdef ADHOC
	if (adhoc_enable)
	{
		/* Use fixed settings for some options during AdHoc communication */
#if ENABLE_RASTER_OPTION
		cps_raster_enable    = 1;
#endif
		platform_performance_level    = power_get_highest_performance_level();
		option_vsync         = 0;
		option_autoframeskip = 0;
		option_frameskip     = 0;
		option_showfps       = 0;
		option_speedlimit    = 1;
		option_sound_enable  = 1;
	}
	else
#endif
	{
#ifdef COMMAND_LIST
		if (cache_parent_name[0])
			load_commandlist(game_name, cache_parent_name);
		else
			load_commandlist(game_name, NULL);
#endif
	}

	power_set_performance_level(platform_performance_level);

	if (load_rom_cpu1() == 0) return 0;
	if (load_rom_user1() == 0) return 0;
	if (load_rom_cpu2() == 0) return 0;
	if (option_sound_enable)
	{
		if (load_rom_sound1() == 0) return 0;
	}
	if (load_rom_gfx1() == 0) return 0;

	static_ram1 = (uint8_t *)cps1_ram;
	static_ram2 = (uint8_t *)cps1_gfxram;
	static_ram3 = (uint8_t *)cps2_ram;
	static_ram4 = (uint8_t *)cps2_output;
	static_ram5 = (uint8_t *)cps2_objram[0];
	static_ram6 = (uint8_t *)cps2_objram[1];

	qsound_sharedram1 = &memory_region_cpu2[0xc000];
	qsound_sharedram2 = &memory_region_cpu2[0xf000];

	memory_region_cpu2[0xd007] = 0x80;

	return 1;
}


/*------------------------------------------------------
	Memory Interface Termination
------------------------------------------------------*/

void memory_shutdown(void)
{
#if USE_CACHE
	cache_shutdown();
#endif
	memory_allocation_shape_release(&cps2_memory_shape);

	if (gfx_pen_usage[TILE08]) free(gfx_pen_usage[TILE08]);
	if (gfx_pen_usage[TILE16]) free(gfx_pen_usage[TILE16]);
	if (gfx_pen_usage[TILE32]) free(gfx_pen_usage[TILE32]);

	if (memory_region_cpu1)   free(memory_region_cpu1);
	if (memory_region_cpu2)   free(memory_region_cpu2);
	if (memory_region_gfx1)   free(memory_region_gfx1);
	if (memory_region_sound1) free(memory_region_sound1);
	if (memory_region_user1)  free(memory_region_user1);
}


/******************************************************************************
	M68000 Memory Read/Write Functions
******************************************************************************/

/*------------------------------------------------------
	M68000 Memory Read (byte)
------------------------------------------------------*/

uint8_t m68000_read_memory_8(uint32_t offset)
{
	int shift;
	uint16_t mem_mask;

	offset &= M68K_AMASK;

	if (offset < memory_length_cpu1)
	{
		return READ_BYTE(memory_region_cpu1, offset);
	}

	shift = (~offset & 1) << 3;
	mem_mask = ~(0xff << shift);

	switch (offset >> 16)
	{
	case 0x40:
		return READ_BYTE(static_ram4, (offset - 0x400000));

	case 0x61:
		return qsound_sharedram1_r(offset >> 1, mem_mask) >> shift;

	case 0x66:
		return READ_BYTE(static_ram3, (offset - 0x660000));

	case 0x70:
		if (offset & 0x8000)
			return READ_BYTE(static_ram6, (offset & 0x1fff));
		else
			return READ_BYTE(static_ram5, (offset & 0x1fff));
		break;

	case 0x80:
		switch (offset & 0xff00)
		{
		case 0x0100:
		case 0x4100:
			return cps1_output_r(offset >> 1, mem_mask) >> shift;

		case 0x4000:
			switch (offset & 0xfe)
			{
			case 0x00: return cps2_inputport0_r(offset >> 1, mem_mask) >> shift;
			case 0x10: return cps2_inputport1_r(offset >> 1, mem_mask) >> shift;
			case 0x20: return cps2_eeprom_port_r(offset >> 1, mem_mask) >> shift;
			case 0x30: return cps2_qsound_volume_r(offset >> 1, mem_mask) >> shift;
			}
			break;
		}
		break;

	case 0x90:
	case 0x91:
	case 0x92:
		return READ_BYTE(static_ram2, (offset - 0x900000));

	case 0xff:
		return READ_BYTE(static_ram1, (offset - 0xff0000));
	}

	return 0xff;
}


/*------------------------------------------------------
	M68000 Memory Read (word)
------------------------------------------------------*/

uint16_t m68000_read_memory_16(uint32_t offset)
{
	offset &= M68K_AMASK;

	if (offset < memory_length_cpu1)
	{
		return READ_WORD(memory_region_cpu1, offset);
	}

	switch (offset >> 16)
	{
	case 0x40:
		return READ_WORD(static_ram4, (offset - 0x400000));

	case 0x61:
		return qsound_sharedram1_r(offset >> 1, 0);

	case 0x66:
		return READ_WORD(static_ram3, (offset - 0x660000));

	case 0x70:
		if (offset & 0x8000)
			return READ_WORD(static_ram6, (offset & 0x1fff));
		else
			return READ_WORD(static_ram5, (offset & 0x1fff));
		break;

	case 0x80:
		switch (offset & 0xff00)
		{
		case 0x0100:
		case 0x4100:
			return cps1_output_r(offset >> 1, 0);

		case 0x4000:
			switch (offset & 0xfe)
			{
			case 0x00: return cps2_inputport0_r(offset >> 1, 0);
			case 0x10: return cps2_inputport1_r(offset >> 1, 0);
			case 0x20: return cps2_eeprom_port_r(offset >> 1, 0);
			case 0x30: return cps2_qsound_volume_r(offset >> 1, 0);
			}
			break;
		}
		break;

	case 0x90:
	case 0x91:
	case 0x92:
		return READ_WORD(static_ram2, (offset - 0x900000));

	case 0xff:
		return READ_WORD(static_ram1, (offset - 0xff0000));
	}

	return 0xffff;
}


/*------------------------------------------------------
	M68000 Memory Write (byte)
------------------------------------------------------*/

void m68000_write_memory_8(uint32_t offset, uint8_t data)
{
	int shift = (~offset & 1) << 3;
	uint16_t mem_mask = ~(0xff << shift);

	offset &= M68K_AMASK;

	switch (offset >> 16)
	{
	case 0x40:
#if !RELEASE
		if (!phoenix_edition)
#endif
			WRITE_BYTE(static_ram4, (offset - 0x400000), data);
		return;

	case 0x61:
		qsound_sharedram1_w(offset >> 1, data << shift, mem_mask);
		return;

	case 0x66:
		WRITE_BYTE(static_ram3, (offset - 0x660000), data);
		return;

	case 0x70:
		if (offset & 0x8000)
			WRITE_BYTE(static_ram6, (offset & 0x1fff), data);
		else
			WRITE_BYTE(static_ram5, (offset & 0x1fff), data);
		return;

	case 0x80:
		switch (offset & 0xff00)
		{
		case 0x0100:
		case 0x4100:
			cps1_output_w(offset >> 1, data << shift, mem_mask);
			return;

		case 0x4000:
			switch (offset & 0xfe)
			{
			case 0x40:
				cps2_eeprom_port_w(offset >> 1, data << shift, mem_mask);
				return;

			case 0xe0:
				if (offset & 1)
				{
					cps2_objram_bank = data & 1;
					static_ram6 = (uint8_t *)cps2_objram[cps2_objram_bank ^ 1];
				}
				return;
			}
			break;
		}
		break;

	case 0x90:
	case 0x91:
	case 0x92:
		WRITE_BYTE(static_ram2, (offset - 0x900000), data);
		return;

	case 0xff:
#if !RELEASE
		if (phoenix_edition)
		{
			if (offset >= 0xfffff0)
			{
				offset -= 0xbffff0;
				WRITE_BYTE(static_ram4, (offset - 0x400000), data);
				return;
			}
		}
#endif
		WRITE_BYTE(static_ram1, (offset - 0xff0000), data);
		return;
	}
}


/*------------------------------------------------------
	M68000 Memory Write (word)
------------------------------------------------------*/

void m68000_write_memory_16(uint32_t offset, uint16_t data)
{
	offset &= M68K_AMASK;

	switch (offset >> 16)
	{
	case 0x40:
#if !RELEASE
		if (!phoenix_edition)
#endif
			WRITE_WORD(static_ram4, (offset - 0x400000), data);
		return;

	case 0x61:
		qsound_sharedram1_w(offset >> 1, data, 0);
		return;

	case 0x66:
		WRITE_WORD(static_ram3, (offset - 0x660000), data);
		return;

	case 0x70:
		if (offset & 0x8000)
			WRITE_WORD(static_ram6, (offset & 0x1fff), data);
		else
			WRITE_WORD(static_ram5, (offset & 0x1fff), data);
		break;

	case 0x80:
		switch (offset & 0xff00)
		{
		case 0x0100:
		case 0x4100:
			cps1_output_w(offset >> 1, data, 0);
			return;

		case 0x4000:
			switch (offset & 0xfe)
			{
			case 0x40:
				cps2_eeprom_port_w(offset >> 1, data, 0);
				return;

			case 0xe0:
				cps2_objram_bank = data & 1;
				static_ram6 = (uint8_t *)cps2_objram[cps2_objram_bank ^ 1];
				return;
			}
			break;
		}
		break;

	case 0x90:
	case 0x91:
	case 0x92:
		WRITE_WORD(static_ram2, (offset - 0x900000), data);
		return;

	case 0xff:
#if !RELEASE
		if (phoenix_edition)
		{
			if (offset >= 0xfffff0)
			{
				offset -= 0xbffff0;
				WRITE_WORD(static_ram4, (offset - 0x400000), data);
				return;
			}
		}
#endif
		WRITE_WORD(static_ram1, (offset - 0xff0000), data);
		return;
	}
}


/******************************************************************************
	Z80 Memory Read/Write Functions
******************************************************************************/

/*------------------------------------------------------
	Z80 Memory Read (byte)
------------------------------------------------------*/

uint8_t z80_read_memory_8(uint32_t offset)
{
	return memory_region_cpu2[offset & Z80_AMASK];
}


/*------------------------------------------------------
	Z80 Memory Write (byte)
------------------------------------------------------*/

void z80_write_memory_8(uint32_t offset, uint8_t data)
{
	offset &= Z80_AMASK;

	switch (offset & 0xf000)
	{
	case 0xc000: case 0xf000:
		// c000-cfff: QSOUND shared RAM
		// f000-ffff: RAM
		memory_region_cpu2[offset] = data;
		break;

	case 0xd000:
		switch (offset)
		{
		case 0xd000: qsound_data_h_w(0, data); break;
		case 0xd001: qsound_data_l_w(0, data); break;
		case 0xd002: qsound_cmd_w(0, data); break;
		case 0xd003: qsound_banksw_w(0, data); break;
		}
		break;
	}
}


/******************************************************************************
	Save/Load State
******************************************************************************/

#ifdef SAVE_STATE

STATE_SAVE( memory )
{
	state_save_byte(cps1_ram, 0x10000);
	state_save_byte(cps1_gfxram, 0x30000);
	state_save_byte(cps1_output, 0x100);
	state_save_byte(cps2_ram, 0x4002);
	state_save_byte(cps2_objram[0], 0x2000);
	state_save_byte(cps2_objram[1], 0x2000);
	state_save_byte(cps2_output, 0x10);
	state_save_byte(qsound_sharedram1, 0x1000);
	state_save_byte(qsound_sharedram2, 0x1000);
}

STATE_LOAD( memory )
{
	state_load_byte(cps1_ram, 0x10000);
	state_load_byte(cps1_gfxram, 0x30000);
	state_load_byte(cps1_output, 0x100);
	state_load_byte(cps2_ram, 0x4002);
	state_load_byte(cps2_objram[0], 0x2000);
	state_load_byte(cps2_objram[1], 0x2000);
	state_load_byte(cps2_output, 0x10);
	state_load_byte(qsound_sharedram1, 0x1000);
	state_load_byte(qsound_sharedram2, 0x1000);
}

#endif /* SAVE_STATE */
