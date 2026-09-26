/*****************************************************************************

	common.c

******************************************************************************/

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

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
extern char delimiter;

extern char game_dir[PATH_MAX];
extern char zip_dir[PATH_MAX];
extern char launchDir[PATH_MAX];

extern char game_name[16];
extern char parent_name[16];
extern char cache_name[16];

void error_memory(const char *mem_name);
void error_file(const char *rom_name);
void error_crc(const char *rom_name);
void error_rom(const char *rom_name);

typedef enum rom_file_open_result_t
{
	ROM_FILE_OPEN_CRC_MISMATCH = -2,
	ROM_FILE_OPEN_NOT_FOUND = -1,
	ROM_FILE_OPEN_OK = 0
} rom_file_open_result_t;

rom_file_open_result_t file_open(const char *fname1, const char *fname2, const uint32_t crc, char *fname);
void file_close(void);
int file_read(void *buf, size_t length);
int file_getc(void);

int rom_load(struct rom_t *rom, uint8_t *mem, int idx, int max);

int str_cmp(const char *s1, const char *s2);
void check_byte_order(void);
