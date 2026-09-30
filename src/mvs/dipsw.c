/******************************************************************************

	dipsw.c

	MVS DIP Switch Settings

******************************************************************************/

#include "mvs.h"
#include "common/emulator_options.h"
#include "common/ui_text_driver.h"

#define MENU_BLANK		{ "\n", 0, 0x00, 0, 0, { NULL } }

#define MENU_RETURN_CHS	{ "\267\265\273\330\326\367\262\313\265\245", 1, 0x00, 0, 0, { NULL } }

#define MENU_RETURN_CHT	{ "\267\265\273\330\326\367\262\313\206\316", 1, 0x00, 0, 0, { NULL } }

#define MENU_RETURN_JP	{ "\245\341\245\244\245\363\245\341\245\313\245\345\251`\244\313\221\370\244\353", 1, 0x00, 0, 0, { NULL } }

#define MENU_RETURN		{ "Return to main menu", 1, 0x00, 0, 0, { NULL } }

#define MENU_END		{ "\0", 0, 0x00, 0, 0, { NULL } }


/******************************************************************************
	Global Variables
******************************************************************************/

int neogeo_hard_dipsw;


/******************************************************************************
	Local Structures
******************************************************************************/

/*--------------------------------------
  Standard
--------------------------------------*/

static dipswitch_t dipswitch_default[] =
{
	{ "Test Switch",				1, 0x01, 0, 1, { "Off","On" } },
	{ "Coin Chutes",				1, 0x02, 0, 1, { "1", "2" } },
	{ "Autofire (in some games)",	1, 0x04, 0, 1, { "Off","On" } },
	{ "COMM Settings",				1, 0x38, 0, 4, { "Off","1","2","3","4" } },
	{ "Free Play",					1, 0x40, 0, 1, { "Off","On" } },
	{ "Freeze",						1, 0x80, 0, 1, { "Off","On" } },
	MENU_BLANK,
	MENU_RETURN,
	MENU_END,
};
static dipswitch_t dipswitch_default_jp[] =
{
	{ "Test Switch",				1, 0x01, 0, 1, { "Off","On" } },
	{ "Coin Chutes",				1, 0x02, 0, 1, { "1", "2" } },
	{ "Autofire (in some games)",	1, 0x04, 0, 1, { "Off","On" } },
	{ "COMM Settings",				1, 0x38, 0, 4, { "Off","1","2","3","4" } },
	{ "Free Play",					1, 0x40, 0, 1, { "Off","On" } },
	{ "Freeze",						1, 0x80, 0, 1, { "Off","On" } },
	MENU_BLANK,
	MENU_RETURN_JP,
	MENU_END,
};
static dipswitch_t dipswitch_default_chs[] =
{
	{ "\262\342\312\324\277\252\271\330",					1, 0x01, 0, 1, { "\271\330","\277\252" } },
	{ "\315\266\261\322\262\333",						1, 0x02, 0, 1, { "1", "2" } },
	{ "\327\324\266\257\301\254\267\242(\262\277\267\326\323\316\317\267)",			1, 0x04, 0, 1, { "\271\330","\277\252" } },
	{ "\301\252\273\372\311\350\326\303",					1, 0x38, 0, 4, { "\271\330","1","2","3","4" } },
	{ "\303\342\267\321\323\316\315\346",					1, 0x40, 0, 1, { "\271\330","\277\252" } },
	{ "\313\370\266\250",						1, 0x80, 0, 1, { "\271\330","\277\252" } },
	MENU_BLANK,
	MENU_RETURN_CHS,
	MENU_END,
};
static dipswitch_t dipswitch_default_cht[] =
{
	{ "\234y\324\207\351_\352P",					1, 0x01, 0, 1, { "\352P","\351_" } },
	{ "\315\266\216\305\262\333",						1, 0x02, 0, 1, { "1", "2" } },
	{ "\327\324\204\323\337B\260l(\262\277\267\326\337[\221\362)",			1, 0x04, 0, 1, { "\352P","\351_" } },
	{ "\302\223\231C\324O\326\303",					1, 0x38, 0, 4, { "\352P","1","2","3","4" } },
	{ "\303\342\331M\337[\315\346",					1, 0x40, 0, 1, { "\352P","\351_" } },
	{ "\346i\266\250",						1, 0x80, 0, 1, { "\352P","\351_" } },
	MENU_BLANK,
	MENU_RETURN_CHT,
	MENU_END,
};

/*--------------------------------------
  PCB
--------------------------------------*/

static dipswitch_t dipswitch_pcb[] =
{
	{ "Test Switch",				1, 0x01, 0, 1, { "Off","On" } },
	{ "Coin Chutes",				1, 0x02, 0, 1, { "1", "2" } },
	{ "Autofire (in some games)",	1, 0x04, 0, 1, { "Off","On" } },
	{ "COMM Settings",				1, 0x38, 0, 4, { "Off","1","2","3","4" } },
	{ "Free Play",					1, 0x40, 0, 1, { "Off","On" } },
	{ "Freeze",						1, 0x80, 0, 1, { "Off","On" } },
	{ "Hard Dip 3 (Region)",		1, 0x01, 0, 1, { "Asia","Japan" } },
	MENU_BLANK,
	MENU_RETURN,
	MENU_END,
};
static dipswitch_t dipswitch_pcb_jp[] =
{
	{ "Test Switch",				1, 0x01, 0, 1, { "Off","On" } },
	{ "Coin Chutes",				1, 0x02, 0, 1, { "1", "2" } },
	{ "Autofire (in some games)",	1, 0x04, 0, 1, { "Off","On" } },
	{ "COMM Settings",				1, 0x38, 0, 4, { "Off","1","2","3","4" } },
	{ "Free Play",					1, 0x40, 0, 1, { "Off","On" } },
	{ "Freeze",						1, 0x80, 0, 1, { "Off","On" } },
	{ "Hard Dip 3 (Region)",		1, 0x01, 0, 1, { "Asia","Japan" } },
	MENU_BLANK,
	MENU_RETURN_JP,
	MENU_END,
};
static dipswitch_t dipswitch_pcb_chs[] =
{
	{ "\262\342\312\324\277\252\271\330",					1, 0x01, 0, 1, { "\271\330","\277\252" } },
	{ "\315\266\261\322\262\333",						1, 0x02, 0, 1, { "1", "2" } },
	{ "\327\324\266\257\301\254\267\242(\262\277\267\326\323\316\317\267)",			1, 0x04, 0, 1, { "\271\330","\277\252" } },
	{ "\301\252\273\372\311\350\326\303",					1, 0x38, 0, 4, { "\271\330","1","2","3","4" } },
	{ "\303\342\267\321\323\316\315\346",					1, 0x40, 0, 1, { "\271\330","\277\252" } },
	{ "\313\370\266\250",						1, 0x80, 0, 1, { "\271\330","\277\252" } },
	{ "\323\262\274\376Dip 3(\307\370\323\362)",			1, 0x01, 0, 1, { "\321\307\260\346","\310\325\260\346" } },
	MENU_BLANK,
	MENU_RETURN_CHS,
	MENU_END,
};
static dipswitch_t dipswitch_pcb_cht[] =
{
	{ "\234y\324\207\351_\352P",					1, 0x01, 0, 1, { "\352P","\351_" } },
	{ "\315\266\216\305\262\333",						1, 0x02, 0, 1, { "1", "2" } },
	{ "\327\324\204\323\337B\260l(\262\277\267\326\337[\221\362)",			1, 0x04, 0, 1, { "\352P","\351_" } },
	{ "\302\223\231C\324O\326\303",					1, 0x38, 0, 4, { "\352P","1","2","3","4" } },
	{ "\303\342\331M\337[\315\346",					1, 0x40, 0, 1, { "\352P","\351_" } },
	{ "\346i\266\250",						1, 0x80, 0, 1, { "\352P","\351_" } },
	{ "\323\262\274\376Dip 3(\205^\323\362)",			1, 0x01, 0, 1, { "\201\206\260\346","\310\325\260\346" } },
	MENU_BLANK,
	MENU_RETURN_CHT,
	MENU_END,
};

/*--------------------------------------
  Mahjong
--------------------------------------*/

static dipswitch_t dipswitch_mjneogeo[] =
{	{ "Test Switch",				1, 0x01, 0, 1, { "Off","On" } },
	{ "Coin Chutes",				1, 0x02, 0, 1, { "1", "2" } },
	{ "Mahjong Control Panel",		0, 0x04, 0, 1, { "Off","On" } },
	{ "COMM Settings",				1, 0x38, 0, 4, { "Off","1","2","3","4" } },
	{ "Free Play",					1, 0x40, 0, 1, { "Off","On" } },
	{ "Freeze",						1, 0x80, 0, 1, { "Off","On" } },
	MENU_BLANK,
	MENU_RETURN,
	MENU_END,
};
static dipswitch_t dipswitch_mjneogeo_jp[] =
{
	{ "Test Switch",				1, 0x01, 0, 1, { "Off","On" } },
	{ "Coin Chutes",				1, 0x02, 0, 1, { "1", "2" } },
	{ "Mahjong Control Panel",		0, 0x04, 0, 1, { "Off","On" } },
	{ "COMM Settings",				1, 0x38, 0, 4, { "Off","1","2","3","4" } },
	{ "Free Play",					1, 0x40, 0, 1, { "Off","On" } },
	{ "Freeze",						1, 0x80, 0, 1, { "Off","On" } },
	MENU_BLANK,
	MENU_RETURN_JP,
	MENU_END,
};
static dipswitch_t dipswitch_mjneogeo_chs[] =
{
	{ "\262\342\312\324\277\252\271\330",					1, 0x01, 0, 1, { "\271\330","\277\252" } },
	{ "\315\266\261\322\262\333",						1, 0x02, 0, 1, { "1", "2" } },
	{ "\302\351\275\253\262\331\327\367\260\346",					1, 0x04, 0, 1, { "\271\330","\277\252" } },
	{ "\301\252\273\372\311\350\326\303",					1, 0x38, 0, 4, { "\271\330","1","2","3","4" } },
	{ "\303\342\267\321\323\316\315\346",					1, 0x40, 0, 1, { "\271\330","\277\252" } },
	{ "\313\370\266\250",						1, 0x80, 0, 1, { "\271\330","\277\252" } },
	MENU_BLANK,
	MENU_RETURN_CHS,
	MENU_END,
};
static dipswitch_t dipswitch_mjneogeo_cht[] =
{
	{ "\234y\324\207\351_\352P",					1, 0x01, 0, 1, { "\352P","\351_" } },
	{ "\315\266\216\305\262\333",						1, 0x02, 0, 1, { "1", "2" } },
	{ "\302\351\214\242\262\331\327\367\260\346",					1, 0x04, 0, 1, { "\352P","\351_" } },
	{ "\302\223\231C\324O\326\303",					1, 0x38, 0, 4, { "\352P","1","2","3","4" } },
	{ "\303\342\331M\337[\315\346",					1, 0x40, 0, 1, { "\352P","\351_" } },
	{ "\346i\266\250",						1, 0x80, 0, 1, { "\352P","\351_" } },
	MENU_BLANK,
	MENU_RETURN_CHT,
	MENU_END,
};


/*--------------------------------------
  kog
--------------------------------------*/

#if !RELEASE
static dipswitch_t dipswitch_kog[] =
{
	{ "Test Switch",				1, 0x01, 0, 1, { "Off","On" } },
	{ "Coin Chutes",				1, 0x02, 0, 1, { "1", "2" } },
	{ "Autofire (in some games)",	1, 0x04, 0, 1, { "Off","On" } },
	{ "COMM Settings",				1, 0x38, 0, 4, { "Off","1","2","3","4" } },
	{ "Free Play",					1, 0x40, 0, 1, { "Off","On" } },
	{ "Freeze",						1, 0x80, 0, 1, { "Off","On" } },
	{ "Title Language",				1, 0x01, 0, 1, { "Chinese","English" } },
	MENU_BLANK,
	MENU_RETURN,
	MENU_END,
};
static dipswitch_t dipswitch_kog_jp[] =
{
	{ "Test Switch",				1, 0x01, 0, 1, { "Off","On" } },
	{ "Coin Chutes",				1, 0x02, 0, 1, { "1", "2" } },
	{ "Mahjong Control Panel",		0, 0x04, 0, 1, { "Off","On" } },
	{ "COMM Settings",				1, 0x38, 0, 4, { "Off","1","2","3","4" } },
	{ "Free Play",					1, 0x40, 0, 1, { "Off","On" } },
	{ "Freeze",						1, 0x80, 0, 1, { "Off","On" } },
	{ "Title Language",				1, 0x01, 0, 1, { "Chinese","English" } },
	MENU_BLANK,
	MENU_RETURN_JP,
	MENU_END,
};
static dipswitch_t dipswitch_kog_chs[] =
{
	{ "\262\342\312\324\277\252\271\330",					1, 0x01, 0, 1, { "\271\330","\277\252" } },
	{ "\315\266\261\322\262\333",						1, 0x02, 0, 1, { "1", "2" } },
	{ "\327\324\266\257\301\254\267\242(\262\277\267\326\323\316\317\267)",			1, 0x04, 0, 1, { "\271\330","\277\252" } },
	{ "\301\252\273\372\311\350\326\303",					1, 0x38, 0, 4, { "\271\330","1","2","3","4" } },
	{ "\303\342\267\321\323\316\315\346",					1, 0x40, 0, 1, { "\271\330","\277\252" } },
	{ "\313\370\266\250",						1, 0x80, 0, 1, { "\271\330","\277\252" } },
	{ "\261\352\314\342\323\357\321\324",					1, 0x01, 0, 1, { "\326\320\316\304","\323\242\316\304" } },
	MENU_BLANK,
	MENU_RETURN_CHS,
	MENU_END,
};
static dipswitch_t dipswitch_kog_cht[] =
{
	{ "\234y\324\207\351_\352P",					1, 0x01, 0, 1, { "\352P","\351_" } },
	{ "\315\266\216\305\262\333",						1, 0x02, 0, 1, { "1", "2" } },
	{ "\327\324\204\323\337B\260l(\262\277\267\326\337[\221\362)",			1, 0x04, 0, 1, { "\352P","\351_" } },
	{ "\302\223\231C\324O\326\303",					1, 0x38, 0, 4, { "\352P","1","2","3","4" } },
	{ "\303\342\331M\337[\315\346",					1, 0x40, 0, 1, { "\352P","\351_" } },
	{ "\346i\266\250",						1, 0x80, 0, 1, { "\352P","\351_" } },
	{ "\230\313\356}\325Z\321\324",					1, 0x01, 0, 1, { "\326\320\316\304","\323\242\316\304" } },
	MENU_BLANK,
	MENU_RETURN_CHT,
	MENU_END,
};
#endif


dipswitch_t *load_dipswitch(void)
{
	uint8_t value = ~neogeo_dipswitch;
	dipswitch_t *dipswitch = NULL;
	if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_JAPANESE)
	{
		switch (neogeo_ngh)
		{
		case NGH_mahretsu:
		case NGH_janshin:
		case NGH_minasan:
		case NGH_bakatono:
		case NGH_fr2ch:
			dipswitch = dipswitch_mjneogeo_jp;
			break;

		default:
			dipswitch = dipswitch_default_jp;
			break;
		}

		if (machine_init_type == INIT_ms5pcb
		||	machine_init_type == INIT_svcpcb)
		{
			dipswitch = dipswitch_pcb_jp;
			dipswitch[6].value = neogeo_hard_dipsw;
		}
#if !RELEASE
		else if (machine_init_type == INIT_kog)
		{
			dipswitch = dipswitch_kog_jp;
			dipswitch[6].value = neogeo_hard_dipsw;
		}
#endif
	}
	else if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_CHINESE_SIMPLIFIED)
	{
		switch (neogeo_ngh)
		{
		case NGH_mahretsu:
		case NGH_janshin:
		case NGH_minasan:
		case NGH_bakatono:
		case NGH_fr2ch:
			dipswitch = dipswitch_mjneogeo_chs;
			break;

		default:
			dipswitch = dipswitch_default_chs;
			break;
		}

		if (machine_init_type == INIT_ms5pcb
		||	machine_init_type == INIT_svcpcb)
		{
			dipswitch = dipswitch_pcb_chs;
			dipswitch[6].value = neogeo_hard_dipsw;
		}
#if !RELEASE
		else if (machine_init_type == INIT_kog)
		{
			dipswitch = dipswitch_kog_chs;
			dipswitch[6].value = neogeo_hard_dipsw;
		}
#endif
	}
	else if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_CHINESE_TRADITIONAL)
	{
		switch (neogeo_ngh)
		{
		case NGH_mahretsu:
		case NGH_janshin:
		case NGH_minasan:
		case NGH_bakatono:
		case NGH_fr2ch:
			dipswitch = dipswitch_mjneogeo_cht;
			break;

		default:
			dipswitch = dipswitch_default_cht;
			break;
		}

		if (machine_init_type == INIT_ms5pcb
		||	machine_init_type == INIT_svcpcb)
		{
			dipswitch = dipswitch_pcb_cht;
			dipswitch[6].value = neogeo_hard_dipsw;
		}
#if !RELEASE
		else if (machine_init_type == INIT_kog)
		{
			dipswitch = dipswitch_kog_cht;
			dipswitch[6].value = neogeo_hard_dipsw;
		}
#endif
	}
	else
	{
		switch (neogeo_ngh)
		{
		case NGH_mahretsu:
		case NGH_janshin:
		case NGH_minasan:
		case NGH_bakatono:
		case NGH_fr2ch:
			dipswitch = dipswitch_mjneogeo;
			break;

		default:
			dipswitch = dipswitch_default;
			break;
	}

		if (machine_init_type == INIT_ms5pcb
		||	machine_init_type == INIT_svcpcb)
		{
			dipswitch = dipswitch_pcb;
			dipswitch[6].value = neogeo_hard_dipsw;
		}
#if !RELEASE
		else if (machine_init_type == INIT_kog)
		{
			dipswitch = dipswitch_kog;
			dipswitch[6].value = neogeo_hard_dipsw;
		}
#endif
	}

	dipswitch[0].value = (value & 0x01) != 0;
	dipswitch[1].value = (value & 0x02) != 0;
	dipswitch[2].value = (value & 0x04) != 0;
	dipswitch[4].value = (value & 0x40) != 0;
	dipswitch[5].value = (value & 0x80) != 0;

	switch (neogeo_dipswitch & 0x38)
	{
	case 0x00: dipswitch[3].value = 4; break;
	case 0x10: dipswitch[3].value = 3; break;
	case 0x20: dipswitch[3].value = 2; break;
	case 0x30: dipswitch[3].value = 1; break;
	case 0x38: dipswitch[3].value = 0; break;
	}

	return dipswitch;
}


void save_dipswitch(void)
{
	uint8_t value;
	dipswitch_t *dipswitch = NULL;
	if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_JAPANESE)
	{
		switch (neogeo_ngh)
		{
		case NGH_mahretsu:
		case NGH_janshin:
		case NGH_minasan:
		case NGH_bakatono:
		case NGH_fr2ch:
			dipswitch = dipswitch_mjneogeo_jp;
			break;

		default:
			dipswitch = dipswitch_default_jp;
			break;
		}

		if (machine_init_type == INIT_ms5pcb
		||	machine_init_type == INIT_svcpcb)
		{
			dipswitch = dipswitch_pcb_jp;
			neogeo_hard_dipsw = dipswitch[6].value;
		}
#if !RELEASE
		else if (machine_init_type == INIT_kog)
		{
			dipswitch = dipswitch_kog_jp;
			neogeo_hard_dipsw = dipswitch[6].value;
		}
#endif
	}
	else if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_CHINESE_SIMPLIFIED)
	{
		switch (neogeo_ngh)
		{
		case NGH_mahretsu:
		case NGH_janshin:
		case NGH_minasan:
		case NGH_bakatono:
		case NGH_fr2ch:
			dipswitch = dipswitch_mjneogeo_chs;
			break;

		default:
			dipswitch = dipswitch_default_chs;
			break;
		}

		if (machine_init_type == INIT_ms5pcb
		||	machine_init_type == INIT_svcpcb)
		{
			dipswitch = dipswitch_pcb_chs;
			neogeo_hard_dipsw = dipswitch[6].value;
		}
#if !RELEASE
		else if (machine_init_type == INIT_kog)
		{
			dipswitch = dipswitch_kog_chs;
			neogeo_hard_dipsw = dipswitch[6].value;
		}
#endif
	}
	else if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_CHINESE_TRADITIONAL)
	{
		switch (neogeo_ngh)
		{
		case NGH_mahretsu:
		case NGH_janshin:
		case NGH_minasan:
		case NGH_bakatono:
		case NGH_fr2ch:
			dipswitch = dipswitch_mjneogeo_cht;
			break;

		default:
			dipswitch = dipswitch_default_cht;
			break;
		}

		if (machine_init_type == INIT_ms5pcb
		||	machine_init_type == INIT_svcpcb)
		{
			dipswitch = dipswitch_pcb_cht;
			neogeo_hard_dipsw = dipswitch[6].value;
		}
#if !RELEASE
		else if (machine_init_type == INIT_kog)
		{
			dipswitch = dipswitch_kog_cht;
			neogeo_hard_dipsw = dipswitch[6].value;
		}
#endif
	}
	else
	{
		switch (neogeo_ngh)
		{
		case NGH_mahretsu:
		case NGH_janshin:
		case NGH_minasan:
		case NGH_bakatono:
		case NGH_fr2ch:
			dipswitch = dipswitch_mjneogeo;
			break;

		default:
			dipswitch = dipswitch_default;
			break;
		}

		if (machine_init_type == INIT_ms5pcb
		||	machine_init_type == INIT_svcpcb)
		{
			dipswitch = dipswitch_pcb;
			neogeo_hard_dipsw = dipswitch[6].value;
		}
#if !RELEASE
		else if (machine_init_type == INIT_kog)
		{
			dipswitch = dipswitch_kog;
			neogeo_hard_dipsw = dipswitch[6].value;
		}
#endif
	}
	value = 0;
	value |= (dipswitch[0].value != 0) ? 0x00: 0x01;
	value |= (dipswitch[1].value != 0) ? 0x00: 0x02;
	value |= (dipswitch[2].value != 0) ? 0x00: 0x04;
	value |= (dipswitch[4].value != 0) ? 0x00: 0x40;
	value |= (dipswitch[5].value != 0) ? 0x00: 0x80;
	switch (dipswitch[3].value)
	{
	case 0: value |= 0x38; break;
	case 1: value |= 0x30; break;
	case 2: value |= 0x20; break;
	case 3: value |= 0x10; break;
	case 4: value |= 0x00; break;
	}

	neogeo_dipswitch = value;
}
