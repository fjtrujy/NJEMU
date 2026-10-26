#ifndef NJEMU_PS2_HDD_LAUNCH_H
#define NJEMU_PS2_HDD_LAUNCH_H

#include <stdbool.h>

/* True only when an HDD partition and mounted PFS cwd were preserved from
 * the PS2 launch arguments before resetting the IOP. */
bool ps2_hdd_launch_active(void);

#endif
