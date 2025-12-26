/******************************************************************************

	loadrom.h

	ROM Image File Load Functions

******************************************************************************/

#ifndef LOAD_ROM_H
#define LOAD_ROM_H

#include <stddef.h>
#include <stdint.h>

enum
{
	ROM_LOAD = 0,
	ROM_CONTINUE,
	ROM_WORDSWAP,
	MAP_MAX
};

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

#if (EMU_SYSTEM != NCDZ)
int64_t file_open(const char *fname1, const char *fname2, const uint32_t crc, char *fname);
void file_close(void);
size_t file_read(void *buf, size_t length);
int file_getc(void);
int rom_load(struct rom_t *rom, uint8_t *mem, int idx, int max);
#endif

#if USE_CACHE && (EMU_SYSTEM == MVS)
enum
{
	CACHE_INFO = 0,
	CACHE_CROM,
	CACHE_SROM,
	CACHE_VROM
};

int cachefile_open(int type);
#endif

void error_memory(const char *mem_name);
void error_crc(const char *rom_name);
void error_file(const char *rom_name);

#endif /* LOAD_ROM_H */
