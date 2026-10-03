/******************************************************************************

	dipsw.c

	CPS1 DIPスイッチ設定

******************************************************************************/

#include <limits.h>
#include <stdio.h>
#include "cps1.h"
#include "common/dip_metadata.h"
#include "common/emulator_options.h"
#include "common/path_utils.h"
#include "common/runtime_paths.h"
#include "common/ui_text_driver.h"








static dip_metadata_t dip_metadata;
static dipswitch_t *active_dipswitch;

static const char *dip_profile_name(int input_type)
{
	switch (input_type)
	{
	case INPTYPE_forgottn: return "forgottn";
	case INPTYPE_ghouls: return "ghouls";
	case INPTYPE_ghoulsu: return "ghoulsu";
	case INPTYPE_daimakai: return "daimakai";
	case INPTYPE_strider: return "strider";
	case INPTYPE_stridrua: return "stridrua";
	case INPTYPE_dynwar: return "dynwar";
	case INPTYPE_willow: return "willow";
	case INPTYPE_unsquad: return "unsquad";
	case INPTYPE_ffight: return "ffight";
	case INPTYPE_1941: return "1941";
	case INPTYPE_mercs: return "mercs";
	case INPTYPE_mtwins: return "mtwins";
	case INPTYPE_msword: return "msword";
	case INPTYPE_cawing: return "cawing";
	case INPTYPE_nemo: return "nemo";
	case INPTYPE_sf2: return "sf2";
	case INPTYPE_sf2j: return "sf2j";
	case INPTYPE_3wonders: return "3wonders";
	case INPTYPE_kod: return "kod";
	case INPTYPE_kodj: return "kodj";
	case INPTYPE_captcomm: return "captcomm";
	case INPTYPE_knights: return "knights";
	case INPTYPE_varth: return "varth";
	case INPTYPE_cworld2j: return "cworld2j";
	case INPTYPE_qad: return "qad";
	case INPTYPE_qadj: return "qadj";
	case INPTYPE_qtono2: return "qtono2";
	case INPTYPE_megaman: return "megaman";
	case INPTYPE_rockmanj: return "rockmanj";
	case INPTYPE_pnickj: return "pnickj";
#if !RELEASE
	case INPTYPE_wofhfh: return "wofhfh";
	case INPTYPE_punisherbz: return "punisherbz";
#endif
	default: return NULL;
	}
}

static dip_metadata_language_t dip_metadata_language(void)
{
	switch (ui_text_driver->getLanguage(ui_text_data))
	{
	case UI_LANG_JAPANESE: return DIP_METADATA_LANG_JAPANESE;
	case UI_LANG_CHINESE_SIMPLIFIED: return DIP_METADATA_LANG_CHINESE_SIMPLIFIED;
	case UI_LANG_CHINESE_TRADITIONAL: return DIP_METADATA_LANG_CHINESE_TRADITIONAL;
	default: return DIP_METADATA_LANG_ENGLISH;
	}
}

#define load_free_play				(dip++)->value = dip_load_bit(DIP_C, 2, 1);
#define load_freeze					(dip++)->value = dip_load_bit(DIP_C, 3, 1);
#define load_flip_screen			(dip++)->value = dip_load_bit(DIP_C, 4, 1);
#define load_demo_sounds			(dip++)->value = dip_load_bit(DIP_C, 5, 1);
#define load_allow_continue			(dip++)->value = dip_load_bit(DIP_C, 6, 1);
#define load_game_mode				(dip++)->value = dip_load_bit(DIP_C, 7, 1);

#define save_free_play				dip_save_bit(DIP_C, (dip++)->value, 2, 1);
#define save_freeze					dip_save_bit(DIP_C, (dip++)->value, 3, 1);
#define save_flip_screen			dip_save_bit(DIP_C, (dip++)->value, 4, 1);
#define save_demo_sounds			dip_save_bit(DIP_C, (dip++)->value, 5, 1);
#define save_allow_continue			dip_save_bit(DIP_C, (dip++)->value, 6, 1);
#define save_game_mode				dip_save_bit(DIP_C, (dip++)->value, 7, 1);

#define load_demo_sounds2			(dip++)->value = dip_load_bit(DIP_C, 5, 0);
#define load_allow_continue2		(dip++)->value = dip_load_bit(DIP_C, 6, 0);

#define save_demo_sounds2			dip_save_bit(DIP_C, (dip++)->value, 5, 0);
#define save_allow_continue2		dip_save_bit(DIP_C, (dip++)->value, 6, 0);


/*--------------------------------------
  共通 (bit)
--------------------------------------*/







static int dip_load_bit(int sw, int shift, int invert)
{
	int value = 0;

	if (invert)
		value = (cps1_dipswitch[sw] & (1 << shift)) ? 0 : 1;
	else
		value = (cps1_dipswitch[sw] & (1 << shift)) ? 1 : 0;

	return value;
}

static void dip_save_bit(int sw, int value, int shift, int invert)
{
	if (invert) value ^= 1;
	cps1_dipswitch[sw] &= ~(1 << shift);
	cps1_dipswitch[sw] |= value << shift;
}


/*--------------------------------------
  共通 (コイン type1)
--------------------------------------*/







static int dip_load_coin1a(void)
{
	switch (cps1_dipswitch[DIP_A] & 0x07)
	{
	case 0x00: return 0;
	case 0x01: return 1;
	case 0x02: return 2;
	case 0x03: return 7;
	case 0x04: return 6;
	case 0x05: return 5;
	case 0x06: return 4;
	case 0x07: return 3;
	}
	return 0;
}

static void dip_save_coin1a(int value)
{
	cps1_dipswitch[DIP_A] &= ~0x07;

	switch (value)
	{
	case 0: cps1_dipswitch[DIP_A] |= 0x00; break;
	case 1: cps1_dipswitch[DIP_A] |= 0x01; break;
	case 2: cps1_dipswitch[DIP_A] |= 0x02; break;
	case 3: cps1_dipswitch[DIP_A] |= 0x07; break;
	case 4: cps1_dipswitch[DIP_A] |= 0x06; break;
	case 5: cps1_dipswitch[DIP_A] |= 0x05; break;
	case 6: cps1_dipswitch[DIP_A] |= 0x04; break;
	case 7: cps1_dipswitch[DIP_A] |= 0x03; break;
	}
}

static int dip_load_coin1b(void)
{
	switch (cps1_dipswitch[DIP_A] & 0x38)
	{
	case 0x00: return 0;
	case 0x08: return 1;
	case 0x10: return 2;
	case 0x18: return 7;
	case 0x20: return 6;
	case 0x28: return 5;
	case 0x30: return 4;
	case 0x38: return 3;
	}
	return 0;
}

static void dip_save_coin1b(int value)
{
	cps1_dipswitch[DIP_A] &= ~0x38;

	switch (value)
	{
	case 0: cps1_dipswitch[DIP_A] |= 0x00; break;
	case 1: cps1_dipswitch[DIP_A] |= 0x08; break;
	case 2: cps1_dipswitch[DIP_A] |= 0x10; break;
	case 3: cps1_dipswitch[DIP_A] |= 0x38; break;
	case 4: cps1_dipswitch[DIP_A] |= 0x30; break;
	case 5: cps1_dipswitch[DIP_A] |= 0x28; break;
	case 6: cps1_dipswitch[DIP_A] |= 0x20; break;
	case 7: cps1_dipswitch[DIP_A] |= 0x18; break;
	}
}

/*--------------------------------------
  共通 (コイン type2)
--------------------------------------*/







static int dip_load_coin2a(void)
{
	switch (cps1_dipswitch[DIP_A] & 0x07)
	{
	case 0x00: return 3;
	case 0x01: return 0;
	case 0x02: return 1;
	case 0x03: return 2;
	case 0x04: return 7;
	case 0x05: return 6;
	case 0x06: return 5;
	case 0x07: return 4;
	}
	return 0;
}

static void dip_save_coin2a(int value)
{
	cps1_dipswitch[DIP_A] &= ~0x07;

	switch (value)
	{
	case 0: cps1_dipswitch[DIP_A] |= 0x01; break;
	case 1: cps1_dipswitch[DIP_A] |= 0x02; break;
	case 2: cps1_dipswitch[DIP_A] |= 0x03; break;
	case 3: cps1_dipswitch[DIP_A] |= 0x00; break;
	case 4: cps1_dipswitch[DIP_A] |= 0x07; break;
	case 5: cps1_dipswitch[DIP_A] |= 0x06; break;
	case 6: cps1_dipswitch[DIP_A] |= 0x05; break;
	case 7: cps1_dipswitch[DIP_A] |= 0x04; break;
	}
}

static int dip_load_coin2b(void)
{
	switch (cps1_dipswitch[DIP_A] & 0x38)
	{
	case 0x00: return 3;
	case 0x08: return 0;
	case 0x10: return 1;
	case 0x18: return 2;
	case 0x20: return 7;
	case 0x28: return 6;
	case 0x30: return 5;
	case 0x38: return 4;
	}
	return 0;
}

static void dip_save_coin2b(int value)
{
	cps1_dipswitch[DIP_A] &= ~0x38;

	switch (value)
	{
	case 0: cps1_dipswitch[DIP_A] |= 0x08; break;
	case 1: cps1_dipswitch[DIP_A] |= 0x10; break;
	case 2: cps1_dipswitch[DIP_A] |= 0x18; break;
	case 3: cps1_dipswitch[DIP_A] |= 0x00; break;
	case 4: cps1_dipswitch[DIP_A] |= 0x38; break;
	case 5: cps1_dipswitch[DIP_A] |= 0x30; break;
	case 6: cps1_dipswitch[DIP_A] |= 0x28; break;
	case 7: cps1_dipswitch[DIP_A] |= 0x20; break;
	}
}

/*--------------------------------------
  共通 (筐体)
--------------------------------------*/





static int dip_load_cabinet(void)
{
	switch (cps1_dipswitch[DIP_A] & 0xc0)
	{
	case 0x00: return 2;
	case 0x80: return 1;
	case 0xc0: return 0;
	}
	return 0;
}

static void dip_save_cabinet(int value)
{
	cps1_dipswitch[DIP_A] &= ~0xc0;

	switch (value)
	{
	case 0: cps1_dipswitch[DIP_A] |= 0xc0; break;
	case 1: cps1_dipswitch[DIP_A] |= 0x80; break;
	case 2: cps1_dipswitch[DIP_A] |= 0x00; break;
	}
}

/*--------------------------------------
  共通 (難易度)
--------------------------------------*/







static int dip_load_difficulty1(void)
{
	switch (cps1_dipswitch[DIP_B] & 0x07)
	{
	case 0x00: return 7;
	case 0x01: return 6;
	case 0x02: return 5;
	case 0x03: return 4;
	case 0x04: return 3;
	case 0x05: return 2;
	case 0x06: return 1;
	case 0x07: return 0;
	}
	return 0;
}

static void dip_save_difficulty1(int value)
{
	cps1_dipswitch[DIP_B] &= ~0x07;
	
	switch (value)
	{
	case 0: cps1_dipswitch[DIP_B] |= 0x07; break;
	case 1: cps1_dipswitch[DIP_B] |= 0x06; break;
	case 2: cps1_dipswitch[DIP_B] |= 0x05; break;
	case 3: cps1_dipswitch[DIP_B] |= 0x04; break;
	case 4: cps1_dipswitch[DIP_B] |= 0x03; break;
	case 5: cps1_dipswitch[DIP_B] |= 0x02; break;
	case 6: cps1_dipswitch[DIP_B] |= 0x01; break;
	case 7: cps1_dipswitch[DIP_B] |= 0x00; break;
	}
}

static int dip_load_difficulty2(void)
{
	switch (cps1_dipswitch[DIP_B] & 0x07)
	{
	case 0x00: return 7;
	case 0x01: return 6;
	case 0x02: return 5;
	case 0x03: return 4;
	case 0x04: return 0;
	case 0x05: return 1;
	case 0x06: return 2;
	case 0x07: return 3;
	}
	return 0;
}

static void dip_save_difficulty2(int value)
{
	cps1_dipswitch[DIP_B] &= ~0x07;

	switch (value)
	{
	case 0: cps1_dipswitch[DIP_B] |= 0x04; break;
	case 1: cps1_dipswitch[DIP_B] |= 0x05; break;
	case 2: cps1_dipswitch[DIP_B] |= 0x06; break;
	case 3: cps1_dipswitch[DIP_B] |= 0x07; break;
	case 4: cps1_dipswitch[DIP_B] |= 0x03; break;
	case 5: cps1_dipswitch[DIP_B] |= 0x02; break;
	case 6: cps1_dipswitch[DIP_B] |= 0x01; break;
	case 7: cps1_dipswitch[DIP_B] |= 0x00; break;
	}
}

/*--------------------------------------
  forgottn
--------------------------------------*/


static void dip_load_forgottn(int language)
{
	dipswitch_t *dip;
	switch (language)
	{
	case 1:  dip = active_dipswitch; break;
	case 2:  dip = active_dipswitch; break;
	case 3:  dip = active_dipswitch; break;
	default: dip = active_dipswitch; break;
	}

	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_coin1b();
	(dip++)->value = dip_load_bit(DIP_A, 6, 0);
	(dip++)->value = dip_load_bit(DIP_A, 7, 1);

	(dip++)->value = dip_load_difficulty1();
	(dip++)->value = dip_load_bit(DIP_B, 6, 1);
	(dip++)->value = dip_load_bit(DIP_B, 7, 1);
}

static void dip_save_forgottn(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1:  dip = active_dipswitch; break;
	case 2:  dip = active_dipswitch; break;
	case 3:  dip = active_dipswitch; break;
	default: dip = active_dipswitch; break;
	}

	dip_save_coin1a((dip++)->value);
	dip_save_coin1b((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 0);
	dip_save_bit(DIP_A, (dip++)->value, 7, 1);

	dip_save_difficulty1((dip++)->value);
	dip_save_bit(DIP_B, (dip++)->value, 6, 1);
	dip_save_bit(DIP_B, (dip++)->value, 7, 1);
}

/*--------------------------------------
  ghouls / ghoulsu / daimakai
--------------------------------------*/




static void dip_load_ghouls(int type, int language)
{
	dipswitch_t *dip;
	
	switch (language)
	{
	case 1:
		switch (type)
		{
		case 1:  dip = active_dipswitch; break;
		case 2:  dip = active_dipswitch; break;
		default: dip = active_dipswitch; break;
		}
	break;

	case 2:
		switch (type)
		{
		case 1:  dip = active_dipswitch; break;
		case 2:  dip = active_dipswitch; break;
		default: dip = active_dipswitch; break;
		}
	break;

	case 3:
		switch (type)
		{
		case 1:  dip = active_dipswitch; break;
		case 2:  dip = active_dipswitch; break;
		default: dip = active_dipswitch; break;
		}
	break;

	default:
		switch (type)
		{
		case 1:  dip = active_dipswitch; break;
		case 2:  dip = active_dipswitch; break;
		default: dip = active_dipswitch; break;
		}
	break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_coin1b();
	(dip++)->value = dip_load_cabinet();

	// DIP B
	(dip++)->value = dip_load_difficulty2();
	switch (cps1_dipswitch[DIP_B] & 0x30)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x10: (dip++)->value = 1; break;
	case 0x20: (dip++)->value = 0; break;
	case 0x30: (dip++)->value = 2; break;
	}
	if (type != 0) (dip++)->value = dip_load_bit(DIP_B, 7, 1);

	// DIP C
	if (type == 1)
	{
		switch (cps1_dipswitch[DIP_C] & 0x03)
		{
		case 0x00: (dip++)->value = 0; break;
		case 0x01: (dip++)->value = 3; break;
		case 0x02: (dip++)->value = 2; break;
		case 0x03: (dip++)->value = 1; break;
		}
	}
	else
	{
		switch (cps1_dipswitch[DIP_C] & 0x03)
		{
		case 0x00: (dip++)->value = 3; break;
		case 0x01: (dip++)->value = 2; break;
		case 0x02: (dip++)->value = 1; break;
		case 0x03: (dip++)->value = 0; break;
		}
	}
	load_flip_screen;
	load_demo_sounds2;
	load_allow_continue2;
	load_game_mode;
}

static void dip_save_ghouls(int type, int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1:
		switch (type)
		{
		case 1:  dip = active_dipswitch; break;
		case 2:  dip = active_dipswitch; break;
		default: dip = active_dipswitch; break;
		}
	break;

	case 2:
		switch (type)
		{
		case 1:  dip = active_dipswitch; break;
		case 2:  dip = active_dipswitch; break;
		default: dip = active_dipswitch; break;
		}
	break;

	case 3:
		switch (type)
		{
		case 1:  dip = active_dipswitch; break;
		case 2:  dip = active_dipswitch; break;
		default: dip = active_dipswitch; break;
		}
	break;

	default:
		switch (type)
		{
		case 1:  dip = active_dipswitch; break;
		case 2:  dip = active_dipswitch; break;
		default: dip = active_dipswitch; break;
		}
	break;
	}
	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_coin1b((dip++)->value);
	dip_save_cabinet((dip++)->value);

	// DIP B
	dip_save_difficulty2((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x30; cps1_dipswitch[DIP_B] |= 0x20; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x30; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x30; cps1_dipswitch[DIP_B] |= 0x30; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x30; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	if (type != 0) dip_save_bit(DIP_B, (dip++)->value, 7, 1);

	// DIP C
	if (type == 1)
	{
		switch ((dip++)->value)
		{
		case 0: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x00; break;
		case 1: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x03; break;
		case 2: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x02; break;
		case 3: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x01; break;
		}
	}
	else
	{
		switch ((dip++)->value)
		{
		case 0: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x03; break;
		case 1: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x02; break;
		case 2: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x01; break;
		case 3: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x00; break;
		}
	}
	save_flip_screen;
	save_demo_sounds2;
	save_allow_continue2;
	save_game_mode;
}

/*--------------------------------------
  srtider / stridrua
--------------------------------------*/






static void dip_load_strider(int type, int language)
{
	dipswitch_t *dip;
	switch (language)
	{
	case 1:
		{
		if (type)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	case 2:
		{
		if (type)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	case 3:
		{
		if (type)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	default:
		{
		if (type)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_coin1b();
	(dip++)->value = dip_load_cabinet();

	// DIP B
	(dip++)->value = dip_load_difficulty2();
	if (type == 1) (dip++)->value = dip_load_bit(DIP_B, 3, 1);
	switch (cps1_dipswitch[DIP_B] & 0x30)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x10: (dip++)->value = 2; break;
	case 0x20: (dip++)->value = 1; break;
	case 0x30: (dip++)->value = 0; break;
	}
	switch (cps1_dipswitch[DIP_B] & 0xc0)
	{
	case 0x00: (dip++)->value = 1; break;
	case 0x80: (dip++)->value = 2; break;
	case 0xc0: (dip++)->value = 0; break;
	}

	// DIP C
	switch (cps1_dipswitch[DIP_C] & 0x03)
	{
	case 0x00: (dip++)->value = 0; break;
	case 0x01: (dip++)->value = 3; break;
	case 0x02: (dip++)->value = 2; break;
	case 0x03: (dip++)->value = 1; break;
	}
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds2;
	load_allow_continue2;
	load_game_mode;
}

static void dip_save_strider(int type, int language)
{
	dipswitch_t *dip;
	switch (language)
	{
	case 1:
		{
		if (type)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	case 2:
		{
		if (type)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	case 3:
		{
		if (type)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	default:
		{
		if (type)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_coin1b((dip++)->value);
	dip_save_cabinet((dip++)->value);

	// DIP B
	dip_save_difficulty2((dip++)->value);
	if (type == 1) dip_save_bit(DIP_B, (dip++)->value, 3, 1);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x30; cps1_dipswitch[DIP_B] |= 0x30; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x30; cps1_dipswitch[DIP_B] |= 0x20; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x30; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x30; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0xc0; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0x00; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0x80; break;
	}

	// DIP C
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x00; break;
	case 1: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x03; break;
	case 2: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x02; break;
	case 3: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x01; break;
	}
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds2;
	save_allow_continue2;
	save_game_mode;
}

/*--------------------------------------
  dynwar
--------------------------------------*/


static void dip_load_dynwar(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1:dip = active_dipswitch;break;
	case 2:dip = active_dipswitch;break;
	case 3:dip = active_dipswitch;break;
	default:dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin2a();
	(dip++)->value = dip_load_coin2b();
	(dip++)->value = dip_load_bit(DIP_A, 7, 1);

	// DIP B
	(dip++)->value = dip_load_difficulty2();

	// DIP C
	(dip++)->value = dip_load_bit(DIP_C, 0, 1);
	(dip++)->value = dip_load_bit(DIP_C, 1, 1);
	load_flip_screen;
	load_demo_sounds2;
	load_allow_continue2;
	load_game_mode;
}

static void dip_save_dynwar(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1:dip = active_dipswitch;break;
	case 2:dip = active_dipswitch;break;
	case 3:dip = active_dipswitch;break;
	default:dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin2a((dip++)->value);
	dip_save_coin2b((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 7, 1);

	// DIP B
	dip_save_difficulty2((dip++)->value);

	// DIP C
	dip_save_bit(DIP_C, (dip++)->value, 0, 1);
	dip_save_bit(DIP_C, (dip++)->value, 1, 1);
	save_flip_screen;
	save_demo_sounds2;
	save_allow_continue2;
	save_game_mode;
}

/*--------------------------------------
  willow
--------------------------------------*/





static void dip_load_willow(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin2a();
	(dip++)->value = dip_load_coin2b();
	(dip++)->value = dip_load_cabinet();

	// DIP B
	(dip++)->value = dip_load_difficulty2();
	switch (cps1_dipswitch[DIP_B] & 0x18)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x08: (dip++)->value = 2; break;
	case 0x10: (dip++)->value = 0; break;
	case 0x18: (dip++)->value = 1; break;
	}
	(dip++)->value = dip_load_bit(DIP_B, 7, 1);

	// DIP C
	switch (cps1_dipswitch[DIP_C] & 0x03)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x01: (dip++)->value = 0; break;
	case 0x02: (dip++)->value = 2; break;
	case 0x03: (dip++)->value = 1; break;
	}
	switch (cps1_dipswitch[DIP_C] & 0x0c)
	{
	case 0x00: (dip++)->value = 0; break;
	case 0x04: (dip++)->value = 3; break;
	case 0x08: (dip++)->value = 2; break;
	case 0x0c: (dip++)->value = 1; break;
	}
	load_flip_screen;
	load_demo_sounds2;
	load_allow_continue2;
	load_game_mode;
}

static void dip_save_willow(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin2a((dip++)->value);
	dip_save_coin2b((dip++)->value);
	dip_save_cabinet((dip++)->value);

	// DIP B
	dip_save_difficulty2((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x18; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x08; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	dip_save_bit(DIP_B, (dip++)->value, 7, 1);

	// DIP C
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x02; break;
	case 1: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x03; break;
	case 2: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x01; break;
	case 3: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_C] &= ~0x0c; cps1_dipswitch[DIP_C] |= 0x00; break;
	case 1: cps1_dipswitch[DIP_C] &= ~0x0c; cps1_dipswitch[DIP_C] |= 0x0c; break;
	case 2: cps1_dipswitch[DIP_C] &= ~0x0c; cps1_dipswitch[DIP_C] |= 0x08; break;
	case 3: cps1_dipswitch[DIP_C] &= ~0x0c; cps1_dipswitch[DIP_C] |= 0x04; break;
	}
	save_flip_screen;
	save_demo_sounds2;
	save_allow_continue2;
	save_game_mode;
}

/*--------------------------------------
  unsquad
--------------------------------------*/


static void dip_load_unsquad(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin2a();
	(dip++)->value = dip_load_coin2b();

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0x18)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x08: (dip++)->value = 2; break;
	case 0x10: (dip++)->value = 0; break;
	case 0x18: (dip++)->value = 1; break;
	}

	// DIP C
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue;
	load_game_mode;
}

static void dip_save_unsquad(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin2a((dip++)->value);
	dip_save_coin2b((dip++)->value);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x18; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x08; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x00; break;
	}

	// DIP C
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue;
	save_game_mode;
}

/*--------------------------------------
  ffight
--------------------------------------*/


static void dip_load_ffight(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_coin1b();
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0x18)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x08: (dip++)->value = 2; break;
	case 0x10: (dip++)->value = 1; break;
	case 0x18: (dip++)->value = 0; break;
	}
	switch (cps1_dipswitch[DIP_B] & 0x60)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x20: (dip++)->value = 2; break;
	case 0x40: (dip++)->value = 1; break;
	case 0x60: (dip++)->value = 0; break;
	}

	// DIP C
	switch (cps1_dipswitch[DIP_C] & 0x03)
	{
	case 0x00: (dip++)->value = 0; break;
	case 0x01: (dip++)->value = 3; break;
	case 0x02: (dip++)->value = 2; break;
	case 0x03: (dip++)->value = 1; break;
	}
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue;
	load_game_mode;
}

static void dip_save_ffight(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_coin1b((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x18; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x08; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x60; cps1_dipswitch[DIP_B] |= 0x60; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x60; cps1_dipswitch[DIP_B] |= 0x40; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x60; cps1_dipswitch[DIP_B] |= 0x20; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x60; cps1_dipswitch[DIP_B] |= 0x00; break;
	}

	// DIP C
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x00; break;
	case 1: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x03; break;
	case 2: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x02; break;
	case 3: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x01; break;
	}
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue;
	save_game_mode;
}

/*--------------------------------------
  1941
--------------------------------------*/


static void dip_load_1941(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_coin1b();
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0x18)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x08: (dip++)->value = 2; break;
	case 0x10: (dip++)->value = 1; break;
	case 0x18: (dip++)->value = 0; break;
	}
	switch (cps1_dipswitch[DIP_B] & 0x60)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x20: (dip++)->value = 2; break;
	case 0x40: (dip++)->value = 1; break;
	case 0x60: (dip++)->value = 0; break;
	}
	(dip++)->value = dip_load_bit(DIP_B, 7, 1);

	// DIP C
	(dip++)->value = dip_load_bit(DIP_C, 0, 0);
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue;
	load_game_mode;
}

static void dip_save_1941(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_coin1b((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x18; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x08; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x60; cps1_dipswitch[DIP_B] |= 0x60; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x60; cps1_dipswitch[DIP_B] |= 0x40; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x60; cps1_dipswitch[DIP_B] |= 0x20; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x60; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	dip_save_bit(DIP_B, (dip++)->value, 7, 1);

	// DIP C
	dip_save_bit(DIP_C, (dip++)->value, 0, 0);
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue;
	save_game_mode;
}

/*--------------------------------------
  mercs
--------------------------------------*/





static void dip_load_mercs(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	(dip++)->value = dip_load_bit(DIP_B, 3, 0);
	(dip++)->value = dip_load_bit(DIP_B, 4, 0);

	// DIP C
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue;
	load_game_mode;
}

static void dip_save_mercs(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	dip_save_bit(DIP_B, (dip++)->value, 3, 0);
	dip_save_bit(DIP_B, (dip++)->value, 4, 0);

	// DIP C
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue;
	save_game_mode;
}

/*--------------------------------------
  mtwins
--------------------------------------*/


static void dip_load_mtwins(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_coin1b();

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0x38)
	{
	case 0x00: case 0x20: (dip++)->value = 3; break;
	case 0x08: case 0x28: (dip++)->value = 2; break;
	case 0x10: case 0x30: (dip++)->value = 0; break;
	case 0x18: case 0x38: (dip++)->value = 1; break;
	}

	// DIP C
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue;
	load_game_mode;
}

static void dip_save_mtwins(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_coin1b((dip++)->value);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x18; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x08; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x00; break;
	}

	// DIP C
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue;
	save_game_mode;
}

/*--------------------------------------
  msword
--------------------------------------*/


static void dip_load_msword(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_coin1b();
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0x38)
	{
	case 0x00: (dip++)->value = 7; break;
	case 0x08: (dip++)->value = 6; break;
	case 0x10: (dip++)->value = 5; break;
	case 0x18: (dip++)->value = 4; break;
	case 0x20: (dip++)->value = 0; break;
	case 0x28: (dip++)->value = 1; break;
	case 0x30: (dip++)->value = 2; break;
	case 0x38: (dip++)->value = 3; break;
	}
	(dip++)->value = dip_load_bit(DIP_B, 6, 1);

	// DIP C
	switch (cps1_dipswitch[DIP_C] & 0x03)
	{
	case 0x00: (dip++)->value = 0; break;
	case 0x01: (dip++)->value = 3; break;
	case 0x02: (dip++)->value = 2; break;
	case 0x03: (dip++)->value = 1; break;
	}
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue;
	load_game_mode;
}

static void dip_save_msword(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_coin1b((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x20; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x28; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x30; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x38; break;
	case 4: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x18; break;
	case 5: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 6: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x08; break;
	case 7: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	dip_save_bit(DIP_B, (dip++)->value, 6, 1);

	// DIP C
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x00; break;
	case 1: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x03; break;
	case 2: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x02; break;
	case 3: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x01; break;
	}
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue;
	save_game_mode;
}

/*--------------------------------------
  cawing
--------------------------------------*/


static void dip_load_cawing(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_coin1b();
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0x18)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x08: (dip++)->value = 2; break;
	case 0x10: (dip++)->value = 0; break;
	case 0x18: (dip++)->value = 1; break;
	}

	// DIP C
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue;
	load_game_mode;
}

static void dip_save_cawing(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_coin1b((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x18; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x08; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x00; break;
	}

	// DIP C
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue;
	save_game_mode;
}

/*--------------------------------------
  nemo
--------------------------------------*/


static void dip_load_nemo(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_coin1b();
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0x18)
	{
	case 0x00: (dip++)->value = 0; break;
	case 0x08: (dip++)->value = 2; break;
	case 0x18: (dip++)->value = 1; break;
	}

	// DIP C
	switch (cps1_dipswitch[DIP_C] & 0x03)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x01: (dip++)->value = 2; break;
	case 0x02: (dip++)->value = 0; break;
	case 0x03: (dip++)->value = 1; break;
	}
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue;
	load_game_mode;
}

static void dip_save_nemo(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_coin1b((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x00; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x18; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x08; break;
	}

	// DIP C
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x02; break;
	case 1: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x03; break;
	case 2: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x01; break;
	case 3: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x00; break;
	}
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue;
	save_game_mode;
}

/*--------------------------------------
  sf2 / sf2j
--------------------------------------*/



static void dip_load_sf2(int type, int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1:dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	case 2:dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	case 3:dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	default:dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_coin1b();
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	if (type == 1) (dip++)->value = dip_load_bit(DIP_B, 3, 1);

	// DIP C
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue;
	load_game_mode;
}

static void dip_save_sf2(int type, int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1:dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	case 2:dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	case 3:dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	default:dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_coin1b((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	if (type == 1) dip_save_bit(DIP_B, (dip++)->value, 3, 1);

	// DIP C
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue;
	save_game_mode;
}

/*--------------------------------------
  3wonders
--------------------------------------*/


static void dip_load_3wonders(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_coin1b();
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);
	(dip++)->value = dip_load_bit(DIP_A, 7, 1);

	// DIP B
	switch (cps1_dipswitch[DIP_B] & 0x03)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x01: (dip++)->value = 2; break;
	case 0x02: (dip++)->value = 1; break;
	case 0x03: (dip++)->value = 0; break;
	}
	switch (cps1_dipswitch[DIP_B] & 0x0c)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x04: (dip++)->value = 2; break;
	case 0x08: (dip++)->value = 1; break;
	case 0x0c: (dip++)->value = 0; break;
	}
	switch (cps1_dipswitch[DIP_B] & 0x30)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x10: (dip++)->value = 2; break;
	case 0x20: (dip++)->value = 1; break;
	case 0x30: (dip++)->value = 0; break;
	}
	switch (cps1_dipswitch[DIP_B] & 0xc0)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x40: (dip++)->value = 2; break;
	case 0x80: (dip++)->value = 1; break;
	case 0xc0: (dip++)->value = 0; break;
	}

	// DIP C
	switch (cps1_dipswitch[DIP_C] & 0x03)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x01: (dip++)->value = 2; break;
	case 0x02: (dip++)->value = 1; break;
	case 0x03: (dip++)->value = 0; break;
	}
	switch (cps1_dipswitch[DIP_C] & 0x0c)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x04: (dip++)->value = 2; break;
	case 0x08: (dip++)->value = 1; break;
	case 0x0c: (dip++)->value = 0; break;
	}
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue;
	load_game_mode;
}

static void dip_save_3wonders(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_coin1b((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);
	dip_save_bit(DIP_A, (dip++)->value, 7, 1);

	// DIP B
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x03; cps1_dipswitch[DIP_B] |= 0x03; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x03; cps1_dipswitch[DIP_B] |= 0x02; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x03; cps1_dipswitch[DIP_B] |= 0x01; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x03; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x0c; cps1_dipswitch[DIP_B] |= 0x0c; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x0c; cps1_dipswitch[DIP_B] |= 0x08; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x0c; cps1_dipswitch[DIP_B] |= 0x04; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x0c; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x30; cps1_dipswitch[DIP_B] |= 0x30; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x30; cps1_dipswitch[DIP_B] |= 0x20; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x30; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x30; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0xc0; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0x80; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0x40; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0x00; break;
	}

	// DIP C
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x03; break;
	case 1: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x02; break;
	case 2: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x01; break;
	case 3: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_C] &= ~0x0c; cps1_dipswitch[DIP_C] |= 0x0c; break;
	case 1: cps1_dipswitch[DIP_C] &= ~0x0c; cps1_dipswitch[DIP_C] |= 0x08; break;
	case 2: cps1_dipswitch[DIP_C] &= ~0x0c; cps1_dipswitch[DIP_C] |= 0x04; break;
	case 3: cps1_dipswitch[DIP_C] &= ~0x0c; cps1_dipswitch[DIP_C] |= 0x00; break;
	}
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue;
	save_game_mode;
}

/*--------------------------------------
  kod / kodj
--------------------------------------*/



static void dip_load_kod(int type, int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	case 2: dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	case 3: dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	default: dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_bit(DIP_A, 3, 0);
	(dip++)->value = dip_load_bit(DIP_A, 4, 0);
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0x38)
	{
	case 0x00: (dip++)->value = 7; break;
	case 0x08: (dip++)->value = 6; break;
	case 0x10: (dip++)->value = 5; break;
	case 0x18: (dip++)->value = 4; break;
	case 0x20: (dip++)->value = 3; break;
	case 0x28: (dip++)->value = 2; break;
	case 0x30: (dip++)->value = 0; break;
	case 0x38: (dip++)->value = 1; break;
	}
	switch (cps1_dipswitch[DIP_B] & 0xc0)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x40: (dip++)->value = 2; break;
	case 0x80: (dip++)->value = 0; break;
	case 0xc0: (dip++)->value = 1; break;
	}

	// DIP C
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue;
	load_game_mode;
}

static void dip_save_kod(int type, int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	case 2: dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	case 3: dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	default: dip = (type == 0) ? active_dipswitch : active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 3, 0);
	dip_save_bit(DIP_A, (dip++)->value, 4, 0);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x30; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x38; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x28; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x20; break;
	case 4: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x18; break;
	case 5: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 6: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x08; break;
	case 7: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0x80; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0xc0; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0x40; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0x00; break;
	}

	// DIP C
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue;
	save_game_mode;
}
/*--------------------------------------
  captcomm
--------------------------------------*/


static void dip_load_captcomm(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0x18)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x08: (dip++)->value = 2; break;
	case 0x10: (dip++)->value = 1; break;
	case 0x18: (dip++)->value = 0; break;
	}
	switch (cps1_dipswitch[DIP_B] & 0xc0)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x40: (dip++)->value = 0; break;
	case 0x80: (dip++)->value = 2; break;
	case 0xc0: (dip++)->value = 1; break;
	}

	// DIP C
	switch (cps1_dipswitch[DIP_C] & 0x03)
	{
	case 0x00: (dip++)->value = 0; break;
	case 0x01: (dip++)->value = 3; break;
	case 0x02: (dip++)->value = 2; break;
	case 0x03: (dip++)->value = 1; break;
	}
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue;
	load_game_mode;
}

static void dip_save_captcomm(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x18; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x08; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0x40; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0xc0; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0x80; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0x00; break;
	}

	// DIP C
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x00; break;
	case 1: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x03; break;
	case 2: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x02; break;
	case 3: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x01; break;
	}
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue;
	save_game_mode;
}

/*--------------------------------------
  knights
--------------------------------------*/


static void dip_load_knights(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0x38)
	{
	case 0x00: (dip++)->value = 0; break;
	case 0x08: (dip++)->value = 1; break;
	case 0x10: (dip++)->value = 2; break;
	case 0x18: (dip++)->value = 7; break;
	case 0x20: (dip++)->value = 6; break;
	case 0x28: (dip++)->value = 5; break;
	case 0x30: (dip++)->value = 4; break;
	case 0x38: (dip++)->value = 3; break;
	}
	(dip++)->value = dip_load_bit(DIP_B, 6, 0);
	(dip++)->value = dip_load_bit(DIP_B, 7, 0);

	// DIP C
	switch (cps1_dipswitch[DIP_C] & 0x03)
	{
	case 0x00: (dip++)->value = 0; break;
	case 0x01: (dip++)->value = 3; break;
	case 0x02: (dip++)->value = 2; break;
	case 0x03: (dip++)->value = 1; break;
	}
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue;
	load_game_mode;
}

static void dip_save_knights(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x00; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x08; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x38; break;
	case 4: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x30; break;
	case 5: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x28; break;
	case 6: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x20; break;
	case 7: cps1_dipswitch[DIP_B] &= ~0x38; cps1_dipswitch[DIP_B] |= 0x18; break;
	}
	dip_save_bit(DIP_B, (dip++)->value, 6, 0);
	dip_save_bit(DIP_B, (dip++)->value, 7, 0);

	// DIP C
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x00; break;
	case 1: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x03; break;
	case 2: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x02; break;
	case 3: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x01; break;
	}
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue;
	save_game_mode;
}

/*--------------------------------------
  varth
--------------------------------------*/


static void dip_load_varth(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_coin1b();
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0x18)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x08: (dip++)->value = 2; break;
	case 0x10: (dip++)->value = 1; break;
	case 0x18: (dip++)->value = 0; break;
	}

	// DIP C
	switch (cps1_dipswitch[DIP_C] & 0x03)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x01: (dip++)->value = 1; break;
	case 0x02: (dip++)->value = 0; break;
	case 0x03: (dip++)->value = 2; break;
	}
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue;
	load_game_mode;
}

static void dip_save_varth(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_coin1b((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x18; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x08; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x00; break;
	}

	// DIP C
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x02; break;
	case 1: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x01; break;
	case 2: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x03; break;
	case 3: cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x00; break;
	}
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue;
	save_game_mode;
}

/*--------------------------------------
  cworld2j
--------------------------------------*/


static void dip_load_cworld2j(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);
	(dip++)->value = dip_load_bit(DIP_A, 7, 1);

	// DIP B
	switch (cps1_dipswitch[DIP_B] & 0x07)
	{
	case 0x02: (dip++)->value = 4; break;
	case 0x03: (dip++)->value = 3; break;
	case 0x04: (dip++)->value = 2; break;
	case 0x05: (dip++)->value = 1; break;
	case 0x06: (dip++)->value = 0; break;
	}
	switch (cps1_dipswitch[DIP_B] & 0x18)
	{
	case 0x00: (dip++)->value = 2; break;
	case 0x10: (dip++)->value = 1; break;
	case 0x18: (dip++)->value = 0; break;
	}
	switch (cps1_dipswitch[DIP_B] & 0xe0)
	{
	case 0x00: (dip++)->value = 0; break;
	case 0x80: (dip++)->value = 1; break;
	case 0xa0: (dip++)->value = 3; break;
	case 0xc0: (dip++)->value = 4; break;
	case 0xe0: (dip++)->value = 2; break;
	}

	// DIP C
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue2;
	load_game_mode;
}

static void dip_save_cworld2j(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);
	dip_save_bit(DIP_A, (dip++)->value, 7, 1);

	// DIP B
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x06; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x05; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x04; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x03; break;
	case 4: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x02; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x18; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0x00; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0x80; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0xe0; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0xa0; break;
	case 4: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0xc0; break;
	}

	// DIP C
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue2;
	save_game_mode;
}

/*--------------------------------------
  qad / qadj
--------------------------------------*/



static void dip_load_qad(int type, int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	case 2:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	case 3:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	default:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);
	(dip++)->value = dip_load_bit(DIP_A, 7, 1);

	// DIP B
	if (type == 0)
	{
		switch (cps1_dipswitch[DIP_B] & 0x07)
		{
		case 0x02: (dip++)->value = 4; break;
		case 0x03: (dip++)->value = 3; break;
		case 0x04: (dip++)->value = 2; break;
		case 0x05: (dip++)->value = 1; break;
		case 0x06: (dip++)->value = 0; break;
		}
		switch (cps1_dipswitch[DIP_B] & 0x18)
		{
		case 0x00: (dip++)->value = 3; break;
		case 0x08: (dip++)->value = 2; break;
		case 0x10: (dip++)->value = 1; break;
		case 0x18: (dip++)->value = 0; break;
		}
		switch (cps1_dipswitch[DIP_B] & 0xe0)
		{
		case 0x60: (dip++)->value = 0; break;
		case 0x80: (dip++)->value = 1; break;
		case 0xa0: (dip++)->value = 2; break;
		case 0xc0: (dip++)->value = 3; break;
		case 0xe0: (dip++)->value = 4; break;
		}
	}
	else
	{
		switch (cps1_dipswitch[DIP_B] & 0x07)
		{
		case 0x03: (dip++)->value = 4; break;
		case 0x04: (dip++)->value = 3; break;
		case 0x05: (dip++)->value = 2; break;
		case 0x06: (dip++)->value = 1; break;
		case 0x07: (dip++)->value = 0; break;
		}
		switch (cps1_dipswitch[DIP_B] & 0xe0)
		{
		case 0xa0: (dip++)->value = 0; break;
		case 0xc0: (dip++)->value = 1; break;
		case 0xe0: (dip++)->value = 2; break;
		}
	}

	// DIP C
	load_free_play;
	load_freeze;
	load_flip_screen;
	if (type == 0)
	{
		load_demo_sounds2;
	}
	else
	{
		load_demo_sounds;
	}
	load_allow_continue2;
	load_game_mode;
}

static void dip_save_qad(int type, int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	case 2:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	case 3:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	default:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);
	dip_save_bit(DIP_A, (dip++)->value, 7, 1);

	// DIP B
	if (type == 0)
	{
		switch ((dip++)->value)
		{
		case 0: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x06; break;
		case 1: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x05; break;
		case 2: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x04; break;
		case 3: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x03; break;
		case 4: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x02; break;
		}
		switch ((dip++)->value)
		{
		case 0: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x18; break;
		case 1: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x10; break;
		case 2: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x08; break;
		case 3: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x00; break;
		}
		switch ((dip++)->value)
		{
		case 0: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0x60; break;
		case 1: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0x80; break;
		case 2: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0xa0; break;
		case 3: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0xc0; break;
		case 4: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0xe0; break;
		}
	}
	else
	{
		switch ((dip++)->value)
		{
		case 0: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x07; break;
		case 1: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x06; break;
		case 2: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x05; break;
		case 3: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x04; break;
		case 4: cps1_dipswitch[DIP_B] &= ~0x07; cps1_dipswitch[DIP_B] |= 0x03; break;
		}
		switch ((dip++)->value)
		{
		case 0: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0xa0; break;
		case 1: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0xc0; break;
		case 2: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0xe0; break;
		}
	}

	// DIP C
	save_free_play;
	save_freeze;
	save_flip_screen;
	if (type == 0)
	{
		save_demo_sounds2;
	}
	else
	{
		save_demo_sounds;
	}
	save_allow_continue2;
	save_game_mode;
}

/*--------------------------------------
  qtono2
--------------------------------------*/


static void dip_load_qtono2(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_bit(DIP_A, 6, 1);

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0xe0)
	{
	case 0x60: (dip++)->value = 0; break;
	case 0x80: (dip++)->value = 1; break;
	case 0xa0: (dip++)->value = 3; break;
	case 0xc0: (dip++)->value = 4; break;
	case 0xe0: (dip++)->value = 2; break;
	}

	// DIP C
	(dip++)->value = dip_load_bit(DIP_C, 1, 1);
	load_free_play;
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_allow_continue2;
	load_game_mode;
}

static void dip_save_qtono2(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 6, 1);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0x60; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0x80; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0xe0; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0xa0; break;
	case 4: cps1_dipswitch[DIP_B] &= ~0xe0; cps1_dipswitch[DIP_B] |= 0xc0; break;
	}

	// DIP C
	dip_save_bit(DIP_C, (dip++)->value, 1, 1);
	save_free_play;
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_allow_continue2;
	save_game_mode;
}

/*--------------------------------------
  megaman / rockmanj
--------------------------------------*/







static void dip_load_megaman(int type, int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	case 2:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	case 3:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	default:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;
	}

	// DIP A
	switch (cps1_dipswitch[DIP_A] & 0x1f)
	{
	case 0x0d: (dip++)->value = 18; break;
	case 0x0e: (dip++)->value = 8; break;
	case 0x0f: (dip++)->value = 0; break;
	case 0x10: (dip++)->value = 1; break;
	case 0x11: (dip++)->value = 2; break;
	case 0x12: (dip++)->value = 3; break;
	case 0x13: (dip++)->value = 4; break;
	case 0x14: (dip++)->value = 5; break;
	case 0x15: (dip++)->value = 6; break;
	case 0x16: (dip++)->value = 7; break;
	case 0x17: (dip++)->value = 17; break;
	case 0x18: (dip++)->value = 16; break;
	case 0x19: (dip++)->value = 15; break;
	case 0x1a: (dip++)->value = 14; break;
	case 0x1b: (dip++)->value = 13; break;
	case 0x1c: (dip++)->value = 12; break;
	case 0x1d: (dip++)->value = 11; break;
	case 0x1e: (dip++)->value = 10; break;
	case 0x1f: (dip++)->value = 9; break;
	}
	switch (cps1_dipswitch[DIP_A] & 0x60)
	{
	case 0x20: (dip++)->value = 1; break;
	case 0x40: (dip++)->value = 0; break;
	case 0x60: (dip++)->value = 2; break;
	}

	// DIP B
	switch (cps1_dipswitch[DIP_B] & 0x03)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x01: (dip++)->value = 2; break;
	case 0x02: (dip++)->value = 1; break;
	case 0x03: (dip++)->value = 0; break;
	}
	switch (cps1_dipswitch[DIP_B] & 0x0c)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x04: (dip++)->value = 2; break;
	case 0x08: (dip++)->value = 1; break;
	case 0x0c: (dip++)->value = 0; break;
	}
	if (type == 0) (dip++)->value = dip_load_bit(DIP_B, 6, 1);

	// DIP C
	(dip++)->value = dip_load_bit(DIP_C, 0, 1);
	(dip++)->value = dip_load_bit(DIP_C, 1, 0);
	(dip++)->value = dip_load_bit(DIP_C, 2, type);
	load_game_mode;
}

static void dip_save_megaman(int type, int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	case 2:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	case 3:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;

	default:
		{
		if (type == 0)
			dip = active_dipswitch;
		else
			dip = active_dipswitch;
		}
	break;
	}

	// DIP A
	switch ((dip++)->value)
	{
	case 0:  cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x0f; break;
	case 1:  cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x10; break;
	case 2:  cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x11; break;
	case 3:  cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x12; break;
	case 4:  cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x13; break;
	case 5:  cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x14; break;
	case 6:  cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x15; break;
	case 7:  cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x16; break;
	case 8:  cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x0e; break;
	case 9:  cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x1f; break;
	case 10: cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x1e; break;
	case 11: cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x1d; break;
	case 12: cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x1c; break;
	case 13: cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x1b; break;
	case 14: cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x1a; break;
	case 15: cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x19; break;
	case 16: cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x18; break;
	case 17: cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x17; break;
	case 18: cps1_dipswitch[DIP_A] &= ~0x1f; cps1_dipswitch[DIP_A] |= 0x0d; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_A] &= ~0x60; cps1_dipswitch[DIP_A] |= 0x40; break;
	case 1: cps1_dipswitch[DIP_A] &= ~0x60; cps1_dipswitch[DIP_A] |= 0x20; break;
	case 2: cps1_dipswitch[DIP_A] &= ~0x60; cps1_dipswitch[DIP_A] |= 0x60; break;
	}

	// DIP B
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x03; cps1_dipswitch[DIP_B] |= 0x03; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x03; cps1_dipswitch[DIP_B] |= 0x02; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x03; cps1_dipswitch[DIP_B] |= 0x01; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x03; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x0c; cps1_dipswitch[DIP_B] |= 0x0c; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x0c; cps1_dipswitch[DIP_B] |= 0x08; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x0c; cps1_dipswitch[DIP_B] |= 0x04; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x0c; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	if (type == 0) dip_save_bit(DIP_B, (dip++)->value, 6, 1);

	// DIP C
	dip_save_bit(DIP_C, (dip++)->value, 0, 1);
	dip_save_bit(DIP_C, (dip++)->value, 1, 0);
	dip_save_bit(DIP_C, (dip++)->value, 2, type);
	save_game_mode;
}

/*--------------------------------------
  pnickj
--------------------------------------*/


static void dip_load_pnickj(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	(dip++)->value = dip_load_coin1a();
	(dip++)->value = dip_load_bit(DIP_A, 3, 1);

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0xc0)
	{
	case 0x00: (dip++)->value = 3; break;
	case 0x40: (dip++)->value = 2; break;
	case 0x80: (dip++)->value = 1; break;
	case 0xc0: (dip++)->value = 0; break;
	}

	// DIP C
	load_freeze;
	load_flip_screen;
	load_demo_sounds;
	load_game_mode;
}

static void dip_save_pnickj(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	dip_save_coin1a((dip++)->value);
	dip_save_bit(DIP_A, (dip++)->value, 3, 1);

	// DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0xc0; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0x80; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0x40; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0xc0; cps1_dipswitch[DIP_B] |= 0x00; break;
	}

	// DIP C
	save_freeze;
	save_flip_screen;
	save_demo_sounds;
	save_game_mode;
}

#if !RELEASE
/*--------------------------------------
  wofhfh
--------------------------------------*/





static void dip_load_wofhfh(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	switch (cps1_dipswitch[DIP_A] & 0x03)
	{
	case 0x03: (dip++)->value = 0; break;
	case 0x02: (dip++)->value = 1; break;
	case 0x01: (dip++)->value = 2; break;
	case 0x00: (dip++)->value = 3; break;
	}

	// DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0x70)
	{
	case 0x00: (dip++)->value = 0; break;
	case 0x10: (dip++)->value = 1; break;
	case 0x20: (dip++)->value = 2; break;
	case 0x30: (dip++)->value = 3; break;
	case 0x40: (dip++)->value = 4; break;
	case 0x50: (dip++)->value = 5; break;
	case 0x60: (dip++)->value = 6; break;
	case 0x70: (dip++)->value = 7; break;
	}

	// DIP C
	switch (cps1_dipswitch[DIP_C] & 0x03)
	{
	case 0x01: (dip++)->value = 0; break;
	case 0x02: (dip++)->value = 1; break;
	case 0x03: (dip++)->value = 2; break;
	}
}

static void dip_save_wofhfh(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	switch ((dip++)->value)
	{
	case 0:  cps1_dipswitch[DIP_A] &= ~0x03; cps1_dipswitch[DIP_A] |= 0x03; break;
	case 1:  cps1_dipswitch[DIP_A] &= ~0x03; cps1_dipswitch[DIP_A] |= 0x02; break;
	case 2:  cps1_dipswitch[DIP_A] &= ~0x03; cps1_dipswitch[DIP_A] |= 0x01; break;
	case 3:  cps1_dipswitch[DIP_A] &= ~0x03; cps1_dipswitch[DIP_A] |= 0x00; break;
	}

	// DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x70; cps1_dipswitch[DIP_B] |= 0x00; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x70; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x70; cps1_dipswitch[DIP_B] |= 0x20; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x70; cps1_dipswitch[DIP_B] |= 0x30; break;
	case 4: cps1_dipswitch[DIP_B] &= ~0x70; cps1_dipswitch[DIP_B] |= 0x40; break;
	case 5: cps1_dipswitch[DIP_B] &= ~0x70; cps1_dipswitch[DIP_B] |= 0x50; break;
	case 6: cps1_dipswitch[DIP_B] &= ~0x70; cps1_dipswitch[DIP_B] |= 0x60; break;
	case 7: cps1_dipswitch[DIP_B] &= ~0x70; cps1_dipswitch[DIP_B] |= 0x70; break;
	}

	// DIP C
	switch ((dip++)->value)
	{
	case 0:  cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x01; break;
	case 1:  cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x02; break;
	case 2:  cps1_dipswitch[DIP_C] &= ~0x03; cps1_dipswitch[DIP_C] |= 0x03; break;
	}
}

/*--------------------------------------
  punisherbz
--------------------------------------*/


static void dip_load_punisherbz(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	switch (cps1_dipswitch[DIP_A] & 0x08)
	{
	case 0x08: (dip++)->value = 0; break;
	case 0x00: (dip++)->value = 1; break;
	}
	switch (cps1_dipswitch[DIP_A] & 0x30)
	{
	case 0x30: (dip++)->value = 0; break;
	case 0x20: (dip++)->value = 1; break;
	case 0x10: (dip++)->value = 2; break;
	case 0x00: (dip++)->value = 3; break;
	}
	switch (cps1_dipswitch[DIP_A] & 0x40)
	{
	case 0x40: (dip++)->value = 0; break;
	case 0x00: (dip++)->value = 1; break;
	}
	switch (cps1_dipswitch[DIP_A] & 0x80)
	{
	case 0x80: (dip++)->value = 0; break;
	case 0x00: (dip++)->value = 1; break;
	}
	//DIP B
	(dip++)->value = dip_load_difficulty1();
	switch (cps1_dipswitch[DIP_B] & 0x18)
	{
	case 0x18: (dip++)->value = 0; break;
	case 0x10: (dip++)->value = 1; break;
	case 0x08: (dip++)->value = 2; break;
	case 0x00: (dip++)->value = 3; break;
	}
	switch (cps1_dipswitch[DIP_B] & 0x20)
	{
	case 0x20: (dip++)->value = 0; break;
	case 0x00: (dip++)->value = 1; break;
	}
	switch (cps1_dipswitch[DIP_B] & 0x40)
	{
	case 0x40: (dip++)->value = 0; break;
	case 0x00: (dip++)->value = 1; break;
	}
}

static void dip_save_punisherbz(int language)
{
	dipswitch_t *dip;

	switch (language)
	{
	case 1: dip = active_dipswitch;break;
	case 2: dip = active_dipswitch;break;
	case 3: dip = active_dipswitch;break;
	default: dip = active_dipswitch;break;
	}

	// DIP A
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_A] &= ~0x08; cps1_dipswitch[DIP_A] |= 0x08; break;
	case 1: cps1_dipswitch[DIP_A] &= ~0x08; cps1_dipswitch[DIP_A] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_A] &= ~0x30; cps1_dipswitch[DIP_A] |= 0x30; break;
	case 1: cps1_dipswitch[DIP_A] &= ~0x30; cps1_dipswitch[DIP_A] |= 0x20; break;
	case 2: cps1_dipswitch[DIP_A] &= ~0x30; cps1_dipswitch[DIP_A] |= 0x10; break;
	case 3: cps1_dipswitch[DIP_A] &= ~0x30; cps1_dipswitch[DIP_A] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_A] &= ~0x40; cps1_dipswitch[DIP_A] |= 0x40; break;
	case 1: cps1_dipswitch[DIP_A] &= ~0x40; cps1_dipswitch[DIP_A] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_A] &= ~0x80; cps1_dipswitch[DIP_A] |= 0x80; break;
	case 1: cps1_dipswitch[DIP_A] &= ~0x80; cps1_dipswitch[DIP_A] |= 0x00; break;
	}
	//DIP B
	dip_save_difficulty1((dip++)->value);
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x18; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x10; break;
	case 2: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x08; break;
	case 3: cps1_dipswitch[DIP_B] &= ~0x18; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x20; cps1_dipswitch[DIP_B] |= 0x20; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x20; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
	switch ((dip++)->value)
	{
	case 0: cps1_dipswitch[DIP_B] &= ~0x40; cps1_dipswitch[DIP_B] |= 0x40; break;
	case 1: cps1_dipswitch[DIP_B] &= ~0x40; cps1_dipswitch[DIP_B] |= 0x00; break;
	}
}
#endif


dipswitch_t *load_dipswitch(int *sx)
{
	dipswitch_t *dipswitch = NULL;
	const char *profile = dip_profile_name(machine_input_type);
	char path[PATH_MAX];
	dip_metadata_error_t metadata_error;

	dip_metadata_unload(&dip_metadata);
	active_dipswitch = NULL;
	if (profile == NULL)
		return NULL;
	if (!path_format(path, sizeof(path), "%s%s", launchDir, "dip_metadata.cps1"))
		return NULL;
	metadata_error = dip_metadata_load_profile(
		&dip_metadata, path, profile, dip_metadata_language());
	if (metadata_error != DIP_METADATA_OK)
	{
		printf("CPS1 DIP metadata: cannot load %s/%s: %s\n", path, profile,
			dip_metadata_error_string(metadata_error));
		return NULL;
	}
	active_dipswitch = dip_metadata_active(&dip_metadata);

	if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_JAPANESE)
	{
		switch (machine_input_type)
		{
		case INPTYPE_forgottn:	dip_load_forgottn(1);	dipswitch = active_dipswitch; break;
		case INPTYPE_ghouls:	dip_load_ghouls(0,1);	dipswitch = active_dipswitch; break;
		case INPTYPE_ghoulsu:	dip_load_ghouls(1,1);	dipswitch = active_dipswitch; break;
		case INPTYPE_daimakai:	dip_load_ghouls(2,1);	dipswitch = active_dipswitch; break;
		case INPTYPE_strider:	dip_load_strider(0,1);	dipswitch = active_dipswitch; break;
		case INPTYPE_stridrua:	dip_load_strider(1,1);	dipswitch = active_dipswitch; break;
		case INPTYPE_dynwar:	dip_load_dynwar(1);		dipswitch = active_dipswitch; break;
		case INPTYPE_willow:	dip_load_willow(1);		dipswitch = active_dipswitch; break;
		case INPTYPE_unsquad:	dip_load_unsquad(1);	dipswitch = active_dipswitch; break;
		case INPTYPE_ffight:	dip_load_ffight(1);		dipswitch = active_dipswitch; break;
		case INPTYPE_1941:		dip_load_1941(1);		dipswitch = active_dipswitch; break;
		case INPTYPE_mercs:	dip_load_mercs(1);		dipswitch = active_dipswitch; break;
		case INPTYPE_mtwins:	dip_load_mtwins(1);		dipswitch = active_dipswitch; break;
		case INPTYPE_msword:	dip_load_msword(1);		dipswitch = active_dipswitch; *sx = 270; break;
		case INPTYPE_cawing:	dip_load_cawing(1);		dipswitch = active_dipswitch; *sx = 270; break;
		case INPTYPE_nemo:		dip_load_nemo(1);		dipswitch = active_dipswitch; break;
		case INPTYPE_sf2:		dip_load_sf2(0,1);		dipswitch = active_dipswitch; break;
		case INPTYPE_sf2j:		dip_load_sf2(1,1);		dipswitch = active_dipswitch; break;
		case INPTYPE_3wonders:	dip_load_3wonders(1);	dipswitch = active_dipswitch; break;
		case INPTYPE_kod:		dip_load_kod(0,1);		dipswitch = active_dipswitch; break;
		case INPTYPE_kodj:		dip_load_kod(1,1);		dipswitch = active_dipswitch; break;
		case INPTYPE_captcomm:	dip_load_captcomm(1);	dipswitch = active_dipswitch; break;
		case INPTYPE_knights:	dip_load_knights(1);	dipswitch = active_dipswitch; break;
		case INPTYPE_varth:	dip_load_varth(1);		dipswitch = active_dipswitch; break;
		case INPTYPE_cworld2j:	dip_load_cworld2j(1);	dipswitch = active_dipswitch; break;
		case INPTYPE_qad:		dip_load_qad(0,1);		dipswitch = active_dipswitch; break;
		case INPTYPE_qadj:		dip_load_qad(1,1);		dipswitch = active_dipswitch; break;
		case INPTYPE_qtono2:	dip_load_qtono2(1);		dipswitch = active_dipswitch; break;
		case INPTYPE_megaman:	dip_load_megaman(0,1);	dipswitch = active_dipswitch; break;
		case INPTYPE_rockmanj:	dip_load_megaman(1,1);	dipswitch = active_dipswitch; break;
		case INPTYPE_pnickj:	dip_load_pnickj(1);		dipswitch = active_dipswitch; break;
#if !RELEASE
		case INPTYPE_wofhfh:	dip_load_wofhfh(1);		dipswitch = active_dipswitch; break;
		case INPTYPE_punisherbz:dip_load_punisherbz(1);dipswitch = active_dipswitch; break;
#endif
		}
	}
	else if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_CHINESE_SIMPLIFIED)
	{
		switch (machine_input_type)
		{
		case INPTYPE_forgottn: dip_load_forgottn(2);  dipswitch = active_dipswitch; break;
		case INPTYPE_ghouls:   dip_load_ghouls(0,2);  dipswitch = active_dipswitch; break;
		case INPTYPE_ghoulsu:  dip_load_ghouls(1,2);  dipswitch = active_dipswitch; break;
		case INPTYPE_daimakai: dip_load_ghouls(2,2);  dipswitch = active_dipswitch; break;
		case INPTYPE_strider:  dip_load_strider(0,2); dipswitch = active_dipswitch; break;
		case INPTYPE_stridrua: dip_load_strider(1,2); dipswitch = active_dipswitch; break;
		case INPTYPE_dynwar:   dip_load_dynwar(2);    dipswitch = active_dipswitch; break;
		case INPTYPE_willow:   dip_load_willow(2);    dipswitch = active_dipswitch; break;
		case INPTYPE_unsquad:  dip_load_unsquad(2);   dipswitch = active_dipswitch; break;
		case INPTYPE_ffight:   dip_load_ffight(2);    dipswitch = active_dipswitch; break;
		case INPTYPE_1941:     dip_load_1941(2);      dipswitch = active_dipswitch; break;
		case INPTYPE_mercs:    dip_load_mercs(2);     dipswitch = active_dipswitch; break;
		case INPTYPE_mtwins:   dip_load_mtwins(2);    dipswitch = active_dipswitch; break;
		case INPTYPE_msword:   dip_load_msword(2);    dipswitch = active_dipswitch; *sx = 270; break;
		case INPTYPE_cawing:   dip_load_cawing(2);    dipswitch = active_dipswitch; *sx = 270; break;
		case INPTYPE_nemo:     dip_load_nemo(2);      dipswitch = active_dipswitch; break;
		case INPTYPE_sf2:      dip_load_sf2(0,2);     dipswitch = active_dipswitch; break;
		case INPTYPE_sf2j:     dip_load_sf2(1,2);     dipswitch = active_dipswitch; break;
		case INPTYPE_3wonders: dip_load_3wonders(2);  dipswitch = active_dipswitch; break;
		case INPTYPE_kod:      dip_load_kod(0,2);     dipswitch = active_dipswitch; break;
		case INPTYPE_kodj:     dip_load_kod(1,2);     dipswitch = active_dipswitch; break;
		case INPTYPE_captcomm: dip_load_captcomm(2);  dipswitch = active_dipswitch; break;
		case INPTYPE_knights:  dip_load_knights(2);   dipswitch = active_dipswitch; break;
		case INPTYPE_varth:    dip_load_varth(2);     dipswitch = active_dipswitch; break;
		case INPTYPE_cworld2j: dip_load_cworld2j(2);  dipswitch = active_dipswitch; break;
		case INPTYPE_qad:      dip_load_qad(0,2);     dipswitch = active_dipswitch; break;
		case INPTYPE_qadj:     dip_load_qad(1,2);     dipswitch = active_dipswitch; break;
		case INPTYPE_qtono2:   dip_load_qtono2(2);   dipswitch = active_dipswitch; break;
		case INPTYPE_megaman:  dip_load_megaman(0,2); dipswitch = active_dipswitch; break;
		case INPTYPE_rockmanj: dip_load_megaman(1,2); dipswitch = active_dipswitch; break;
		case INPTYPE_pnickj:   dip_load_pnickj(2);   dipswitch = active_dipswitch; break;
#if !RELEASE
		case INPTYPE_wofhfh:   dip_load_wofhfh(2);   dipswitch = active_dipswitch; break;
		case INPTYPE_punisherbz:dip_load_punisherbz(2);   dipswitch = active_dipswitch; break;
#endif
		}
	}
	else if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_CHINESE_TRADITIONAL)
	{
		switch (machine_input_type)
		{
		case INPTYPE_forgottn: dip_load_forgottn(3); dipswitch = active_dipswitch; break;
		case INPTYPE_ghouls:   dip_load_ghouls(0,3);  dipswitch = active_dipswitch; break;
		case INPTYPE_ghoulsu:  dip_load_ghouls(1,3);  dipswitch = active_dipswitch; break;
		case INPTYPE_daimakai: dip_load_ghouls(2,3);  dipswitch = active_dipswitch; break;
		case INPTYPE_strider:  dip_load_strider(0,3); dipswitch = active_dipswitch; break;
		case INPTYPE_stridrua: dip_load_strider(1,3); dipswitch = active_dipswitch; break;
		case INPTYPE_dynwar:   dip_load_dynwar(3);   dipswitch = active_dipswitch; break;
		case INPTYPE_willow:   dip_load_willow(3);   dipswitch = active_dipswitch; break;
		case INPTYPE_unsquad:  dip_load_unsquad(3);  dipswitch = active_dipswitch; break;
		case INPTYPE_ffight:   dip_load_ffight(3);   dipswitch = active_dipswitch; break;
		case INPTYPE_1941:     dip_load_1941(3);     dipswitch = active_dipswitch; break;
		case INPTYPE_mercs:    dip_load_mercs(3);    dipswitch = active_dipswitch; break;
		case INPTYPE_mtwins:   dip_load_mtwins(3);   dipswitch = active_dipswitch; break;
		case INPTYPE_msword:   dip_load_msword(3);   dipswitch = active_dipswitch; *sx = 270; break;
		case INPTYPE_cawing:   dip_load_cawing(3);   dipswitch = active_dipswitch; *sx = 270; break;
		case INPTYPE_nemo:     dip_load_nemo(3);     dipswitch = active_dipswitch; break;
		case INPTYPE_sf2:      dip_load_sf2(0,3);     dipswitch = active_dipswitch; break;
		case INPTYPE_sf2j:     dip_load_sf2(1,3);     dipswitch = active_dipswitch; break;
		case INPTYPE_3wonders: dip_load_3wonders(3); dipswitch = active_dipswitch; break;
		case INPTYPE_kod:      dip_load_kod(0,3);     dipswitch = active_dipswitch; break;
		case INPTYPE_kodj:     dip_load_kod(1,3);     dipswitch = active_dipswitch; break;
		case INPTYPE_captcomm: dip_load_captcomm(3); dipswitch = active_dipswitch; break;
		case INPTYPE_knights:  dip_load_knights(3);  dipswitch = active_dipswitch; break;
		case INPTYPE_varth:    dip_load_varth(3);    dipswitch = active_dipswitch; break;
		case INPTYPE_cworld2j: dip_load_cworld2j(3); dipswitch = active_dipswitch; break;
		case INPTYPE_qad:      dip_load_qad(0,3);     dipswitch = active_dipswitch; break;
		case INPTYPE_qadj:     dip_load_qad(1,3);     dipswitch = active_dipswitch; break;
		case INPTYPE_qtono2:   dip_load_qtono2(3);   dipswitch = active_dipswitch; break;
		case INPTYPE_megaman:  dip_load_megaman(0,3); dipswitch = active_dipswitch; break;
		case INPTYPE_rockmanj: dip_load_megaman(1,3); dipswitch = active_dipswitch; break;
		case INPTYPE_pnickj:   dip_load_pnickj(3);   dipswitch = active_dipswitch; break;
#if !RELEASE
		case INPTYPE_wofhfh:   dip_load_wofhfh(3);   dipswitch = active_dipswitch; break;
		case INPTYPE_punisherbz:dip_load_punisherbz(3);   dipswitch = active_dipswitch; break;
#endif
		}
	}
	else
	{
		switch (machine_input_type)
		{
		case INPTYPE_forgottn: dip_load_forgottn(0); dipswitch = active_dipswitch; break;
		case INPTYPE_ghouls:   dip_load_ghouls(0,0);  dipswitch = active_dipswitch; break;
		case INPTYPE_ghoulsu:  dip_load_ghouls(1,0);  dipswitch = active_dipswitch; break;
		case INPTYPE_daimakai: dip_load_ghouls(2,0);  dipswitch = active_dipswitch; break;
		case INPTYPE_strider:  dip_load_strider(0,0); dipswitch = active_dipswitch; break;
		case INPTYPE_stridrua: dip_load_strider(1,0); dipswitch = active_dipswitch; break;
		case INPTYPE_dynwar:   dip_load_dynwar(0);   dipswitch = active_dipswitch; break;
		case INPTYPE_willow:   dip_load_willow(0);   dipswitch = active_dipswitch; break;
		case INPTYPE_unsquad:  dip_load_unsquad(0);  dipswitch = active_dipswitch; break;
		case INPTYPE_ffight:   dip_load_ffight(0);   dipswitch = active_dipswitch; break;
		case INPTYPE_1941:     dip_load_1941(0);     dipswitch = active_dipswitch; break;
		case INPTYPE_mercs:    dip_load_mercs(0);    dipswitch = active_dipswitch; break;
		case INPTYPE_mtwins:   dip_load_mtwins(0);   dipswitch = active_dipswitch; break;
		case INPTYPE_msword:   dip_load_msword(0);   dipswitch = active_dipswitch; *sx = 270; break;
		case INPTYPE_cawing:   dip_load_cawing(0);   dipswitch = active_dipswitch; *sx = 270; break;
		case INPTYPE_nemo:     dip_load_nemo(0);     dipswitch = active_dipswitch; break;
		case INPTYPE_sf2:      dip_load_sf2(0,0);     dipswitch = active_dipswitch; break;
		case INPTYPE_sf2j:     dip_load_sf2(1,0);     dipswitch = active_dipswitch; break;
		case INPTYPE_3wonders: dip_load_3wonders(0); dipswitch = active_dipswitch; break;
		case INPTYPE_kod:      dip_load_kod(0,0);     dipswitch = active_dipswitch; break;
		case INPTYPE_kodj:     dip_load_kod(1,0);     dipswitch = active_dipswitch; break;
		case INPTYPE_captcomm: dip_load_captcomm(0); dipswitch = active_dipswitch; break;
		case INPTYPE_knights:  dip_load_knights(0);  dipswitch = active_dipswitch; break;
		case INPTYPE_varth:    dip_load_varth(0);    dipswitch = active_dipswitch; break;
		case INPTYPE_cworld2j: dip_load_cworld2j(0); dipswitch = active_dipswitch; break;
		case INPTYPE_qad:      dip_load_qad(0,0);     dipswitch = active_dipswitch; break;
		case INPTYPE_qadj:     dip_load_qad(1,0);     dipswitch = active_dipswitch; break;
		case INPTYPE_qtono2:   dip_load_qtono2(0);   dipswitch = active_dipswitch; break;
		case INPTYPE_megaman:  dip_load_megaman(0,0); dipswitch = active_dipswitch; break;
		case INPTYPE_rockmanj: dip_load_megaman(1,0); dipswitch = active_dipswitch; break;
		case INPTYPE_pnickj:   dip_load_pnickj(0);   dipswitch = active_dipswitch; break;
#if !RELEASE
		case INPTYPE_wofhfh:   dip_load_wofhfh(0);   dipswitch = active_dipswitch; break;
		case INPTYPE_punisherbz:dip_load_punisherbz(0);   dipswitch = active_dipswitch; break;
#endif
		}
	}
	return dipswitch;
}


void save_dipswitch(void)
{
	if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_JAPANESE)
	{
		switch (machine_input_type)
		{
		case INPTYPE_forgottn: dip_save_forgottn(1); break;
		case INPTYPE_ghouls:   dip_save_ghouls(0,1); break;
		case INPTYPE_ghoulsu:  dip_save_ghouls(1,1); break;
		case INPTYPE_daimakai: dip_save_ghouls(2,1); break;
		case INPTYPE_strider:  dip_save_strider(0,1); break;
		case INPTYPE_stridrua: dip_save_strider(1,1); break;
		case INPTYPE_dynwar:   dip_save_dynwar(1); break;
		case INPTYPE_willow:   dip_save_willow(1); break;
		case INPTYPE_unsquad:  dip_save_unsquad(1); break;
		case INPTYPE_ffight:   dip_save_ffight(1); break;
		case INPTYPE_1941:     dip_save_1941(1); break;
		case INPTYPE_mercs:    dip_save_mercs(1); break;
		case INPTYPE_mtwins:   dip_save_mtwins(1); break;
		case INPTYPE_msword:   dip_save_msword(1); break;
		case INPTYPE_cawing:   dip_save_cawing(1); break;
		case INPTYPE_nemo:     dip_save_nemo(1); break;
		case INPTYPE_sf2:      dip_save_sf2(0,1); break;
		case INPTYPE_sf2j:     dip_save_sf2(1,1); break;
		case INPTYPE_3wonders: dip_save_3wonders(1); break;
		case INPTYPE_kod:      dip_save_kod(0,1); break;
		case INPTYPE_kodj:     dip_save_kod(1,1); break;
		case INPTYPE_captcomm: dip_save_captcomm(1); break;
		case INPTYPE_knights:  dip_save_knights(1); break;
		case INPTYPE_varth:    dip_save_varth(1); break;
		case INPTYPE_cworld2j: dip_save_cworld2j(1); break;
		case INPTYPE_qad:      dip_save_qad(0,1); break;
		case INPTYPE_qadj:     dip_save_qad(1,1); break;
		case INPTYPE_qtono2:   dip_save_qtono2(1); break;
		case INPTYPE_megaman:  dip_save_megaman(0,1); break;
		case INPTYPE_rockmanj: dip_save_megaman(1,1); break;
		case INPTYPE_pnickj:   dip_save_pnickj(1); break;
#if !RELEASE
		case INPTYPE_wofhfh:   dip_save_wofhfh(1); break;
		case INPTYPE_punisherbz:dip_save_punisherbz(1); break;
#endif
		}
	}
	else if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_CHINESE_SIMPLIFIED)
	{
		switch (machine_input_type)
		{
		case INPTYPE_forgottn: dip_save_forgottn(2); break;
		case INPTYPE_ghouls:   dip_save_ghouls(0,2); break;
		case INPTYPE_ghoulsu:  dip_save_ghouls(1,2); break;
		case INPTYPE_daimakai: dip_save_ghouls(2,2); break;
		case INPTYPE_strider:  dip_save_strider(0,2); break;
		case INPTYPE_stridrua: dip_save_strider(1,2); break;
		case INPTYPE_dynwar:   dip_save_dynwar(2); break;
		case INPTYPE_willow:   dip_save_willow(2); break;
		case INPTYPE_unsquad:  dip_save_unsquad(2); break;
		case INPTYPE_ffight:   dip_save_ffight(2); break;
		case INPTYPE_1941:     dip_save_1941(2); break;
		case INPTYPE_mercs:    dip_save_mercs(2); break;
		case INPTYPE_mtwins:   dip_save_mtwins(2); break;
		case INPTYPE_msword:   dip_save_msword(2); break;
		case INPTYPE_cawing:   dip_save_cawing(2); break;
		case INPTYPE_nemo:     dip_save_nemo(2); break;
		case INPTYPE_sf2:      dip_save_sf2(0,2); break;
		case INPTYPE_sf2j:     dip_save_sf2(1,2); break;
		case INPTYPE_3wonders: dip_save_3wonders(2); break;
		case INPTYPE_kod:      dip_save_kod(0,2); break;
		case INPTYPE_kodj:     dip_save_kod(1,2); break;
		case INPTYPE_captcomm: dip_save_captcomm(2); break;
		case INPTYPE_knights:  dip_save_knights(2); break;
		case INPTYPE_varth:    dip_save_varth(2); break;
		case INPTYPE_cworld2j: dip_save_cworld2j(2); break;
		case INPTYPE_qad:      dip_save_qad(0,2); break;
		case INPTYPE_qadj:     dip_save_qad(1,2); break;
		case INPTYPE_qtono2:   dip_save_qtono2(2); break;
		case INPTYPE_megaman:  dip_save_megaman(0,2); break;
		case INPTYPE_rockmanj: dip_save_megaman(1,2); break;
		case INPTYPE_pnickj:   dip_save_pnickj(2); break;
#if !RELEASE
		case INPTYPE_wofhfh:   dip_save_wofhfh(2); break;
		case INPTYPE_punisherbz:dip_save_punisherbz(2); break;
#endif
		}
	}
	else if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_CHINESE_TRADITIONAL)
	{
		switch (machine_input_type)
		{
		case INPTYPE_forgottn: dip_save_forgottn(3); break;
		case INPTYPE_ghouls:   dip_save_ghouls(0,3); break;
		case INPTYPE_ghoulsu:  dip_save_ghouls(1,3); break;
		case INPTYPE_daimakai: dip_save_ghouls(2,3); break;
		case INPTYPE_strider:  dip_save_strider(0,3); break;
		case INPTYPE_stridrua: dip_save_strider(1,3); break;
		case INPTYPE_dynwar:   dip_save_dynwar(3); break;
		case INPTYPE_willow:   dip_save_willow(3); break;
		case INPTYPE_unsquad:  dip_save_unsquad(3); break;
		case INPTYPE_ffight:   dip_save_ffight(3); break;
		case INPTYPE_1941:     dip_save_1941(3); break;
		case INPTYPE_mercs:    dip_save_mercs(3); break;
		case INPTYPE_mtwins:   dip_save_mtwins(3); break;
		case INPTYPE_msword:   dip_save_msword(3); break;
		case INPTYPE_cawing:   dip_save_cawing(3); break;
		case INPTYPE_nemo:     dip_save_nemo(3); break;
		case INPTYPE_sf2:      dip_save_sf2(0,3); break;
		case INPTYPE_sf2j:     dip_save_sf2(1,3); break;
		case INPTYPE_3wonders: dip_save_3wonders(3); break;
		case INPTYPE_kod:      dip_save_kod(0,3); break;
		case INPTYPE_kodj:     dip_save_kod(1,3); break;
		case INPTYPE_captcomm: dip_save_captcomm(3); break;
		case INPTYPE_knights:  dip_save_knights(3); break;
		case INPTYPE_varth:    dip_save_varth(3); break;
		case INPTYPE_cworld2j: dip_save_cworld2j(3); break;
		case INPTYPE_qad:      dip_save_qad(0,3); break;
		case INPTYPE_qadj:     dip_save_qad(1,3); break;
		case INPTYPE_qtono2:   dip_save_qtono2(3); break;
		case INPTYPE_megaman:  dip_save_megaman(0,3); break;
		case INPTYPE_rockmanj: dip_save_megaman(1,3); break;
		case INPTYPE_pnickj:   dip_save_pnickj(3); break;
#if !RELEASE
		case INPTYPE_wofhfh:   dip_save_wofhfh(3); break;
		case INPTYPE_punisherbz:dip_save_punisherbz(3); break;
#endif
		}
	}
	else
	{
		switch (machine_input_type)
		{
		case INPTYPE_forgottn: dip_save_forgottn(0); break;
		case INPTYPE_ghouls:   dip_save_ghouls(0,0); break;
		case INPTYPE_ghoulsu:  dip_save_ghouls(1,0); break;
		case INPTYPE_daimakai: dip_save_ghouls(2,0); break;
		case INPTYPE_strider:  dip_save_strider(0,0); break;
		case INPTYPE_stridrua: dip_save_strider(1,0); break;
		case INPTYPE_dynwar:   dip_save_dynwar(0); break;
		case INPTYPE_willow:   dip_save_willow(0); break;
		case INPTYPE_unsquad:  dip_save_unsquad(0); break;
		case INPTYPE_ffight:   dip_save_ffight(0); break;
		case INPTYPE_1941:     dip_save_1941(0); break;
		case INPTYPE_mercs:    dip_save_mercs(0); break;
		case INPTYPE_mtwins:   dip_save_mtwins(0); break;
		case INPTYPE_msword:   dip_save_msword(0); break;
		case INPTYPE_cawing:   dip_save_cawing(0); break;
		case INPTYPE_nemo:     dip_save_nemo(0); break;
		case INPTYPE_sf2:      dip_save_sf2(0,0); break;
		case INPTYPE_sf2j:     dip_save_sf2(1,0); break;
		case INPTYPE_3wonders: dip_save_3wonders(0); break;
		case INPTYPE_kod:      dip_save_kod(0,0); break;
		case INPTYPE_kodj:     dip_save_kod(1,0); break;
		case INPTYPE_captcomm: dip_save_captcomm(0); break;
		case INPTYPE_knights:  dip_save_knights(0); break;
		case INPTYPE_varth:    dip_save_varth(0); break;
		case INPTYPE_cworld2j: dip_save_cworld2j(0); break;
		case INPTYPE_qad:      dip_save_qad(0,0); break;
		case INPTYPE_qadj:     dip_save_qad(1,0); break;
		case INPTYPE_qtono2:   dip_save_qtono2(0); break;
		case INPTYPE_megaman:  dip_save_megaman(0,0); break;
		case INPTYPE_rockmanj: dip_save_megaman(1,0); break;
		case INPTYPE_pnickj:   dip_save_pnickj(0); break;
#if !RELEASE
		case INPTYPE_wofhfh:   dip_save_wofhfh(0); break;
		case INPTYPE_punisherbz:dip_save_punisherbz(0); break;
#endif
		}
	}

	dip_metadata_unload(&dip_metadata);
	active_dipswitch = NULL;
}
