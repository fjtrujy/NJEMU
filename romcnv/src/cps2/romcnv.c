/*****************************************************************************

	romcnv.c

	NJEMU CPS2 ROM converter

******************************************************************************/

#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/game_database.h"
#include "cache_layouts.h"
#include "romcnv.h"
#include "translation.h"
#include "zip_writer.h"

#define SPRITE_BLANK		0x00
#define SPRITE_TRANSPARENT	0x02
#define SPRITE_OPAQUE		0x01

#define MAX_GFX1ROM			32

enum
{
	REGION_GFX1 = 0,
	REGION_SKIP
};

enum
{
	TILE08 = 0,
	TILE16,
	TILE32,
	TILE_TYPE_MAX
};


/******************************************************************************
	Local Variables
******************************************************************************/

static uint8_t  *memory_region_gfx1;
static uint32_t memory_length_gfx1;

static uint32_t gfx_total_elements[TILE_TYPE_MAX];
static uint8_t  *gfx_pen_usage[TILE_TYPE_MAX];

static struct rom_t gfx1rom[MAX_GFX1ROM];
static int num_gfx1rom;

static uint8_t block_empty[0x200];
static game_database_t cps2_game_database;

static void change_directory(const char *path)
{
	if (chdir(path) != 0)
		perror(path);
}

static int set_cache_parent_policy(const game_database_game_t *game)
{
	if (game->core_flags & GAME_DATABASE_CPS2_CACHE_PARENT_OVERRIDE)
	{
		if (game->aux_name[0] == '\0' || strlen(game->aux_name) >= sizeof(cache_name))
			return 0;
		strcpy(cache_name, game->aux_name);
	}
	else if (game->core_flags & GAME_DATABASE_CPS2_CACHE_INDEPENDENT)
	{
		cache_name[0] = '\0';
	}
	else
	{
		strcpy(cache_name, parent_name);
	}
	return 1;
}

static uint8_t null_tile[128] =
{
	0x67,0x66,0x66,0x66,0x66,0x66,0x66,0x56,
	0x56,0x55,0x55,0x55,0x55,0x55,0x55,0x45,
	0x56,0x51,0x15,0x51,0x11,0x15,0x51,0x45,
	0x56,0x11,0x15,0x51,0x11,0x15,0x51,0x45,
	0x56,0x11,0x11,0x51,0x11,0x15,0x51,0x45,
	0x56,0x11,0x15,0x51,0x11,0x15,0x51,0x45,
	0x56,0x11,0x55,0x51,0x11,0x11,0x51,0x45,
	0x56,0x55,0x55,0x55,0x55,0x55,0x55,0x45,
	0x56,0x11,0x55,0x55,0x11,0x55,0x55,0x45,
	0x56,0x11,0x55,0x55,0x11,0x55,0x55,0x45,
	0x56,0x11,0x55,0x55,0x11,0x55,0x55,0x45,
	0x56,0x11,0x55,0x55,0x11,0x55,0x55,0x45,
	0x56,0x11,0x11,0x51,0x11,0x11,0x51,0x45,
	0x56,0x55,0x55,0x55,0x55,0x55,0x55,0x45,
	0x56,0x55,0x55,0x55,0x55,0x55,0x55,0x45,
	0x45,0x44,0x44,0x44,0x44,0x44,0x44,0x34
};

static uint8_t blank_tile[128] =
{
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0x11,0x11,0x11,0x11,0x11,0x11,0xff,
	0x1f,0xff,0xff,0xff,0xff,0xff,0xff,0xf1,
	0x1f,0xff,0xff,0x1f,0xff,0xff,0xf1,0xf1,
	0x1f,0xff,0xff,0xff,0xf1,0x1f,0x1f,0xf1,
	0x1f,0xff,0xff,0xff,0xff,0xff,0xf1,0xf1,
	0x1f,0xff,0xff,0x1f,0xff,0xff,0xff,0xf1,
	0x1f,0xff,0xff,0x1f,0xff,0xff,0xff,0xf1,
	0x1f,0xff,0xf1,0xff,0xf1,0x1f,0xff,0xf1,
	0x1f,0x1f,0xff,0xff,0xf1,0xff,0xf1,0xf1,
	0x1f,0x1f,0xff,0xff,0x1f,0xff,0xf1,0xf1,
	0x1f,0x1f,0x1f,0xff,0x1f,0xff,0xf1,0xf1,
	0x1f,0xff,0xff,0x11,0xf1,0xff,0xff,0xf1,
	0x1f,0xff,0xff,0xff,0xff,0xff,0xff,0xf1,
	0xff,0x11,0x11,0x11,0x11,0x11,0x11,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
};


static const cps2_cache_layout_t *cache_layout;

/******************************************************************************
	CPS2 Functions
******************************************************************************/

static void unshuffle(uint64_t *buf, int len)
{
	int i;
	uint64_t t;

	if (len == 2) return;

	len /= 2;

	unshuffle(buf, len);
	unshuffle(buf + len, len);

	for (i = 0; i < len / 2; i++)
	{
		t = buf[len / 2 + i];
		buf[len / 2 + i] = buf[len + i];
		buf[len + i] = t;
	}
}


static void cps2_gfx_decode(void)
{
	int i, j;
	uint8_t *gfx = memory_region_gfx1;

	for (i = 0; i < memory_length_gfx1; i += 0x200000)
		unshuffle((uint64_t *)&memory_region_gfx1[i], 0x200000 / 8);

	for (i = 0; i < memory_length_gfx1 / 4; i++)
	{
		uint32_t src = gfx[4 * i] + (gfx[4 * i + 1] << 8) + (gfx[4 * i + 2] << 16) + (gfx[4 * i + 3] << 24);
		uint32_t dw = 0, data;

		for (j = 0; j < 8; j++)
		{
			int n = 0;
			uint32_t mask = (0x80808080 >> j) & src;

			if (mask & 0x000000ff) n |= 1;
			if (mask & 0x0000ff00) n |= 2;
			if (mask & 0x00ff0000) n |= 4;
			if (mask & 0xff000000) n |= 8;

			dw |= n << (j * 4);
		}

		data = ((dw & 0x0000000f) >>  0) | ((dw & 0x000000f0) <<  4)
			 | ((dw & 0x00000f00) <<  8) | ((dw & 0x0000f000) << 12)
			 | ((dw & 0x000f0000) >> 12) | ((dw & 0x00f00000) >>  8)
			 | ((dw & 0x0f000000) >>  4) | ((dw & 0xf0000000) >>  0);

		gfx[4 * i + 0] = data >>  0;
		gfx[4 * i + 1] = data >>  8;
		gfx[4 * i + 2] = data >> 16;
		gfx[4 * i + 3] = data >> 24;
	}
}


static void clear_empty_blocks(void)
{
	int i, j, size;
	uint8_t temp[512];
	int blocks_available = 0;

	memset(block_empty, 0, 0x200);

	for (i = 0; i < memory_length_gfx1; i += 128)
	{
		if (memcmp(&memory_region_gfx1[i], null_tile, 128) == 0
		||	memcmp(&memory_region_gfx1[i], blank_tile, 128) == 0)
			memset(&memory_region_gfx1[i], 0xff, 128);
	}

	if (!strcmp(cache_layout->name, "avsp"))
	{
		for (i = 0xb0; i <= 0xff; i++)
			memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
	}
	else if (!strcmp(cache_layout->name, "ddtod"))
	{
		memcpy(temp, &memory_region_gfx1[0x5be800], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}
		memcpy(temp, &memory_region_gfx1[0x657a00], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}
		memcpy(temp, &memory_region_gfx1[0x707800], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}
		memcpy(temp, &memory_region_gfx1[0x710b80], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}
		memcpy(temp, &memory_region_gfx1[0x77d080], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}
		memcpy(temp, &memory_region_gfx1[0x780000], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}
		memcpy(temp, &memory_region_gfx1[0x7b5580], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}
		memcpy(temp, &memory_region_gfx1[0x7d7800], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}
		memcpy(temp, &memory_region_gfx1[0x93bd00], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}
		memcpy(temp, &memory_region_gfx1[0x9a5380], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}
		memcpy(temp, &memory_region_gfx1[0xa3eb80], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}
		memcpy(temp, &memory_region_gfx1[0xa70300], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}
		memcpy(temp, &memory_region_gfx1[0xa84f00], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}
		memcpy(temp, &memory_region_gfx1[0xb75000], 512);
		for (i = 0; i < memory_length_gfx1; i += 512)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 512) == 0)
				memset(&memory_region_gfx1[i], 0xff, 512);
		}
		memcpy(temp, &memory_region_gfx1[0xb90600], 512);
		for (i = 0; i < memory_length_gfx1; i += 512)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 512) == 0)
				memset(&memory_region_gfx1[i], 0xff, 512);
		}
		memcpy(temp, &memory_region_gfx1[0xbcb200], 512);
		for (i = 0; i < memory_length_gfx1; i += 512)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 512) == 0)
				memset(&memory_region_gfx1[i], 0xff, 512);
		}
		memcpy(temp, &memory_region_gfx1[0xbd0000], 512);
		for (i = 0; i < memory_length_gfx1; i += 512)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 512) == 0)
				memset(&memory_region_gfx1[i], 0xff, 512);
		}
	}
	else if (!strcmp(cache_layout->name, "dstlk") || !strcmp(cache_layout->name, "nwarr"))
	{
		for (i = 0x7d; i <= 0x7f; i++)
			memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
		memset(&memory_region_gfx1[0xff0000 + (16*29)*128], 0xff, 0x10000-(16*29)*128);
		memset(&memory_region_gfx1[0x13f0000 + (16*11)*128], 0xff, 0x10000-(16*11)*128);

		memcpy(temp, &memory_region_gfx1[0x10000], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}
	}
	else if (!strcmp(cache_layout->name, "ringdest"))
	{
		for (i = 0xa0; i <= 0xab; i++)
			memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
		for (i = 0xd0; i <= 0xd3; i++)
			memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
	}
	else if (!strcmp(cache_layout->name, "mpang") || !strcmp(cache_layout->name, "mpangj"))
	{
		memset(&memory_region_gfx1[0x820000 + 16*11*128], 0xff, 16*21*128);
		memset(&memory_region_gfx1[0x830000], 0xff, 0x10000);
		memset(&memory_region_gfx1[0x840000 + (16*31+13)*128], 0xff, 0x10000-(16*31+13)*128);
		memset(&memory_region_gfx1[0x850000], 0xff, 16*16*128);
		memset(&memory_region_gfx1[0x9d0000 + (16*22+13)*128], 0xff, 0x10000-(16*22+13)*128);
		memset(&memory_region_gfx1[0x9e0000], 0xff, 0x10000);
		memset(&memory_region_gfx1[0x9f0000], 0xff, 0x10000);
		memset(&memory_region_gfx1[0xbd0000 + (16*4+8)*128], 0xff, 0x10000-(16*4+8)*128);
		memset(&memory_region_gfx1[0xbe0000], 0xff, 0x10000);
		memset(&memory_region_gfx1[0xbf0000], 0xff, 0x10000);
		memset(&memory_region_gfx1[0xd50000 + (16*12)*128], 0xff, 0x10000-(16*12)*128);
		memset(&memory_region_gfx1[0xd60000], 0xff, 0x10000);
		memset(&memory_region_gfx1[0xd70000], 0xff, 0x10000);
		memset(&memory_region_gfx1[0xdf0000 + (16*24)*128], 0xff, 0x10000-(16*24)*128);
		memset(&memory_region_gfx1[0xef0000 + (16*31)*128], 0xff, 0x10000-(16*31)*128);
		memset(&memory_region_gfx1[0xfb0000 + (16*14)*128], 0xff, 0x10000-(16*14)*128);
		memset(&memory_region_gfx1[0xff0000 + (16*12)*128], 0xff, 0x10000-(16*12)*128);
	}
	else if (!strcmp(cache_layout->name, "mmatrix"))
	{
		memset(&memory_region_gfx1[0xd67600], 0xff, (16*17+4)*128);
		for (i = 0xd7; i <= 0xff; i++)
			memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
	}
	else if (!strcmp(cache_layout->name, "pzloop2"))
	{
		memset(&memory_region_gfx1[0x170000 + 16*16*128], 0xff, 16*16*128);
		memset(&memory_region_gfx1[0x1c0000 + 16* 9*128], 0xff, 16*23*128);
		memset(&memory_region_gfx1[0x230000 + 16* 7*128], 0xff, 16*25*128);
		memset(&memory_region_gfx1[0x270000 + 16*17*128], 0xff, 16*15*128);
		memset(&memory_region_gfx1[0x290000 + 16*23*128], 0xff, 16* 9*128);
		memset(&memory_region_gfx1[0x2d0000 + 16*21*128], 0xff, 16*11*128);
		memset(&memory_region_gfx1[0x390000 + 16*30*128], 0xff, 16* 2*128);
		memset(&memory_region_gfx1[0x410000 + 16*17*128], 0xff, 16*15*128);
		memset(&memory_region_gfx1[0x530000 + 16* 6*128], 0xff, 16*26*128);
		memset(&memory_region_gfx1[0x590000 + 16* 4*128], 0xff, 16*28*128);
		memset(&memory_region_gfx1[0x670000 + 16* 9*128], 0xff, 16*23*128);
		memset(&memory_region_gfx1[0x730000 + 16*12*128], 0xff, 16*20*128);
		memset(&memory_region_gfx1[0x7a0000 + 16*10*128], 0xff, 16*22*128);
		memset(&memory_region_gfx1[0x802000 + 2*128], 0xff, 14*128);
		memset(&memory_region_gfx1[0x806800 + 4*128], 0xff, 12*128);
		memset(&memory_region_gfx1[0x810000 + 16*19*128 + 128], 0xff, 16*13*128 - 128);
		memset(&memory_region_gfx1[0xc80000 + 11*128], 0xff, 0x10000 - 11*128);
		memset(&memory_region_gfx1[0x970000 + (16*27+11)*128], 0xff, 0x10000 - (16*17+11)*128);
		memset(&memory_region_gfx1[0xeb0000 + (16*2+9)*512], 0xff, 0x10000 - (16*2+9)*512);

		for (i = 0x1d; i <= 0x1f; i++) memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
		for (i = 0x2a; i <= 0x2b; i++) memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
		for (i = 0x3a; i <= 0x3f; i++) memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
		for (i = 0x42; i <= 0x47; i++) memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
		for (i = 0x54; i <= 0x57; i++) memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
		for (i = 0x5a; i <= 0x5f; i++) memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
		for (i = 0x74; i <= 0x77; i++) memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
		for (i = 0x7b; i <= 0x7f; i++) memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
		for (i = 0x82; i <= 0x87; i++) memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
		for (i = 0x98; i <= 0x9f; i++) memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
		for (i = 0xc9; i <= 0xd7; i++) memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
		for (i = 0xec; i <= 0xff; i++) memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
	}
	else if (!strcmp(cache_layout->name, "1944"))
	{
		for (i = 0x140; i <= 0x1ff; i++)
			memset(&memory_region_gfx1[i << 16], 0xff, 0x10000);
	}
	else if (!strcmp(cache_layout->name, "choko"))
	{
		memcpy(temp, &memory_region_gfx1[0xa60000+128], 128);
		for (i = 0; i < memory_length_gfx1; i += 128)
		{
			if (memcmp(&memory_region_gfx1[i], temp, 128) == 0)
				memset(&memory_region_gfx1[i], 0xff, 128);
		}

		memset(memory_region_gfx1, 0xff, 0x800000);
		memset(&memory_region_gfx1[0x860000], 0xff, 0x10000);
		memset(&memory_region_gfx1[0x870000], 0xff, 0x10000);

		memset(&memory_region_gfx1[0xa60000+128*16], 0xff, 0x10000-128*16);
		for (i = 0xa70000; i < 0xb00000; i += 0x10000)
			memset(&memory_region_gfx1[i], 0xff, 0x10000);

		memset(&memory_region_gfx1[0xc00000+128*16*3], 0xff, 0x10000-128*16*3);
		for (i = 0xc10000; i < 0xd00000; i += 0x10000)
			memset(&memory_region_gfx1[i], 0xff, 0x10000);

		memset(&memory_region_gfx1[0xfa0000+128*16*12], 0xff, 0x10000-128*16*12);
		memset(&memory_region_gfx1[0xfb0000], 0xff, 0x10000);

		memset(&memory_region_gfx1[0xfd0000+128*16*17+128*5], 0xff, 0x10000-(128*16*17+128*5));
		memset(&memory_region_gfx1[0xfe0000], 0xff, 0x10000);
		memset(&memory_region_gfx1[0xff0000], 0xff, 0x10000);
	}
	else if (!strcmp(cache_layout->name, "jyangoku"))
	{
		memset(memory_region_gfx1, 0xff, 0x800000);
	}

	if (cache_layout->object_end == 0)
	{
		memset(memory_region_gfx1, 0xff, 0x800000);
	}
	else if (cache_layout->object_end != 0x7fffff)
	{
		for (i = cache_layout->object_end + 1; i < 0x800000; i += 0x10000)
		{
			memset(&memory_region_gfx1[i], 0xff, 0x10000);
		}
	}

	for (i = 0; i < memory_length_gfx1 >> 16; i++)
	{
		int empty = 1;
		uint32_t offset = i << 16;

		for (j = 0; j < 0x10000; j++)
		{
			if (memory_region_gfx1[offset + j] != 0xff)
			{
				empty = 0;
				break;
			}
		}

		block_empty[i] = empty;
	}
	for (; i < 0x200; i++)
	{
		block_empty[i] = 1;
	}

	for (i = 0; i < memory_length_gfx1 >> 16; i++)
	{
		if (!block_empty[i]) blocks_available++;
	}
//	printf("cache required size = %x\n", blocks_available << 16);

	size = blocks_available << 16;
	if (size != memory_length_gfx1)
	{
		printf(ROMCNV_TEXT(REMOVE_EMPTY_TILES), memory_length_gfx1, size);
	}
}


static int calc_pen_usage(void)
{
	int i, j, k, size;
	uint32_t *tile, data;
	uint32_t s0 = cache_layout->object_start;
	uint32_t e0 = cache_layout->object_end;
	uint32_t s1 = cache_layout->scroll1_start;
	uint32_t e1 = cache_layout->scroll1_end;
	uint32_t s2 = cache_layout->scroll2_start;
	uint32_t e2 = cache_layout->scroll2_end;
	uint32_t s3 = cache_layout->scroll3_start;
	uint32_t e3 = cache_layout->scroll3_end;
	uint32_t s4 = cache_layout->object2_start;
	uint32_t e4 = cache_layout->object2_end;

	gfx_total_elements[TILE08] = (memory_length_gfx1 - 0x800000) >> 6;
	gfx_total_elements[TILE16] = memory_length_gfx1 >> 7;
	gfx_total_elements[TILE32] = (memory_length_gfx1 - 0x800000) >> 9;

	if (gfx_total_elements[TILE08] >= 0x10000) gfx_total_elements[TILE08] = 0x10000;
	if (gfx_total_elements[TILE32] >= 0x10000) gfx_total_elements[TILE32] = 0x10000;

	gfx_pen_usage[TILE08] = malloc(gfx_total_elements[TILE08]);
	gfx_pen_usage[TILE16] = malloc(gfx_total_elements[TILE16]);
	gfx_pen_usage[TILE32] = malloc(gfx_total_elements[TILE32]);

	if (!gfx_pen_usage[TILE08] || !gfx_pen_usage[TILE16] || !gfx_pen_usage[TILE32])
	{
		fputs(ROMCNV_TEXT(ERROR_ALLOCATE_MEMORY), stdout);
		return 0;
	}

	memset(gfx_pen_usage[TILE08], 0, gfx_total_elements[TILE08]);
	memset(gfx_pen_usage[TILE16], 0, gfx_total_elements[TILE16]);
	memset(gfx_pen_usage[TILE32], 0, gfx_total_elements[TILE32]);

	for (i = 0; i < gfx_total_elements[TILE08]; i++)
	{
		int count = 0;
		uint32_t offset = (0x20000 + i) << 6;
		int s5 = 0x000000;
		int e5 = 0x000000;

		if (!strcmp(cache_layout->name, "pzloop2"))
		{
			s5 = 0x802800;
			e5 = 0x87ffff;
		}

		if ((offset >= s1 && offset <= e1) && !(offset >= s5 && offset <= e5))
		{
			tile = (uint32_t *)&memory_region_gfx1[offset];

			for (j = 0; j < 8; j++)
			{
				tile++;
				data = *tile++;
				for (k = 0; k < 8; k++)
				{
					if ((data & 0x0f) == 0x0f)
						count++;
					data >>= 4;
				}
			}
			if (count == 0)
				gfx_pen_usage[TILE08][i] = SPRITE_OPAQUE;
			else if (count != 8*8)
				gfx_pen_usage[TILE08][i] = SPRITE_TRANSPARENT;
		}
	}

	for (i = 0; i < gfx_total_elements[TILE16]; i++)
	{
		uint32_t s5 = 0;
		uint32_t e5 = 0;
		uint32_t offset = i << 7;

		if (!strcmp(cache_layout->name, "ssf2t"))
		{
			s5 = 0xc00000;
			e5 = 0xfaffff;
		}
		else if (!strcmp(cache_layout->name, "gigawing"))
		{
			s5 = 0xc00000;
			e5 = 0xc7ffff;
		}
		else if (!strcmp(cache_layout->name, "progear"))
		{
			s5 = 0xf27000;
			e5 = 0xf86fff;
		}

		if ((offset >= s0 && offset <= e0)
		||	(offset >= s2 && offset <= e2)
		||	(offset >= s4 && offset <= e4)
		||	(offset >= s5 && offset <= e5))
		{
			int count = 0;

			tile = (uint32_t *)&memory_region_gfx1[offset];

			for (j = 0; j < 2*16; j++)
			{
				data = *tile++;
				for (k = 0; k < 8; k++)
				{
					if ((data & 0x0f) == 0x0f)
						count++;
					data >>= 4;
				}
			}
			if (count == 0)
				gfx_pen_usage[TILE16][i] = SPRITE_OPAQUE;
			else if (count != 2*16*8)
				gfx_pen_usage[TILE16][i] = SPRITE_TRANSPARENT;
		}
	}

	for (i = 0; i < gfx_total_elements[TILE32]; i++)
	{
		int count  = 0;
		uint32_t offset = (0x4000 + i) << 9;

		if (!strcmp(cache_layout->name, "ssf2t"))
		{
			if (offset >= 0xc00000 && offset <= 0xfaffff)
				continue;
		}
		else if (!strcmp(cache_layout->name, "gigawing"))
		{
			if (offset >= 0xc00000 && offset <= 0xc7ffff)
				continue;
		}
		else if (!strcmp(cache_layout->name, "progear"))
		{
			if (offset >= 0xf27000 && offset <= 0xf86fff)
				continue;
		}

		if (offset >= s3 && offset <= e3)
		{
			tile = (uint32_t *)&memory_region_gfx1[offset];

			for (j = 0; j < 4*32; j++)
			{
				data = *tile++;
				for (k = 0; k < 8; k++)
				{
					if ((data & 0x0f) == 0x0f)
						count++;
					data >>= 4;
				}
			}
			if (count == 0)
				gfx_pen_usage[TILE32][i] = SPRITE_OPAQUE;
			else if (count != 4*32*8)
				gfx_pen_usage[TILE32][i] = SPRITE_TRANSPARENT;
		}
	}

	return 1;
}


static int load_rom_gfx1(void)
{
	int i;
	rom_file_open_result_t res;
	char fname[32], *parent;

	if ((memory_region_gfx1 = calloc(1, memory_length_gfx1)) == NULL)
	{
		error_memory("REGION_GFX1");
		return 0;
	}

	parent = strlen(parent_name) ? parent_name : NULL;

	for (i = 0; i < num_gfx1rom; )
	{
		strcpy(fname, gfx1rom[i].name);
		if ((res = file_open(game_name, parent_name, gfx1rom[i].crc, fname)) < 0)
		{
			if (res == ROM_FILE_OPEN_NOT_FOUND)
				error_file(fname);
			else
				error_crc(fname);
			return 0;
		}
		printf(ROMCNV_TEXT(LOADING_FILE), fname);

		i = rom_load(gfx1rom, memory_region_gfx1, i, num_gfx1rom);

		file_close();
	}

	return 1;
}


static int load_rom_info(const char *game_name)
{
	game_database_game_t game;
	game_database_error_t error;
	uint32_t i;

	num_gfx1rom = 0;
	memory_length_gfx1 = 0;

	error = game_database_find_game(&cps2_game_database, game_name, &game);
	if (error == GAME_DATABASE_ERROR_NOT_FOUND)
		return 2;
	if (error != GAME_DATABASE_OK)
	{
		printf("ERROR: Could not read game database record for %s: %s\n",
			game_name, game_database_error_string(error));
		return 3;
	}

	if (strlen(game.parent_name) >= sizeof(parent_name))
		return 3;
	strcpy(parent_name, game.parent_name);

	for (i = 0; i < game.region_count; i++)
	{
		game_database_region_t region;
		uint32_t j;

		error = game_database_get_region(&cps2_game_database, &game, i, &region);
		if (error != GAME_DATABASE_OK)
			return 3;
		if (region.type != GAME_DATABASE_REGION_GFX1)
			continue;
		if (region.rom_count > MAX_GFX1ROM)
			return 3;

		memory_length_gfx1 = region.size;
		for (j = 0; j < region.rom_count; j++)
		{
			game_database_rom_t source;
			struct rom_t *dest = &gfx1rom[j];

			error = game_database_get_rom(&cps2_game_database, &region, j, &source);
			if (error != GAME_DATABASE_OK || strlen(source.name) >= sizeof(dest->name))
				return 3;
			dest->type = source.type;
			dest->offset = source.offset;
			dest->length = source.length;
			dest->crc = source.crc;
			dest->group = source.group;
			dest->skip = source.skip;
			strcpy(dest->name, source.name);
		}
		num_gfx1rom = region.rom_count;
	}

	if (memory_length_gfx1 == 0 || !set_cache_parent_policy(&game))
		return 3;
	return 0;
}


static void free_memory(void)
{
	if (memory_region_gfx1) free(memory_region_gfx1);
	if (gfx_pen_usage[TILE08]) free(gfx_pen_usage[TILE08]);
	if (gfx_pen_usage[TILE16]) free(gfx_pen_usage[TILE16]);
	if (gfx_pen_usage[TILE32]) free(gfx_pen_usage[TILE32]);
}


static int convert_rom(char *game_name)
{
	int res;
	printf(ROMCNV_TEXT(CHECKING_ROM_FILE), game_name);

	memory_region_gfx1 = NULL;
	memory_length_gfx1 = 0;

	gfx_pen_usage[0] = NULL;
	gfx_pen_usage[1] = NULL;
	gfx_pen_usage[2] = NULL;

	if ((res = load_rom_info(game_name)) != 0)
	{
		switch (res)
		{
		case 1: fputs(ROMCNV_TEXT(ERROR_GAME_NOT_SUPPORTED), stdout); break;
		case 2: fputs(ROMCNV_TEXT(ERROR_ROM_NOT_FOUND), stdout); break;
		case 3: fputs(ROMCNV_TEXT(ERROR_CPS2_DATABASE_NOT_FOUND), stdout); break;
		}
		return 0;
	}

	if (strlen(parent_name))
		printf(ROMCNV_TEXT(CLONE_SET_PARENT), parent_name);

	cache_layout = cps2_cache_layout_find(game_name, cache_name);

	if (cache_layout)
	{
		if (load_rom_gfx1())
		{
			cps2_gfx_decode();
			clear_empty_blocks();
			if (calc_pen_usage()) return 1;
		}
	}
	else
	{
		fputs(ROMCNV_TEXT(ERROR_UNKNOWN_ROMSET), stdout);
	}

	return 0;
}


static int create_raw_cache(char *game_name)
{
	int fp;
	int i, offset;
	char version[8];
	uint32_t header_size, aligned_size, block[0x200];
	char fname[PATH_MAX];

	sprintf(version, "CPS2V%d%d\0", VERSION_MAJOR, VERSION_MINOR);

	change_directory("cache");

	header_size = 8;
	header_size += gfx_total_elements[TILE08];
	header_size += gfx_total_elements[TILE16];
	header_size += gfx_total_elements[TILE32];
	header_size += 0x200 * sizeof(uint32_t);

	aligned_size = (header_size + 0xffff) & ~0xffff;

	offset = aligned_size;
	for (i = 0; i < 0x200; i++)
	{
		if (block_empty[i])
		{
			block[i] = 0xffffffff;
		}
		else
		{
			if (lsb_first)
			{
				block[i] = offset;
			}
			else
			{
				block[i] = ((offset & 0x000000ff) << 24)
						 | ((offset & 0x0000ff00) <<  8)
						 | ((offset & 0x00ff0000) >>  8)
						 | ((offset & 0xff000000) >> 24);
			}
			offset += 0x10000;
		}
	}

	sprintf(fname, "%s.cache", game_name);
	fp = open(fname, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fp < 0)
	{
		change_directory("..");
		fputs(ROMCNV_TEXT(ERROR_CREATE_FILE), stdout);
		return 0;
	}

	printf(ROMCNV_TEXT(CPS2_CACHE_NAME_RAW), delimiter, game_name);
	fputs(ROMCNV_TEXT(CREATE_CACHE_FILE), stdout);

	write(fp, version, sizeof(version));
	write(fp, gfx_pen_usage[TILE08], gfx_total_elements[TILE08]);
	write(fp, gfx_pen_usage[TILE16], gfx_total_elements[TILE16]);
	write(fp, gfx_pen_usage[TILE32], gfx_total_elements[TILE32]);
	write(fp, block, 0x200 * sizeof(uint32_t));

	{
		static const char zero = 0;
		for (i = header_size; i < (int)aligned_size; i++)
			write(fp, &zero, 1);
	}

	for (i = 0; i < 0x200; i++)
	{
		if (block_empty[i]) continue;

		write(fp, &memory_region_gfx1[i << 16], 0x10000);
	}

	close(fp);

	change_directory("..");

	return 1;
}


static void print_progress(int count, int total)
{
	int i, progress = (count * 100) / total;

	printf("%3d%% [", progress);
	for (i = 0; i < progress/2; i++) printf("*");
	for (; i < 50; i++) printf(".");
	printf("]\r");
}


static int create_zip_cache(char *game_name)
{
	zip_writer_t writer = {0};
	zip_writer_segment_t cache_info[5];
	uint32_t block, res = 0, total = 0, count = 0;
	char version[8], fname[PATH_MAX], zipname[PATH_MAX];

	sprintf(version, "CPS2V%d%d\0", VERSION_MAJOR, VERSION_MINOR);

	change_directory("cache");

	sprintf(zipname, "%s%ccache%c%s_cache.zip", launchDir, delimiter, delimiter, game_name);
	remove(zipname);

	printf(ROMCNV_TEXT(CPS2_CACHE_NAME_ZIP), delimiter, game_name);
	fputs(ROMCNV_TEXT(CREATE_CACHE_FILE), stdout);
	if (!zip_writer_open(&writer, zipname))
	{
		printf(ROMCNV_TEXT(CPS2_ERROR_CREATE_ZIP), delimiter, game_name);
		goto error;
	}

	printf(ROMCNV_TEXT(CPS2_COMPRESS_ZIP), delimiter, game_name);

	for (block = 0; block < 0x200; block++)
		if (!block_empty[block]) total++;
	total++;

	print_progress(0, total);

	for (block = 0; block < 0x200; block++)
	{
		if (block_empty[block]) continue;

		sprintf(fname, "%03x", block);
		if (!zip_writer_add_mem(&writer, fname,
		                        &memory_region_gfx1[block << 16], 0x10000))
			goto error;
		print_progress(++count, total);
	}

	cache_info[0].data = version;
	cache_info[0].size = 8;
	cache_info[1].data = gfx_pen_usage[TILE08];
	cache_info[1].size = gfx_total_elements[TILE08];
	cache_info[2].data = gfx_pen_usage[TILE16];
	cache_info[2].size = gfx_total_elements[TILE16];
	cache_info[3].data = gfx_pen_usage[TILE32];
	cache_info[3].size = gfx_total_elements[TILE32];
	cache_info[4].data = block_empty;
	cache_info[4].size = 0x200;
	if (!zip_writer_add_segments(&writer, "cache_info", cache_info, 5))
		goto error;

	print_progress(++count, total);
	printf("\n");

	if (!zip_writer_close(&writer))
		goto error;
	res = 1;
	goto done;

error:
	zip_writer_abort(&writer);
	remove(zipname);

done:

	if (!res) fputs(ROMCNV_TEXT(ERROR_CREATE_FILE), stdout);

	change_directory("..");

	return res;
}


int main(int argc, char *argv[])
{
	char *p, path[PATH_MAX];
	int i, path_found = 0, all = 0, zip = 0, res = 1;
	check_byte_order();
	romcnv_translation_init(argc, argv);

	printf("----------------------------------------------\n");
	printf(ROMCNV_TEXT(CPS2_BANNER), VERSION_STR);
	printf("----------------------------------------------\n\n");

	if (argc > 1)
	{
		for (i = 1; i < argc; i++)
		{
			int translation_span = romcnv_translation_option_span(argc, argv, i);
			if (translation_span != 0)
			{
				i += translation_span - 1;
			}
			else if (!strcasecmp(argv[i], "-all"))
			{
				all = 1;
			}
			else if (!strcasecmp(argv[i], "-zip"))
			{
				zip = 1;
			}
			else if (argv[i][0] == '-')
			{
				printf(ROMCNV_TEXT(ERROR_UNKNOWN_OPTION), argv[i]);
				return 1;
			}
			else if (strchr(argv[i], DELIMITER) != NULL)
			{
				path_found = i;
			}
		}
	}

	if (!path_found)
	{
		printf("usage: romcnv_cps2 fullpath%cgamename.zip [-zip] [-lang en|zh-Hans]\n", DELIMITER);
		printf("  or   romcnv_cps2 fullpath -all [-zip] [-lang en|zh-Hans]\n\n", DELIMITER);
		return 0;
	}

	if (chdir("cache") != 0)
	{
		if (mkdir("cache", 0777) != 0)
		{
			fputs(ROMCNV_TEXT(CPS2_ERROR_CREATE_CACHE_DIR), stdout);
			goto error;
		}
	}
	else change_directory("..");

	getcwd(launchDir, PATH_MAX);
	strcat(launchDir, "/");

	snprintf(path, sizeof(path), "%sgame_database.cps2", launchDir);
	{
		game_database_error_t database_error = game_database_open(
			&cps2_game_database, path, GAME_DATABASE_CORE_CPS2);
		if (database_error != GAME_DATABASE_OK)
		{
				printf("ERROR: Could not load game_database.cps2: %s\n",
					game_database_error_string(database_error));
				res = 0;
				goto error;
			}
	}
	snprintf(path, sizeof(path), "%s%s", launchDir, CPS2_CACHE_LAYOUT_FILENAME);
	switch (cps2_cache_layouts_load(path))
	{
	case CPS2_CACHE_LAYOUT_OK:
		break;
	case CPS2_CACHE_LAYOUT_NOT_FOUND:
		fputs(ROMCNV_TEXT(ERROR_CPS2_LAYOUTS_NOT_FOUND), stdout);
		res = 0;
		goto error;
	case CPS2_CACHE_LAYOUT_INVALID:
	default:
		fputs(ROMCNV_TEXT(ERROR_CPS2_LAYOUTS_INVALID), stdout);
		res = 0;
		goto error;
	}

	if (all)
	{
		strcpy(zip_dir, argv[path_found]);
		strcpy(game_dir, zip_dir);
		strcat(game_dir, "/");

		for (i = 0; i < cps2_cache_layout_count(); i++)
		{
			const cps2_cache_layout_t *layout = cps2_cache_layout_at(i);

			res = 1;

			strcpy(game_name, layout->name);

			printf("\n-------------------------------------------\n");
			printf("  ROM set: %s\n", game_name);
			printf("-------------------------------------------\n\n");

			change_directory(launchDir);
			if (!convert_rom(game_name))
			{
				fputs(ROMCNV_TEXT(ERROR_CONVERT_FAILED_SKIP), stdout);
			}
			else
			{
				if (zip)
					res = create_zip_cache(game_name);
				else
					res = create_raw_cache(game_name);

				if (res) fputs(ROMCNV_TEXT(DONE), stdout);
			}
			free_memory();
		}
		fputs(ROMCNV_TEXT(COMPLETE), stdout);
		fputs(ROMCNV_TEXT(CPS2_COPY_ALL), stdout);
	}
	else
	{
		strcpy(game_dir, argv[path_found]);

		if ((p = strrchr(game_dir, delimiter)) != NULL)
		{
			strcpy(game_name, p + 1);
			strcpy(zip_dir, game_dir);
			*strrchr(zip_dir, delimiter) = '\0';
		}
		else
		{
			strcpy(game_name, game_dir);
			strcpy(zip_dir, "");
		}

		p = game_name;
		while (*p)
		{
			*p = tolower(*p);
			*p++;
		}

		printf(ROMCNV_TEXT(PATH), zip_dir);
		printf(ROMCNV_TEXT(CPS2_FILE_NAME), game_name);

		if ((p = strrchr(game_name, '.')) == NULL)
		{
			fputs(ROMCNV_TEXT(ERROR_INVALID_PATH), stdout);
			goto error;
		}
		*p = '\0';

		change_directory(launchDir);
		if (!convert_rom(game_name))
		{
			res = 0;
		}
		else
		{
			if (zip)
				res = create_zip_cache(game_name);
			else
				res = create_raw_cache(game_name);
		}
		if (res)
		{
			fputs(ROMCNV_TEXT(COMPLETE), stdout);
			printf(ROMCNV_TEXT(CPS2_COPY_SINGLE), delimiter, game_name);

		}
		free_memory();
	}

error:
	game_database_close(&cps2_game_database);
	return res;
}
