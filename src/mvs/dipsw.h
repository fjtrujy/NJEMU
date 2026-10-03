/******************************************************************************

	dipsw.h

	MVS DIP Switch Handling

******************************************************************************/

#ifndef MVS_DIP_SWITCH_H
#define MVS_DIP_SWITCH_H

#include "common/dip_menu.h"

extern int neogeo_hard_dipsw;

dipswitch_t *load_dipswitch(void);
void save_dipswitch(void);

#endif /* MVS_DIP_SWITCH_H */
