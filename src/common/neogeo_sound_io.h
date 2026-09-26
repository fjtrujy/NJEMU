#ifndef COMMON_NEOGEO_SOUND_IO_H
#define COMMON_NEOGEO_SOUND_IO_H

#include <stdint.h>

/* Sound-side callbacks shared by the MVS and NCDZ CPU/audio wrappers. The
 * selected Neo Geo target provides the implementation in its driver.c. */
uint8_t neogeo_z80_port_r(uint16_t port);
void neogeo_z80_port_w(uint16_t port, uint8_t value);
void neogeo_sound_irq(int irq);

#endif /* COMMON_NEOGEO_SOUND_IO_H */
