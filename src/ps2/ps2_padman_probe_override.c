#include <ps2_irx_module.h>

/* The normal ps2_drivers descriptor is archived, so this explicit symbol
 * replaces only PADMAN in PS2_IOP_PADMAN_PROBE builds. All other modules
 * continue to come from the normal embedded ps2_drivers library. */
extern unsigned char ps2_padman_probe_irx[];
extern unsigned int size_ps2_padman_probe_irx;

const struct ps2_irx_module ps2_irx_padman = {
    17u,
    ps2_padman_probe_irx,
    &size_ps2_padman_probe_irx
};
