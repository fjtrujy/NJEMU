#ifndef FILER_H
#define FILER_H

#include <limits.h>
#include <stdint.h>

extern char startupDir[PATH_MAX];

void file_browser(void);
void show_exit_screen(void);
void delete_files(const char *dirname, const char *pattern);

char *find_file(char *pattern, char *path);
#ifdef SAVE_STATE
void find_state_file(uint8_t *slot);
#endif

#endif // FILER_H
