/******************************************************************************

	filer.c

	PSP �t�@�C���u���E�U

******************************************************************************/

#ifndef PSP_FILEBROWSER_H
#define PSP_FILEBROWSER_H

#include <limits.h>

extern char startupDir[PATH_MAX];

int file_exist(const char *path);
#ifdef SAVE_STATE
void find_state_file(uint8_t *slot);
#endif

#endif /* PSP_FILEBROWSER_H */
