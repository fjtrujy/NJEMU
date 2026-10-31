#ifndef NJEMU_PS2_LAUNCH_STORAGE_H
#define NJEMU_PS2_LAUNCH_STORAGE_H

#include <string.h>

typedef enum ps2_launch_storage {
	PS2_LAUNCH_STORAGE_DEFAULT = 0,
	PS2_LAUNCH_STORAGE_APA_PFS,
	PS2_LAUNCH_STORAGE_ATA_BDM,
} ps2_launch_storage_t;

/* The universal bootstrap supplies the transport marker because massN: is
 * shared by USB, MX4SIO and ATA/BDM. Never infer ATA from the cwd alone. */
static inline ps2_launch_storage_t ps2_launch_storage_detect(
	int argc, const char *const argv[], const char *cwd, const char *ata_marker)
{
	int i;

	if (ata_marker != NULL && argv != NULL) {
		for (i = 0; i < argc; ++i) {
			if (argv[i] != NULL && strcmp(argv[i], ata_marker) == 0)
				return PS2_LAUNCH_STORAGE_ATA_BDM;
		}
	}
	if (cwd != NULL &&
	    (strncmp(cwd, "pfs:/", 5) == 0 || strncmp(cwd, "pfs0:/", 6) == 0))
		return PS2_LAUNCH_STORAGE_APA_PFS;
	return PS2_LAUNCH_STORAGE_DEFAULT;
}

#endif
