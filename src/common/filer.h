#ifndef FILER_H
#define FILER_H

#include <limits.h>
#include <stdint.h>

extern char startupDir[PATH_MAX];

char *find_file(char *pattern, char *path);
int file_exist(const char *path);
#ifdef SAVE_STATE
void find_state_file(uint8_t *slot);
#endif

#endif // FILER_H
