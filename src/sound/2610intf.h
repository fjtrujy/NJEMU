/***************************************************************************

	2610intf.c

	YM2610 interface

***************************************************************************/

#ifndef _2610INTF_H
#define _2610INTF_H

#include "include/memory.h"
#include "ym2610.h"

#define YM2610UpdateRequest()

void YM2610_sh_start(void);
void YM2610_sh_stop(void);
void YM2610_sh_reset(void);
#if (EMU_SYSTEM == MVS)
bool YM2610_restore_from_pcm_window_context(const ym2610_context_t *source,
	const uint64_t timer_remaining[2], const uint8_t timer_enabled[2]);
#endif
void timer_callback_2610(int param);

READ8_HANDLER( YM2610_status_port_A_r );
READ8_HANDLER( YM2610_status_port_B_r );
READ8_HANDLER( YM2610_read_port_r );

WRITE8_HANDLER( YM2610_control_port_A_w );
WRITE8_HANDLER( YM2610_control_port_B_w );
WRITE8_HANDLER( YM2610_data_port_A_w );
WRITE8_HANDLER( YM2610_data_port_B_w );

#endif /* _2610INTF_H */
