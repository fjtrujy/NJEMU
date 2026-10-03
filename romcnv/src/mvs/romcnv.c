/*****************************************************************************

	romcnv.c

	ROM converter for MVSPSP

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
#include "romcnv.h"
#include "common.h"
#include "common/game_metadata.h"
#include "neogeo.h"
#include "translation.h"
#include "zip_writer.h"

#define MAX_GAMES			512

#define SPRITE_BLANK		0x00
#define SPRITE_TRANSPARENT	0x01
#define SPRITE_OPAQUE		0x02

#define MAX_GFX2ROM			4
#define MAX_GFX3ROM			16
#define MAX_SND1ROM			8

enum
{
	REGION_CPU1 = 0,
	REGION_CPU2,
	REGION_GFX1,
	REGION_GFX2,
	REGION_GFX3,
	REGION_SOUND1,
	REGION_SOUND2,
	REGION_USER1,
	REGION_SKIP
};

enum
{
	TILE_FIX = 0,
	TILE_SPR,
	TILE_TYPE_MAX
};


/******************************************************************************
	Global Variables
******************************************************************************/

uint8_t *memory_region_gfx2;
uint8_t *memory_region_gfx3;
uint8_t *memory_region_sound1;

uint32_t memory_length_gfx2;
uint32_t memory_length_gfx3;
uint32_t memory_length_sound1;


/******************************************************************************
	Local Variables
******************************************************************************/

static uint32_t gfx_total_elements[TILE_TYPE_MAX];
static uint8_t  *gfx_pen_usage[TILE_TYPE_MAX];

static int disable_sound;
static int machine_driver_type;
static int machine_init_type;
static int machine_input_type;
static int machine_screen_type;

static void change_directory(const char *path)
{
	if (chdir(path) != 0)
		perror(path);
}

static struct rom_t gfx2rom[MAX_GFX2ROM];
static struct rom_t gfx3rom[MAX_GFX3ROM];
static struct rom_t snd1rom[MAX_SND1ROM];

static int num_gfx2rom;
static int num_gfx3rom;
static int num_snd1rom;

static int encrypt_gfx2;
static int encrypt_gfx3;
static int encrypt_snd1;

static int convert_crom;
static int convert_srom;
static int convert_vrom;

static int psp2k;

static char game_names[MAX_GAMES][16];
static game_metadata_t mvs_game_metadata;

static int set_cache_conversion_policy(const char *game_name)
{
	game_metadata_entry_t entry;

	if (!game_metadata_find(&mvs_game_metadata, game_name, &entry))
	{
		printf("ERROR: game metadata for %s is missing.\n", game_name);
		return 0;
	}

	if (parent_name[0])
	{
		convert_crom = (entry.core_flags & GAME_METADATA_MVS_OWNS_CROM) != 0;
		convert_srom = (entry.core_flags & GAME_METADATA_MVS_OWNS_SROM) != 0;
		convert_vrom = (entry.core_flags & GAME_METADATA_MVS_OWNS_VROM) != 0;
	}
	else
	{
		convert_crom = 1;
		convert_srom = 1;
		convert_vrom = 1;
	}
	return 1;
}


/******************************************************************************
	MVS Functions
******************************************************************************/

static void neogeo_decode_spr(uint8_t *mem, uint32_t length, uint8_t *usage)
{
	uint32_t i;

	for (i = 0; i < gfx_total_elements[TILE_SPR]; i++)
	{
		uint8_t swap[128], *gfxdata;
		uint32_t x, y, pen, opaque = 0;

		gfxdata = &mem[128 * i];

		memcpy(swap, gfxdata, 128);

		for (y = 0; y < 16; y++)
		{
			uint32_t dw, data;

			dw = 0;
			for (x = 0; x < 8; x++)
			{
				pen  = ((swap[64 + 4*y + 3] >> x) & 1) << 3;
				pen |= ((swap[64 + 4*y + 1] >> x) & 1) << 2;
				pen |= ((swap[64 + 4*y + 2] >> x) & 1) << 1;
				pen |= ((swap[64 + 4*y + 0] >> x) & 1) << 0;
				opaque += (pen & 0x0f) != 0;
				dw |= pen << 4*x;
			}

			data = ((dw & 0x0000000f) >>  0) | ((dw & 0x000000f0) <<  4)
				 | ((dw & 0x00000f00) <<  8) | ((dw & 0x0000f000) << 12)
				 | ((dw & 0x000f0000) >> 12) | ((dw & 0x00f00000) >>  8)
				 | ((dw & 0x0f000000) >>  4) | ((dw & 0xf0000000) >>  0);

			*(gfxdata++) = data >>  0;
			*(gfxdata++) = data >>  8;
			*(gfxdata++) = data >> 16;
			*(gfxdata++) = data >> 24;

			dw = 0;
			for (x = 0;x < 8;x++)
			{
				pen  = ((swap[4*y + 3] >> x) & 1) << 3;
				pen |= ((swap[4*y + 1] >> x) & 1) << 2;
				pen |= ((swap[4*y + 2] >> x) & 1) << 1;
				pen |= ((swap[4*y + 0] >> x) & 1) << 0;
				opaque += (pen & 0x0f) != 0;
				dw |= pen << 4*x;
			}

			data = ((dw & 0x0000000f) >>  0) | ((dw & 0x000000f0) <<  4)
				 | ((dw & 0x00000f00) <<  8) | ((dw & 0x0000f000) << 12)
				 | ((dw & 0x000f0000) >> 12) | ((dw & 0x00f00000) >>  8)
				 | ((dw & 0x0f000000) >>  4) | ((dw & 0xf0000000) >>  0);

			*(gfxdata++) = data >>  0;
			*(gfxdata++) = data >>  8;
			*(gfxdata++) = data >> 16;
			*(gfxdata++) = data >> 24;
		}

		if (opaque)
			*usage = (opaque == 256) ? SPRITE_OPAQUE : SPRITE_TRANSPARENT;
		else
			*usage = SPRITE_BLANK;
		usage++;
	}
}


static int load_rom_gfx2(void)
{
	if (encrypt_gfx2)
	{
		int i;
		char fname[32], *parent;

		gfx_total_elements[TILE_FIX] = memory_length_gfx2 / 32;

		if ((memory_region_gfx2 = calloc(1, memory_length_gfx2)) == NULL)
		{
			error_memory("REGION_GFX2");
			return 0;
		}
		if ((gfx_pen_usage[TILE_FIX] = calloc(1, gfx_total_elements[TILE_FIX])) == NULL)
		{
			error_memory("PEN_USAGE_GFX2");
			return 0;
		}

		parent = strlen(parent_name) ? parent_name : NULL;

		for (i = 0; i < num_gfx2rom; )
		{
			rom_file_open_result_t res;

			strcpy(fname, gfx2rom[i].name);
			if ((res = file_open(game_name, parent, gfx2rom[i].crc, fname)) < 0)
			{
				if (res == ROM_FILE_OPEN_NOT_FOUND)
					error_file(fname);
				else
					error_crc(fname);
				return 0;
			}
			printf(ROMCNV_TEXT(LOADING_FILE), fname);
			i = rom_load(gfx2rom, memory_region_gfx2, i, num_gfx2rom);

			file_close();
		}
	}

	return 1;
}


static int load_rom_gfx3(void)
{
	int i;
	char fname[32], *parent;

	gfx_total_elements[TILE_SPR] = memory_length_gfx3 / 128;

	if ((memory_region_gfx3 = calloc(1, memory_length_gfx3)) == NULL)
	{
		error_memory("REGION_GFX3");
		return 0;
	}
	if ((gfx_pen_usage[TILE_SPR] = calloc(1, gfx_total_elements[TILE_SPR])) == NULL)
	{
		error_memory("PEN_USAGE_GFX3");
		return 0;
	}

	parent = strlen(parent_name) ? parent_name : NULL;

	for (i = 0; i < num_gfx3rom; )
	{
		rom_file_open_result_t res;

		strcpy(fname, gfx3rom[i].name);
		if ((res = file_open(game_name, parent, gfx3rom[i].crc, fname)) < 0)
		{
			if (res == ROM_FILE_OPEN_NOT_FOUND)
				error_file(fname);
			else
				error_crc(fname);
			return 0;
		}
		printf(ROMCNV_TEXT(LOADING_FILE), fname);
		i = rom_load(gfx3rom, memory_region_gfx3, i, num_gfx3rom);

		file_close();
	}

	return 1;
}


static int load_rom_sound1(void)
{
	int i;
	char fname[32], *parent;

	if ((memory_region_sound1 = calloc(1, memory_length_sound1)) == NULL)
	{
		error_memory("REGION_SOUND1");
		return 0;
	}

	parent = strlen(parent_name) ? parent_name : NULL;

	for (i = 0; i < num_snd1rom; )
	{
		rom_file_open_result_t res;

		strcpy(fname, snd1rom[i].name);
		if ((res = file_open(game_name, parent, snd1rom[i].crc, fname)) < 0)
		{
			if (res == ROM_FILE_OPEN_NOT_FOUND)
				error_file(fname);
			else
				error_crc(fname);
			return 0;
		}

		printf(ROMCNV_TEXT(LOADING_FILE), fname);
		i = rom_load(snd1rom, memory_region_sound1, i, num_snd1rom);

		file_close();
	}

	return 1;
}


static ssize_t fd_readline(int fd, char *buf, size_t size)
{
	size_t i = 0;
	char c;
	while (i < size - 1) {
		if (read(fd, &c, 1) <= 0) break;
		buf[i++] = c;
		if (c == '\n') break;
	}
	buf[i] = '\0';
	return (ssize_t)i;
}

static int build_game_list(void)
{
	int fp;
	char path[PATH_MAX];
	char buf[256];
	int num_games = 0;

	sprintf(path, "%srominfo.mvs", launchDir);

	fp = open(path, O_RDONLY);
	if (fp >= 0)
	{
		while (fd_readline(fp, buf, 255) > 0)
		{
			if (buf[0] == '/' && buf[1] == '/')
				continue;

			if (buf[0] != '\t')
			{
				if (str_cmp(buf, "FILENAME(") == 0)
				{
					char *name;

					strtok(buf, " ");
					strcpy(game_names[num_games], strtok(NULL, " ,"));
					num_games++;
				}
			}
		}
		close(fp);
		return num_games;
	}
	return 0;
}


static int load_rom_info(const char *game_name)
{
	int fp;
	char path[PATH_MAX];
	char buf[256];
	int rom_start = 0;
	int region = 0;
	int total_size = 0;

	num_gfx2rom = 0;
	num_gfx3rom = 0;
	num_snd1rom = 0;

	machine_driver_type = 0;
	machine_input_type  = 0;
	machine_init_type   = 0;
	machine_screen_type = 0;

	encrypt_gfx2 = 0;
	encrypt_gfx3 = 0;
	encrypt_snd1 = 0;

	disable_sound = 0;

	sprintf(path, "%srominfo.mvs", launchDir);

	fp = open(path, O_RDONLY);
	if (fp >= 0)
	{
		while (fd_readline(fp, buf, 255) > 0)
		{
			if (buf[0] == '/' && buf[1] == '/')
				continue;

			if (buf[0] != '\t')
			{
				if (buf[0] == '\r' || buf[0] == '\n')
				{
					// Newline
					continue;
				}
				else if (str_cmp(buf, "FILENAME(") == 0)
				{
					char *name, *parent;
					char *machine, *input, *init, *rotate;

					strtok(buf, " ");
					name    = strtok(NULL, " ,");
					parent  = strtok(NULL, " ,");
					machine = strtok(NULL, " ,");
					input   = strtok(NULL, " ,");
					init    = strtok(NULL, " ,");
					rotate  = strtok(NULL, " ");

					if (strcasecmp(name, game_name) == 0)
					{
						if (str_cmp(parent, "neogeo") == 0)
						{
							parent_name[0] = '\0';
						}
						else if (str_cmp(parent, "pcb") == 0)
						{
							parent_name[0] = '\0';
						}
						else
						{
							strcpy(parent_name, parent);
						}

						sscanf(machine, "%d", &machine_driver_type);
						sscanf(input, "%d", &machine_input_type);
						sscanf(init, "%d", &machine_init_type);
						sscanf(rotate, "%d", &machine_screen_type);
						rom_start = 1;
					}
				}
/*
				else if (rom_start && str_cmp(buf, "END") == 0)
				{
					close(fp);
					if (total_size >= 16*1024*1024)
						return 0;
					else
						return 4;
				}
*/
				else if (rom_start && str_cmp(buf, "END") == 0)
				{
					close(fp);
					if (psp2k)
						{
						if ((total_size > 0x2b50000) || (encrypt_gfx3))
						return 0;
						else
						return 4;
						}
					else
						{
						if (total_size >= 16*1024*1024)
						return 0;
						else
						return 4;
						}
				}
			}
			else if (rom_start)
			{
				if (str_cmp(&buf[1], "REGION(") == 0)
				{
					char *size, *type, *flag;
					int encrypted = 0;
					int size2;

					strtok(&buf[1], " ");
					size = strtok(NULL, " ,");
					type = strtok(NULL, " ,");
					flag = strtok(NULL, " ");

					sscanf(size, "%x", &size2);
					total_size += size2;

					if (strstr(flag, "SOUND_DISABLE")) disable_sound = 1;
					if (strstr(flag, "ENCRYPTED")) encrypted = 1;

					if (strcmp(type, "GFX2") == 0)
					{
						sscanf(size, "%x", &memory_length_gfx2);
						encrypt_gfx2 = encrypted;
						region = REGION_GFX2;
					}
					else if (strcmp(type, "GFX3") == 0)
					{
						sscanf(size, "%x", &memory_length_gfx3);
						encrypt_gfx3 = encrypted;
						region = REGION_GFX3;
					}
					else if (strcmp(type, "SOUND1") == 0)
					{
						sscanf(size, "%x", &memory_length_sound1);
						encrypt_snd1 = encrypted;
						region = REGION_SOUND1;
					}
					else
					{
						region = REGION_SKIP;
					}
				}
				else if (str_cmp(&buf[1], "ROM(") == 0)
				{
					char *type, *name, *offset, *length, *crc;

					strtok(&buf[1], " ");
					type   = strtok(NULL, " ,");
					if (type[0] != '1')
						name = strtok(NULL, " ,");
					else
						name = NULL;
					offset = strtok(NULL, " ,");
					length = strtok(NULL, " ,");
					crc    = strtok(NULL, " ");

					switch (region)
					{
					case REGION_GFX2:
						sscanf(type, "%x", &gfx2rom[num_gfx2rom].type);
						sscanf(offset, "%x", &gfx2rom[num_gfx2rom].offset);
						sscanf(length, "%x", &gfx2rom[num_gfx2rom].length);
						sscanf(crc, "%x", &gfx2rom[num_gfx2rom].crc);
						if (name) strcpy(gfx2rom[num_gfx2rom].name, name);
						gfx2rom[num_gfx2rom].group = 0;
						gfx2rom[num_gfx2rom].skip = 0;
						num_gfx2rom++;
						break;

					case REGION_GFX3:
						sscanf(type, "%x", &gfx3rom[num_gfx3rom].type);
						sscanf(offset, "%x", &gfx3rom[num_gfx3rom].offset);
						sscanf(length, "%x", &gfx3rom[num_gfx3rom].length);
						sscanf(crc, "%x", &gfx3rom[num_gfx3rom].crc);
						if (name) strcpy(gfx3rom[num_gfx3rom].name, name);
						gfx3rom[num_gfx3rom].group = 0;
						gfx3rom[num_gfx3rom].skip = 0;
						num_gfx3rom++;
						break;

					case REGION_SOUND1:
						sscanf(type, "%x", &snd1rom[num_snd1rom].type);
						sscanf(offset, "%x", &snd1rom[num_snd1rom].offset);
						sscanf(length, "%x", &snd1rom[num_snd1rom].length);
						sscanf(crc, "%x", &snd1rom[num_snd1rom].crc);
						if (name) strcpy(snd1rom[num_snd1rom].name, name);
						snd1rom[num_snd1rom].group = 0;
						snd1rom[num_snd1rom].skip = 0;
						num_snd1rom++;
						break;
					}
				}
				else if (str_cmp(&buf[1], "ROMX(") == 0)
				{
					char *type, *name, *offset, *length, *crc;
					char *group, *skip;

					strtok(&buf[1], " ");
					type   = strtok(NULL, " ,");
					if (type[0] != '1')
						name = strtok(NULL, " ,");
					else
						name = NULL;
					offset = strtok(NULL, " ,");
					length = strtok(NULL, " ,");
					crc    = strtok(NULL, " ,");
					group  = strtok(NULL, " ,");
					skip   = strtok(NULL, " ");

					switch (region)
					{
					case REGION_GFX3:
						sscanf(type, "%x", &gfx3rom[num_gfx3rom].type);
						sscanf(offset, "%x", &gfx3rom[num_gfx3rom].offset);
						sscanf(length, "%x", &gfx3rom[num_gfx3rom].length);
						sscanf(crc, "%x", &gfx3rom[num_gfx3rom].crc);
						sscanf(group, "%x", &gfx3rom[num_gfx3rom].group);
						sscanf(skip, "%x", &gfx3rom[num_gfx3rom].skip);
						if (name) strcpy(gfx3rom[num_gfx3rom].name, name);
						num_gfx3rom++;
						break;

					case REGION_SOUND1:
						sscanf(type, "%x", &snd1rom[num_snd1rom].type);
						sscanf(offset, "%x", &snd1rom[num_snd1rom].offset);
						sscanf(length, "%x", &snd1rom[num_snd1rom].length);
						sscanf(crc, "%x", &snd1rom[num_snd1rom].crc);
						sscanf(group, "%x", &snd1rom[num_snd1rom].group);
						sscanf(skip, "%x", &snd1rom[num_snd1rom].skip);
						if (name) strcpy(snd1rom[num_snd1rom].name, name);
						num_snd1rom++;
						break;
					}
				}
			}
		}
		close(fp);
		return 2;
	}
	return 3;
}


void free_memory(void)
{
	if (memory_region_gfx2)      free(memory_region_gfx2);
	if (memory_region_gfx3)      free(memory_region_gfx3);
	if (memory_region_sound1)    free(memory_region_sound1);
	if (gfx_pen_usage[TILE_SPR]) free(gfx_pen_usage[TILE_SPR]);
	if (gfx_pen_usage[TILE_FIX]) free(gfx_pen_usage[TILE_FIX]);
}


static int convert_rom(char *game_name)
{
	int i, res;
	printf(ROMCNV_TEXT(CHECKING_ROM_FILE), game_name);
	memory_region_gfx2   = NULL;
	memory_region_gfx3   = NULL;
	memory_region_sound1 = NULL;

	memory_length_gfx2   = 0;
	memory_length_gfx3   = 0;
	memory_length_sound1 = 0;

	gfx_pen_usage[TILE_FIX] = NULL;
	gfx_pen_usage[TILE_SPR] = NULL;

	if ((res = load_rom_info(game_name)) != 0)
	{
		switch (res)
		{
		case 1: fputs(ROMCNV_TEXT(ERROR_GAME_NOT_SUPPORTED), stdout); break;
		case 2: fputs(ROMCNV_TEXT(ERROR_ROM_NOT_FOUND), stdout); break;
		case 3: fputs(ROMCNV_TEXT(ERROR_MVS_ROMINFO_NOT_FOUND), stdout); break;
		case 4: fputs(ROMCNV_TEXT(INFO_NO_CONVERSION_REQUIRED), stdout); break;
		}
		return 0;
	}

	if (strlen(parent_name))
		printf(ROMCNV_TEXT(CLONE_SET_PARENT), parent_name);
	if (!set_cache_conversion_policy(game_name))
		return 0;
	if (!convert_crom && !convert_srom && !convert_vrom)
	{
		printf("INFO: Cache data inherited from parent; no conversion needed.\n");
		return 2;
	}

	if (psp2k) disable_sound = 0;

	if (convert_vrom && (encrypt_snd1 || disable_sound))
	{
		if (load_rom_sound1())
		{
			if (encrypt_snd1)
			{
				switch (machine_init_type)
				{
				case INIT_kof2002:	neo_pcm2_swap(0);		break;
				case INIT_mslug5:	neo_pcm2_swap(2);		break;
				case INIT_svc:     neo_pcm2_swap(3);       break;
				case INIT_samsho5:	neo_pcm2_swap(4);		break;
				case INIT_kof2003:	neo_pcm2_swap(5);		break;
				case INIT_samsh5sp:neo_pcm2_swap(6);		break;
				case INIT_pnyaa:	neo_pcm2_snk_1999(4);	break;
				case INIT_mslug4:	neo_pcm2_snk_1999(8);	break;
				case INIT_rotd:	neo_pcm2_snk_1999(16);	break;
				case INIT_matrim:	neo_pcm2_swap(1);		break;

				case INIT_ms5pcb:	neo_pcm2_swap(2);		break;
				case INIT_svcpcb:	neo_pcm2_swap(3);		break;
				case INIT_kf2k3pcb:neo_pcm2_swap(5);		break;

				case INIT_kof2002b:neo_pcm2_swap(0);		break;
				case INIT_kf2k2pls:neo_pcm2_swap(0);		break;
				case INIT_kf2k2plc:neo_pcm2_swap(0);		break;
				case INIT_kf2k2mp:	neo_pcm2_swap(0);		break;
				case INIT_kf2k2mp2:neo_pcm2_swap(0);		break;
				case INIT_ms5plus:	neo_pcm2_swap(2);		break;
				case INIT_mslug5b:	neo_pcm2_swap(2);		break;
				case INIT_samsho5b:samsho5b_vx_decrypt();	break;
				case INIT_lans2004:lans2004_vx_decrypt();	break;

				default: goto error;
				}
			}
		}
		else
		{
			goto error;
		}
	}

	if (encrypt_gfx2 || encrypt_gfx3)
	{
		if (load_rom_gfx2() && load_rom_gfx3())
		{
			switch (machine_init_type)
			{
			case INIT_kof99:	kof99_neogeo_gfx_decrypt(0x00);		break;
			case INIT_kof99k:	kof99_neogeo_gfx_decrypt(0x00);		break;
			case INIT_garou:	kof99_neogeo_gfx_decrypt(0x06);		break;
			case INIT_garouh:	kof99_neogeo_gfx_decrypt(0x06);		break;
			case INIT_mslug3:	kof99_neogeo_gfx_decrypt(0xad);		break;
			case INIT_mslug3h:	kof99_neogeo_gfx_decrypt(0xad);		break;
			case INIT_kof2000:	kof2000_neogeo_gfx_decrypt(0x00);	break;
			case INIT_kof2000n:kof2000_neogeo_gfx_decrypt(0x00);	break;
			case INIT_zupapa:	kof99_neogeo_gfx_decrypt(0xbd);		break;
			case INIT_sengoku3:kof99_neogeo_gfx_decrypt(0xfe);		break;
			case INIT_kof2001:	kof2000_neogeo_gfx_decrypt(0x1e);	break;
			case INIT_kof2002:	kof2000_neogeo_gfx_decrypt(0xec);	break;
			case INIT_mslug5:	kof2000_neogeo_gfx_decrypt(0x19);	break;
			case INIT_svc:     kof2000_neogeo_gfx_decrypt(0x57);	break;
			case INIT_samsho5:	kof2000_neogeo_gfx_decrypt(0x0f);	break;
			case INIT_kof2003:	kof2000_neogeo_gfx_decrypt(0x9d);	break;
			case INIT_samsh5sp:kof2000_neogeo_gfx_decrypt(0x0d);	break;
			case INIT_nitd:	kof99_neogeo_gfx_decrypt(0xff);		break;
			case INIT_s1945p:	kof99_neogeo_gfx_decrypt(0x05);		break;
			case INIT_pnyaa:	kof2000_neogeo_gfx_decrypt(0x2e);	break;
			case INIT_preisle2:kof99_neogeo_gfx_decrypt(0x9f);		break;
			case INIT_ganryu:	kof99_neogeo_gfx_decrypt(0x07);		break;
			case INIT_bangbead:kof99_neogeo_gfx_decrypt(0xf8);		break;
			case INIT_mslug4:	kof2000_neogeo_gfx_decrypt(0x31);	break;
			case INIT_rotd:	kof2000_neogeo_gfx_decrypt(0x3f);	break;
			case INIT_matrim:	kof2000_neogeo_gfx_decrypt(0x6a);	break;
			case INIT_jockeygp:kof2000_neogeo_gfx_decrypt(0xac);	break;

			// Jamma PCB

			case INIT_ms5pcb:
				svcpcb_gfx_decrypt();
				kof2000_neogeo_gfx_decrypt(0x19);
				svcpcb_s1data_decrypt();
				break;

			case INIT_svcpcb:
				svcpcb_gfx_decrypt();
				kof2000_neogeo_gfx_decrypt(0x57);
				svcpcb_s1data_decrypt();
				break;

			case INIT_kf2k3pcb:
				kf2k3pcb_gfx_decrypt();
				kof2000_neogeo_gfx_decrypt(0x9d);
				kf2k3pcb_decrypt_s1data();
				break;

			// bootleg

			case INIT_kof97pla:
				neogeo_bootleg_sx_decrypt(1);
				break;

			case INIT_garoubl:
				neogeo_bootleg_sx_decrypt(2);
				neogeo_bootleg_cx_decrypt();
				break;

			case INIT_kf2k1pls:
			case INIT_kf2k1pa:
				cmc50_neogeo_gfx_decrypt(0x1e);
				break;

			case INIT_kof2002b:
				kof2002b_cx_decrypt();
				kof2002b_sx_decrypt();
				break;

			case INIT_kf2k2pls:
				cmc50_neogeo_gfx_decrypt(0xec);
				break;

			case INIT_kf2k2plc:
			case INIT_kf2k2mp:
				cmc50_neogeo_gfx_decrypt(0xec);
				neogeo_bootleg_sx_decrypt(2);
				break;

			case INIT_kf2k2mp2:
				cmc50_neogeo_gfx_decrypt(0xec);
				neogeo_bootleg_sx_decrypt(1);
				break;

			case INIT_kf2k4pls:
				neogeo_bootleg_sx_decrypt(1);
				break;

			case INIT_mslug5b:
				kof2000_neogeo_gfx_decrypt(0x19);
				break;

			case INIT_ms5plus:
				cmc50_neogeo_gfx_decrypt(0x19);
				neogeo_bootleg_sx_decrypt(1);
				break;

			case INIT_svcboot:
			case INIT_svcplusa:
				svcboot_cx_decrypt();
				break;

			case INIT_svcplus:
				svcboot_cx_decrypt();
				neogeo_bootleg_sx_decrypt(1);
				break;

			case INIT_svcsplus:
				svcboot_cx_decrypt();
				neogeo_bootleg_sx_decrypt(2);
				break;

			case INIT_kf2k3bl:
			case INIT_kf2k3pl:
				cmc50_neogeo_gfx_decrypt(0x9d);
				neogeo_bootleg_sx_decrypt(1);
				break;

			case INIT_kf2k3upl:
				cmc50_neogeo_gfx_decrypt(0x9d);
				neogeo_bootleg_sx_decrypt(2);
				break;

			case INIT_kog:
				neogeo_bootleg_cx_decrypt();
				neogeo_bootleg_sx_decrypt(1);
				break;

			case INIT_kof97oro:
				neogeo_bootleg_cx_decrypt();
				neogeo_bootleg_sx_decrypt(1);
				break;

			case INIT_cthd2003:
			case INIT_cthd2k3a:
			case INIT_ct2k3sa:
				cthd2003_cx_decrypt();
				break;

			case INIT_ct2k3sp:
				cthd2003_cx_decrypt();
				ct2k3sp_sx_decrypt();
				break;

			case INIT_samsho5b:
				samsho5b_cx_decrypt();
				neogeo_bootleg_sx_decrypt(1);
				break;

			case INIT_lans2004:
				neogeo_bootleg_cx_decrypt();
				neogeo_bootleg_sx_decrypt(1);
				break;

			case INIT_mslug3b6:
				cmc42_neogeo_gfx_decrypt(0xad);
				neogeo_bootleg_sx_decrypt(2);
				break;

			case INIT_matrimbl:
				cthd2003_cx_decrypt();
				neogeo_sfix_decrypt();
				break;

			default: goto error;
			}

			neogeo_decode_spr(memory_region_gfx3, memory_length_gfx3, gfx_pen_usage[TILE_SPR]);
		}
		else
		{
			return 0;
		}
	}
	else
	{
		if (load_rom_gfx3())
		{
			neogeo_decode_spr(memory_region_gfx3, memory_length_gfx3, gfx_pen_usage[TILE_SPR]);
		}
		else
		{
			return 0;
		}
	}

	return 1;

error:
	return 0;
}



static int create_raw_cache(char *game_name)
{
	int fp;
	char version[8];
	char fname[PATH_MAX];

	sprintf(version, "MVS_V%d%d\0", VERSION_MAJOR, VERSION_MINOR);

	change_directory("processed");
	fputs(ROMCNV_TEXT(MVS_CREATE_PROCESSED_ASSET), stdout);
	sprintf(fname, "%s_cache", game_name);
	if (chdir(fname) != 0)
	{
		if (mkdir(fname, 0777) != 0)
		{
			fputs(ROMCNV_TEXT(ERROR_CREATE_FOLDER), stdout);
			change_directory(launchDir);
			return 0;
		}
		change_directory(fname);
	}

	fp = open("cache_info", O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fp < 0) goto error;
	write(fp, version, 8);
	write(fp, gfx_pen_usage[TILE_SPR], gfx_total_elements[TILE_SPR]);
	close(fp);

	if (convert_crom)
	{
		fp = open("crom", O_WRONLY|O_CREAT|O_TRUNC, 0644);
		if (fp < 0) goto error;
		write(fp, memory_region_gfx3, memory_length_gfx3);
		close(fp);
	}
	if (convert_srom && encrypt_gfx2)
	{
		fp = open("srom", O_WRONLY|O_CREAT|O_TRUNC, 0644);
		if (fp < 0) goto error;
		write(fp, memory_region_gfx2, memory_length_gfx2);
		close(fp);
	}
	if (convert_vrom && (encrypt_snd1 || disable_sound))
	{
		fp = open("vrom", O_WRONLY|O_CREAT|O_TRUNC, 0644);
		if (fp < 0) goto error;
		write(fp, memory_region_sound1, memory_length_sound1);
		close(fp);
	}

	change_directory("..");
	change_directory("..");
	return 1;

error:
	remove("cache_info");
	if (convert_crom)
	{
		remove("crom");
	}
	if (convert_srom && encrypt_gfx2)
	{
		remove("srom");
	}
	if (convert_vrom && (encrypt_snd1 || disable_sound))
	{
		remove("vrom");
	}

	change_directory("..");

	sprintf(fname, "cache_%s", game_name);
	rmdir(fname);
	fputs(ROMCNV_TEXT(ERROR_CREATE_FILE), stdout);
	change_directory("..");
	return 0;
}


static int create_zip_cache(char *game_name)
{
	zip_writer_t writer = {0};
	zip_writer_segment_t cache_info[2];
	uint32_t block, num_blocks;
	char version[8], zipname[PATH_MAX];
	int res = 0;

	sprintf(version, "MVS_V%d%d\0", VERSION_MAJOR, VERSION_MINOR);

	change_directory("processed");

	sprintf(zipname, "%s%cprocessed%c%s_cache.zip", launchDir, delimiter, delimiter, game_name);
	remove(zipname);

	printf(ROMCNV_TEXT(MVS_PROCESSED_ASSET_ZIP), delimiter, game_name);
	fputs(ROMCNV_TEXT(MVS_CREATE_PROCESSED_ASSET), stdout);

	if (!zip_writer_open(&writer, zipname))
	{
		printf(ROMCNV_TEXT(MVS_ERROR_CREATE_ZIP), delimiter, game_name);
		goto error;
	}

	printf(ROMCNV_TEXT(MVS_COMPRESS_ZIP), delimiter, game_name);

	/* Write crom blocks */
	if (convert_crom)
	{
		num_blocks = memory_length_gfx3 >> 16;

		for (block = 0; block < num_blocks; block++)
		{
			static const char cnv_table[16] =
			{
				'0','1','2','3','4','5','6','7',
				'8','9','a','b','c','d','e','f'
			};
			char fname[4];

			fname[0] = cnv_table[(block >> 8) & 0x0f];
			fname[1] = cnv_table[(block >> 4) & 0x0f];
			fname[2] = cnv_table[ block       & 0x0f];
			fname[3] = '\0';

			if (!zip_writer_add_mem(&writer, fname,
			                        &memory_region_gfx3[block << 16], 0x10000))
			{
				printf("ERROR: Could not write cache block %s.\n", fname);
				goto error;
			}
		}
	}

	/* Write srom */
	if (convert_srom && encrypt_gfx2)
	{
		if (!zip_writer_add_mem(&writer, "srom", memory_region_gfx2, memory_length_gfx2))
			goto error;
	}

	/* Write vrom */
	if (convert_vrom && (encrypt_snd1 || disable_sound))
	{
		if (!zip_writer_add_mem(&writer, "vrom", memory_region_sound1, memory_length_sound1))
			goto error;
	}

	/* Write cache_info (version + pen_usage) without staging a combined buffer. */
	cache_info[0].data = version;
	cache_info[0].size = 8;
	cache_info[1].data = gfx_pen_usage[TILE_SPR];
	cache_info[1].size = gfx_total_elements[TILE_SPR];
	if (!zip_writer_add_segments(&writer, "cache_info", cache_info, 2))
		goto error;

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
	printf(ROMCNV_TEXT(MVS_BANNER), VERSION_STR);
	printf("----------------------------------------------\n\n");

	psp2k = 0;
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
			else if (!strcasecmp(argv[i], "-slim"))
			{
				psp2k = 1;
			}
			else if (!strcasecmp(argv[i], "-zip"))
			{
				zip = 1;
			}
			else if (strchr(argv[i], DELIMITER) != NULL)
			{
				path_found = i;
			}
		}
	}

	if (!path_found)
	{
		printf("usage: romcnv_mvs fullpath%cgamename.zip [-zip] [-lang en|zh-Hans]\n", DELIMITER);
		printf("  or   romcnv_mvs fullpath -all [-zip] [-lang en|zh-Hans]\n\n", DELIMITER);
		return 0;
	}

	if (chdir("processed") != 0)
	{
		if (mkdir("processed", 0777) != 0)
		{
			fputs(ROMCNV_TEXT(MVS_ERROR_CREATE_PROCESSED_DIR), stdout);
			goto error;
		}
	}
	else change_directory("..");

	getcwd(launchDir, PATH_MAX);
	strcat(launchDir, "/");

	snprintf(path, sizeof(path), "%sgame_metadata.mvs", launchDir);
	{
		game_metadata_error_t metadata_error = game_metadata_load(
			&mvs_game_metadata, path, GAME_METADATA_CORE_MVS);
		if (metadata_error != GAME_METADATA_OK)
		{
			printf("ERROR: Could not load game_metadata.mvs: %s\n",
				game_metadata_error_string(metadata_error));
			res = 0;
			goto error;
		}
	}

	if (all)
	{
		int total_games;

		strcpy(zip_dir, argv[path_found]);
		strcpy(game_dir, zip_dir);
		strcat(game_dir, "/");

		total_games = build_game_list();

		for (i = 0; i < total_games; i++)
		{
			int convert_result;

			res = 1;

			strcpy(game_name, game_names[i]);
			printf("\n-------------------------------------------\n");
			printf("  ROM set: %s\n", game_name);
			printf("-------------------------------------------\n\n");

			change_directory(launchDir);
			convert_result = convert_rom(game_name);
			if (convert_result == 0)
			{
				fputs(ROMCNV_TEXT(SKIP), stdout);
			}
			else if (convert_result == 1)
			{
				if (zip ? create_zip_cache(game_name) : create_raw_cache(game_name))
				{
					fputs(ROMCNV_TEXT(DONE), stdout);
				}
			}
			free_memory();
		}
		fputs(ROMCNV_TEXT(COMPLETE), stdout);
		fputs(ROMCNV_TEXT(MVS_COPY_ALL), stdout);
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
		printf(ROMCNV_TEXT(MVS_FILE_NAME), game_name);
		if ((p = strrchr(game_name, '.')) == NULL)
		{
			fputs(ROMCNV_TEXT(ERROR_INVALID_PATH), stdout);
			goto error;
		}
		*p = '\0';
		printf(ROMCNV_TEXT(MVS_PROCESSED_FOLDER_NAME), delimiter, game_name);

		change_directory(launchDir);
		{
			int convert_result = convert_rom(game_name);

			if (convert_result == 0)
			{
				res = 0;
			}
			else if (convert_result == 1)
			{
				res = zip ? create_zip_cache(game_name) : create_raw_cache(game_name);
			}
			else
			{
				res = 1;
			}

			if (res && convert_result == 1)
			{
				fputs(ROMCNV_TEXT(COMPLETE), stdout);
				if (zip)
					printf(ROMCNV_TEXT(MVS_COPY_ZIP), delimiter, game_name);
				else
					printf(ROMCNV_TEXT(MVS_COPY_FOLDER), delimiter, game_name);
			}
		}
		free_memory();
	}

error:
	game_metadata_unload(&mvs_game_metadata);
	return res;
}
