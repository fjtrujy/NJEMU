/******************************************************************************

	zfile.h

	ZIP File Operation Functions

******************************************************************************/

#ifndef ZFILE_H
#define ZFILE_H

#include <stdint.h>
#include <limits.h>
#include <stddef.h>
#include "emucfg.h"

int zip_open(const char *path);
void zip_close(void);

int64_t zopen(const char *filename);
size_t zread(int64_t fd, void *buf, size_t size);
int zclose(int64_t fd);
size_t zsize(int64_t fd);
#if (EMU_SYSTEM == NCDZ)
int zlength(const char *filename);
#endif

#endif /* ZFILE_H */
