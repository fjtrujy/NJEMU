/***************************************************************************

	cps1.h

	CPS1 Emulation Core

***************************************************************************/

#ifndef CPS1_H
#define CPS1_H

#include "emumain.h"
#include "cpu/m68000/m68000.h"
#include "cpu/z80/z80.h"
#include "sound/sndintrf.h"
#include "sound/2151intf.h"
#include "sound/qsound.h"
#include "timer.h"
#include "dipsw.h"
#include "driver.h"
#include "eeprom.h"
#include "inptport.h"
#include "memintrf.h"
#include "sprite.h"
#include "vidhrdw.h"

enum TEXTURE_LAYER_INDEX {
	TEXTURE_LAYER_SCROLLH,
	TEXTURE_LAYER_OBJECT,
	TEXTURE_LAYER_SCROLL1,
	TEXTURE_LAYER_SCROLL2,
	TEXTURE_LAYER_SCROLL3,
	TEXTURE_LAYER_COUNT
};

void cps1_main(void);

#endif /* CPS1_H */
