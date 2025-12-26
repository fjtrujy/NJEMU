/*****************************************************************************

	common.c

******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "common.h"
#include "zfile.h"

void swab(const void *restrict src, void *restrict dest, ssize_t nbytes);

enum
{
	ROM_LOAD = 0,
	ROM_CONTINUE,
	ROM_WORDSWAP,
	MAP_MAX
};


/******************************************************************************
	Global Variables
******************************************************************************/

int lsb_first;

int rom_fd;
char delimiter = '/';

char game_dir[PATH_MAX];
char zip_dir[PATH_MAX];
char launchDir[PATH_MAX];

char game_name[16];
char parent_name[16];
char cache_name[16];


/******************************************************************************
	ROM Load Processing
******************************************************************************/

/*--------------------------------------------------------
	Display Error Message
--------------------------------------------------------*/

void error_memory(const char *mem_name)
{
	zip_close();
#ifdef CHINESE
	printf("错误: 无法分配%s内存.\n", mem_name);
#else
	printf("ERROR: Could not allocate %s memory.\n", mem_name);
#endif
}


void error_file(const char *rom_name)
{
	zip_close();
#ifdef CHINESE
	printf("错误: 没有找到文件. \"%s\"\n", rom_name);
#else
	printf("ERROR: File not found. \"%s\"\n", rom_name);
#endif
}


void error_crc(const char *rom_name)
{
	zip_close();
#ifdef CHINESE
	printf("错误: CRC32不正确. \"%s\"\n", rom_name);
#else
	printf("ERROR: File not found. \"%s\"\n", rom_name);
#endif
}


/*--------------------------------------------------------
	Close ROM File
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
	Open ROM File
--------------------------------------------------------*/

int file_open(const char *fname1, const char *fname2, const uint32_t crc, char *fname)
{
	int found = 0, res = -1;
	struct zip_find_t file;
	char path[PATH_MAX];

	file_close();

	sprintf(path, "%s%c%s.zip", zip_dir, delimiter, fname1);

	if (zip_open(path, "rb") != -1)
	{
		if (zip_findfirst(&file))
		{
			if (file.crc32 == crc)
			{
				found = 1;
			}
			else
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
		if (!found)
		{
			if ((rom_fd = zopen(fname)) != -1)
			{
				file_close();
				res = -2;
			}
			zip_close();
		}
	}

	if (!found && fname2 != NULL)
	{
		sprintf(path, "%s%c%s.zip", zip_dir, delimiter, fname2);

		if (zip_open(path, "rb") != -1)
		{
			if (zip_findfirst(&file))
			{
				if (file.crc32 == crc)
				{
					found = 2;
				}
				else
				{
					while (zip_findnext(&file))
					{
						if (file.crc32 == crc)
						{
							found = 2;
							break;
						}
					}
				}
			}
			if (!found)
			{
				if ((rom_fd = zopen(fname)) != -1)
				{
					file_close();
					res = -2;
				}
				zip_close();
			}
		}
	}

	if (found)
	{
		if (fname) strcpy(fname, file.name);
		rom_fd = zopen(file.name);
		return rom_fd;
	}

	return res;
}


/*--------------------------------------------------------
	Read Specified Number of Bytes from ROM File
--------------------------------------------------------*/

int file_read(void *buf, size_t length)
{
	if (rom_fd != -1)
		return zread(rom_fd, buf, length);
	return -1;
}


/*--------------------------------------------------------
	Read 1 Byte from ROM File
--------------------------------------------------------*/

int file_getc(void)
{
	if (rom_fd != -1)
		return zgetc(rom_fd);
	return -1;
}


/*--------------------------------------------------------
	Load ROM into Specified Memory Area
--------------------------------------------------------*/

int rom_load(struct rom_t *rom, uint8_t *mem, int idx, int max)
{
	int offset, length;

_continue:
	offset = rom[idx].offset;

	if (rom[idx].skip == 0)
	{
		file_read(&mem[offset], rom[idx].length);

		if (rom[idx].type == ROM_WORDSWAP)
			swab(&mem[offset], &mem[offset], rom[idx].length);
	}
	else
	{
		int c;
		int skip = rom[idx].skip + rom[idx].group;

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


/******************************************************************************
	Miscellaneous
******************************************************************************/

/*--------------------------------------------------------
	String Comparison
--------------------------------------------------------*/

int str_cmp(const char *s1, const char *s2)
{
	return strncasecmp(s1, s2, strlen(s2));
}


/*--------------------------------------------------------
	Check Byte Order
--------------------------------------------------------*/

void check_byte_order(void)
{
	int32_t temp = 0x12345678;
	char *p = (char *)&temp;

	if (*p == 0x78)
		lsb_first = 1;
	else
		lsb_first = 0;
}
