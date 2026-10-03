/******************************************************************************

	dipsw.h

	CPS1 DIP Switch Settings

******************************************************************************/

#ifndef CPS1_DIP_SWITCH_H
#define CPS1_DIP_SWITCH_H

#include "common/dip_menu.h"

dipswitch_t *load_dipswitch(int *sx);
void save_dipswitch(void);

#endif /* CPS1_DIP_SWITCH_H */
