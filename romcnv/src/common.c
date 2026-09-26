/*****************************************************************************

	common.c

******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "common.h"
#include "zip_reader.h"

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

char delimiter = '/';

static zip_reader_archive_t rom_archive;
static zip_reader_entry_t rom_entry;

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
	file_close();
#ifdef CHINESE
	printf("错误: 无法分配%s内存.\n", mem_name);
#else
	printf("ERROR: Could not allocate %s memory.\n", mem_name);
#endif
}


void error_file(const char *rom_name)
{
	file_close();
#ifdef CHINESE
	printf("错误: 没有找到文件. \"%s\"\n", rom_name);
#else
	printf("ERROR: File not found. \"%s\"\n", rom_name);
#endif
}


void error_crc(const char *rom_name)
{
	file_close();
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
	zip_reader_entry_close(&rom_entry);
	zip_reader_archive_close(&rom_archive);
}


/*--------------------------------------------------------
	Open ROM File
--------------------------------------------------------*/

rom_file_open_result_t file_open(const char *fname1, const char *fname2, const uint32_t crc, char *fname)
{
	const char *set_name[2] = { fname1, fname2 };
	rom_file_open_result_t result = ROM_FILE_OPEN_NOT_FOUND;
	zip_reader_entry_info_t info;
	char path[PATH_MAX];
	int i;

	file_close();

	for (i = 0; i < 2; ++i)
	{
		if (set_name[i] == NULL)
			break;

		sprintf(path, "%s%c%s.zip", zip_dir, delimiter, set_name[i]);
		if (!zip_reader_archive_open(&rom_archive, path))
			continue;

		if (zip_reader_archive_find_crc(&rom_archive, crc, &info))
		{
			if (fname != NULL)
				strcpy(fname, info.name);
			if (zip_reader_entry_open(&rom_archive, info.name, &rom_entry))
				return ROM_FILE_OPEN_OK;

			zip_reader_archive_close(&rom_archive);
			return ROM_FILE_OPEN_NOT_FOUND;
		}

		if (fname != NULL && zip_reader_archive_stat(&rom_archive, fname, &info))
			result = ROM_FILE_OPEN_CRC_MISMATCH;

		zip_reader_archive_close(&rom_archive);
	}

	return result;
}


/*--------------------------------------------------------
	Read Specified Number of Bytes from ROM File
--------------------------------------------------------*/

int file_read(void *buf, size_t length)
{
	if (zip_reader_entry_is_open(&rom_entry))
		return (int)zip_reader_entry_read(&rom_entry, buf, length);
	return -1;
}


/*--------------------------------------------------------
	Read 1 Byte from ROM File
--------------------------------------------------------*/

int file_getc(void)
{
	if (zip_reader_entry_is_open(&rom_entry))
		return zip_reader_entry_getc(&rom_entry);
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
