/******************************************************************************

	loadrom.c

	ROM Image File Load Functions

******************************************************************************/

#include <fcntl.h>
#include <limits.h>
#include <sys/unistd.h>
#include "emumain.h"

void swab(const void *restrict src, void *restrict dest, ssize_t nbytes);

#if (EMU_SYSTEM != NCDZ)

/******************************************************************************
	Local Variables
******************************************************************************/

static int64_t rom_fd = -1;

#if defined(PS2) && defined(GUI)
#define ROM_LOAD_PROGRESS_MIN_SIZE (128 * 1024)
#define ROM_LOAD_PROGRESS_STEPS 4

typedef struct
{
	size_t step;
	size_t next;
	unsigned percent;
} rom_load_progress_t;

static void init_rom_load_progress(rom_load_progress_t *progress, size_t total)
{
	if (total < ROM_LOAD_PROGRESS_MIN_SIZE)
	{
		progress->step = 0;
		progress->next = 0;
		progress->percent = 0;
		return;
	}

	progress->step = (total + ROM_LOAD_PROGRESS_STEPS - 1) / ROM_LOAD_PROGRESS_STEPS;
	progress->next = progress->step;
	progress->percent = 100 / ROM_LOAD_PROGRESS_STEPS;
}

static void report_rom_load_progress(rom_load_progress_t *progress, size_t current)
{
	if (progress->step == 0 || current < progress->next)
		return;

	msg_printf("  %u%%\r", progress->percent);
	progress->next += progress->step;
	progress->percent += 100 / ROM_LOAD_PROGRESS_STEPS;
	if (progress->percent > 100)
		progress->percent = 100;
}

static void file_read_with_progress(uint8_t *buf, size_t length)
{
	size_t offset = 0;
	size_t chunk_size = length;
	rom_load_progress_t progress;

	init_rom_load_progress(&progress, length);
	if (progress.step != 0)
		chunk_size = progress.step;

	while (offset < length)
	{
		size_t chunk = length - offset;

		if (chunk > chunk_size)
			chunk = chunk_size;

		file_read(buf + offset, chunk);
		offset += chunk;
		report_rom_load_progress(&progress, offset);
	}
}
#endif


/******************************************************************************
	ROM File Reading
******************************************************************************/

/*--------------------------------------------------------
	Search and Open File from ZIP File
--------------------------------------------------------*/

int64_t file_open(const char *fname1, const char *fname2, const uint32_t crc, char *fname)
{
	int i, found = 0;
	struct zip_find_t file;
	char path[PATH_MAX];

	for (i = 0; i < 3; i++)
	{
		switch (i)
		{
		case 0: sprintf(path, "%s/%s.zip", game_dir, fname1); break;
		case 1: sprintf(path, "%s/%s.zip", game_dir, fname2); break;
		case 2: sprintf(path, "%sroms/%s.zip", launchDir, fname2); break;
		}

		if (zip_open(path) != -1)
		{
			if (zip_findfirst(&file))
			{
				if (file.crc32 == crc)
				{
					found = 1;
				}
				else
				{
					if (!found)
					{
						while (zip_findnext(&file))
						{
							if (file.crc32 == crc)
							{
								found = 1;
								break;
							}
						}
					}
				}
			}

			if (!found)
			{
				if (fname)
				{
					int64_t fd;

					if ((fd = zopen(fname)) != -1)
					{
						zclose(fd);
						found = 2;
					}
				}
				zip_close();
			}
		}

		if (found || fname2 == NULL) break;
	}

	if (found == 1)
	{
		if (fname) strcpy(fname, file.name);
		rom_fd = zopen(file.name);
		return rom_fd;
	}
	else if (found == 2)
	{
		return -2;	// CRC error
	}

	return -1;	// not found
}


/*--------------------------------------------------------
	Close File
--------------------------------------------------------*/

void file_close(void)
{
	if (rom_fd != -1)
	{
		zclose(rom_fd);
		zip_close();
		rom_fd = -1;
	}
}


/*--------------------------------------------------------
	Read Specified Bytes from File
--------------------------------------------------------*/

size_t file_read(void *buf, size_t length)
{
	if (rom_fd != -1)
		return zread(rom_fd, buf, length);
	return -1;
}


/*--------------------------------------------------------
	Read 1 Character from File
--------------------------------------------------------*/

int file_getc(void)
{
	if (rom_fd != -1)
		return zgetc(rom_fd);
	return -1;
}


/*--------------------------------------------------------
	Open Cache File
--------------------------------------------------------*/

#if USE_CACHE && (EMU_SYSTEM == MVS)
int cachefile_open(int type)
{
	int32_t fd = -1;
	char path[PATH_MAX];

	switch (type)
	{
	case CACHE_INFO:
		if (use_parent_crom && use_parent_srom && use_parent_vrom)
		{
			sprintf(path, "%s/%s_cache/cache_info", cache_dir, parent_name);
			fd = open(path, O_RDONLY, 0777);
		}
		else
		{
			sprintf(path, "%s/%s_cache/cache_info", cache_dir, game_name);
			fd = open(path, O_RDONLY, 0777);
		}
		break;

	case CACHE_CROM:
		if (use_parent_crom)
		{
			sprintf(path, "%s/%s_cache/crom", cache_dir, parent_name);
			fd = open(path, O_RDONLY, 0777);
		}
		if (fd < 0)
		{
			sprintf(path, "%s/%s_cache/crom", cache_dir, game_name);
			fd = open(path, O_RDONLY, 0777);
		}
		break;

	case CACHE_SROM:
		if (use_parent_srom)
		{
			sprintf(path, "%s/%s_cache/srom", cache_dir, parent_name);
			fd = open(path, O_RDONLY, 0777);
		}
		if (fd < 0)
		{
			sprintf(path, "%s/%s_cache/srom", cache_dir, game_name);
			fd = open(path, O_RDONLY, 0777);
		}
		break;

	case CACHE_VROM:
		if (use_parent_vrom)
		{
			sprintf(path, "%s/%s_cache/vrom", cache_dir, parent_name);
			fd = open(path, O_RDONLY, 0777);
		}
		if (fd < 0)
		{
			sprintf(path, "%s/%s_cache/vrom", cache_dir, game_name);
			fd = open(path, O_RDONLY, 0777);
		}
		break;
	}

	return fd;
}


int64_t cachefile_zopen(int type, const char *name)
{
	int use_parent = 0;
	int64_t fd;
	char path[PATH_MAX];

	switch (type)
	{
	case CACHE_CROM: use_parent = use_parent_crom; break;
	case CACHE_SROM: use_parent = use_parent_srom; break;
	case CACHE_VROM: use_parent = use_parent_vrom; break;
	default: break;
	}

	if (use_parent && parent_name[0])
	{
		sprintf(path, "%s/%s_cache.zip", cache_dir, parent_name);
		if (zip_open(path) != -1)
		{
			fd = zopen(name);
			if (fd != -1)
				return fd;
			zip_close();
		}
	}

	sprintf(path, "%s/%s_cache.zip", cache_dir, game_name);
	if (zip_open(path) == -1)
		return -1;

	fd = zopen(name);
	if (fd == -1)
		zip_close();

	return fd;
}
#endif


/*--------------------------------------------------------
	Load ROM
--------------------------------------------------------*/

int rom_load(struct rom_t *rom, uint8_t *mem, int idx, int max)
{
	uint32_t offset, length;

_continue:
	offset = rom[idx].offset;

	if (rom[idx].skip == 0)
	{
	#if defined(PS2) && defined(GUI)
		file_read_with_progress(&mem[offset], rom[idx].length);
	#else
		file_read(&mem[offset], rom[idx].length);
	#endif

		if (rom[idx].type == ROM_WORDSWAP)
			swab(&mem[offset], &mem[offset], rom[idx].length);
	}
	else
	{
		int c;
		int skip = rom[idx].skip + rom[idx].group;
#if defined(PS2) && defined(GUI)
		rom_load_progress_t progress;
		init_rom_load_progress(&progress, rom[idx].length);
#endif

		length = 0;

		if (rom[idx].group == 1)
		{
			if (rom[idx].type == ROM_WORDSWAP)
				offset ^= 1;

			while (length < rom[idx].length)
			{
				if ((c = file_getc()) == EOF) break;
				mem[offset] = c;
				offset += skip;
				length++;
#if defined(PS2) && defined(GUI)
				report_rom_load_progress(&progress, length);
#endif
			}
		}
		else
		{
			while (length < rom[idx].length)
			{
				if ((c = file_getc()) == EOF) break;
				mem[offset + 0] = c;
				if ((c = file_getc()) == EOF) break;
				mem[offset + 1] = c;
				offset += skip;
				length += 2;
#if defined(PS2) && defined(GUI)
				report_rom_load_progress(&progress, length);
#endif
			}
		}
	}

	if (++idx != max)
	{
		if (rom[idx].type == ROM_CONTINUE)
		{
			goto _continue;
		}
	}

	return idx;
}

#endif /* EMU_SYSTEM */


/******************************************************************************
	Display Error Messages
******************************************************************************/

/*------------------------------------------------------
	Display Memory Allocation Error Message
------------------------------------------------------*/

void error_memory(const char *mem_name)
{
	zip_close();
	msg_printf(TEXT(COULD_NOT_ALLOCATE_x_MEMORY), mem_name);
	msg_printf(TEXT(PRESS_ANY_BUTTON2));
	pad_wait_press(PAD_WAIT_INFINITY);
	Loop = LOOP_BROWSER;
}


/*------------------------------------------------------
	Display CRC Error Message
------------------------------------------------------*/

void error_crc(const char *rom_name)
{
	zip_close();
	msg_printf(TEXT(CRC32_NOT_CORRECT_x), rom_name);
	msg_printf(TEXT(PRESS_ANY_BUTTON2));
	pad_wait_press(PAD_WAIT_INFINITY);
	Loop = LOOP_BROWSER;
}


/*------------------------------------------------------
	Display ROM File Error Message
------------------------------------------------------*/

void error_file(const char *rom_name)
{
	zip_close();
	msg_printf(TEXT(FILE_NOT_FOUND_x), rom_name);
	msg_printf(TEXT(PRESS_ANY_BUTTON2));
	pad_wait_press(PAD_WAIT_INFINITY);
	Loop = LOOP_BROWSER;
}
