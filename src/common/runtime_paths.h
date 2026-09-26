#ifndef COMMON_RUNTIME_PATHS_H
#define COMMON_RUNTIME_PATHS_H

#include <limits.h>

/* Runtime paths and selected-game names shared by startup/platform glue and
 * common services.  Keeping these declarations out of emumain.h prevents a
 * platform backend from inheriting the whole emulator/UI dependency graph. */
extern char launchDir[PATH_MAX];
extern char screenshotDir[PATH_MAX];

extern char game_name[16];
extern char parent_name[16];
extern char game_dir[PATH_MAX];

extern char cache_parent_name[16];
extern char cache_dir[PATH_MAX];

#endif /* COMMON_RUNTIME_PATHS_H */
