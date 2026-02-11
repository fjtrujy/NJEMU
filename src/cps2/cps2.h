/******************************************************************************

	cps2.h

	CPS2 Emulation Core

******************************************************************************/

#ifndef CPS2_H
#define CPS2_H

#include "emumain.h"
#include "cpu/m68000/m68000.h"
#include "cpu/z80/z80.h"
#include "sound/sndintrf.h"
#include "sound/qsound.h"
#include "timer.h"
#include "driver.h"
#include "eeprom.h"
#include "inptport.h"
#include "memintrf.h"
#include "sprite.h"
#include "vidhrdw.h"

enum TEXTURE_LAYER_INDEX {
	TEXTURE_LAYER_OBJECT,
	TEXTURE_LAYER_SCROLL1,
	TEXTURE_LAYER_SCROLL2,
	TEXTURE_LAYER_SCROLL3,
	TEXTURE_LAYER_COUNT
};

void cps2_main(void);

#endif /* CPS2_H */
