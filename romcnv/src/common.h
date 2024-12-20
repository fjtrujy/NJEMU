/*****************************************************************************

	common.c

******************************************************************************/

#ifndef COMMON_H
#define COMMON_H

#ifdef WIN32
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <conio.h>
#endif
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <malloc.h>
#include <zlib.h>
#include "zfile.h"

#ifndef MAX_PATH
#define MAX_PATH	512
#endif

#ifdef WIN32
#define DELIMITER	'\\'
#else
#define DELIMITER	'/'
#endif

struct rom_t
{
	uint32_t type;
	uint32_t offset;
	uint32_t length;
	uint32_t crc;
	int group;
	int skip;
	char name[32];
};


extern int lsb_first;
extern int rom_fd;
extern char delimiter;

extern char game_dir[MAX_PATH];
extern char zip_dir[MAX_PATH];
extern char launchDir[MAX_PATH];

extern char game_name[16];
extern char parent_name[16];
extern char cache_name[16];

void error_memory(const char *mem_name);
void error_file(const char *rom_name);
void error_rom(const char *rom_name);

int file_open(const char *fname1, const char *fname2, const uint32_t crc, char *fname);
void file_close(void);
int file_read(void *buf, size_t length);
int file_getc(void);

int rom_load(struct rom_t *rom, uint8_t *mem, int idx, int max);

int str_cmp(const char *s1, const char *s2);
void check_byte_order(void);


/*--------------------------------------------------------
	Windows�p
--------------------------------------------------------*/

#ifdef WIN32
extern int is_win9x;

void check_windows_version(void);
int file_dialog(HWND hwnd, LPCSTR filter, char *fname, uint32_t flags);
int folder_dialog(HWND hwnd, char *path);
void convert_delimiter(char *path);
#endif


#endif /* COMMON_H */
