/******************************************************************************

	inptport.c

	CPS2 入力ポートエミュレーション

******************************************************************************/

#include "cps2.h"


/******************************************************************************
	グローバル変数
******************************************************************************/

int option_controller;
uint16_t cps2_port_value[CPS2_PORT_MAX];

int input_map[MAX_INPUTS];
int input_max_players;
int input_max_buttons;
int input_coin_chuter;
int analog_sensitivity;
int af_interval = 1;


/******************************************************************************
	ローカル変数
******************************************************************************/

static uint8_t ALIGN_DATA input_flag[MAX_INPUTS];
static int ALIGN_DATA af_map1[CPS2_BUTTON_MAX];
static int ALIGN_DATA af_map2[CPS2_BUTTON_MAX];
static int ALIGN_DATA af_counter[CPS2_BUTTON_MAX];
static int input_analog_value[2];
static int input_ui_wait;
static int service_switch;
static int p12_start_pressed;

static uint8_t max_players[COIN_MAX] =
{
	2,	// COIN_NONE: 2P 2シューター固定 (チェック必要なし)

	2,	// COIN_2P1C: 2P 1シューター
	2,	// COIN_2P2C: 2P 2シューター

	3,	// COIN_3P1C: 3P 1シューター
	3,	// COIN_3P2C: 3P 2シューター
	3,	// COIN_3P3C: 3P 3シューター

	4,	// COIN_4P1C: 4P 1シューター
	4,	// COIN_4P2C: 4P 2シューター
	4	// COIN_4P4C: 4P 4シューター
};

static uint8_t coin_chuter[COIN_MAX][4] =
{
	{ 1, 2, 0, 0 },	// COIN_NONE: 2P 2シューター固定 (チェック必要なし)

	{ 1, 1, 0, 0 },	// COIN_2P1C: 2P 1シューター
	{ 1, 2, 0, 0 },	// COIN_2P2C: 2P 2シューター

	{ 1, 1, 1, 0 },	// COIN_3P1C: 3P 1シューター
	{ 1, 1, 2, 0 },	// COIN_3P2C: 3P 2シューター
	{ 1, 2, 3, 0 },	// COIN_3P3C: 3P 3シューター

	{ 1, 1, 1, 1 },	// COIN_4P1C: 4P 1シューター
	{ 1, 1, 2, 2 },	// COIN_3P2C: 4P 2シューター
	{ 1, 2, 3, 4 }	// COIN_3P3C: 4P 4シューター
};


/******************************************************************************
	ローカル関数
******************************************************************************/

/*------------------------------------------------------
	EEPROMのコイン設定をチェック
------------------------------------------------------*/

static void check_eeprom_settings(int popup)
{
	uint8_t eeprom_value = EEPROM_read_data(driver->inp_eeprom);
	uint8_t coin_type = driver->inp_eeprom_value[eeprom_value];

	if (input_coin_chuter != coin_type)
	{
		input_coin_chuter = coin_type;
		input_max_players = max_players[coin_type];

		if (option_controller >= input_max_players)
		{
			option_controller = INPUT_PLAYER1;
			if (popup) ui_popup(TEXT(CONTROLLER_PLAYER1));
		}
	}
}


/*------------------------------------------------------
	連射フラグを更新
------------------------------------------------------*/

static uint32_t update_autofire(uint32_t buttons)
{
	int i;

	for (i = 0; i < input_max_buttons; i++)
	{
		if (af_map1[i])
		{
			if (buttons & af_map1[i])
			{
				buttons &= ~af_map1[i];

				if (af_counter[i] == 0)
					buttons |= af_map2[i];
				else
					buttons &= ~af_map2[i];

				if (++af_counter[i] > af_interval)
					af_counter[i] = 0;
			}
			else
			{
				af_counter[i] = 0;
			}
		}
	}

	return buttons;
}


/*------------------------------------------------------
	CPS2 ポート0 (コントローラ1 / 2)
------------------------------------------------------*/

static void update_inputport0(void)
{
	uint16_t value = 0xffff;

	switch (machine_input_type)
	{
	case INPTYPE_19xx:
	case INPTYPE_batcir:
		if (option_controller == INPUT_PLAYER1)
		{
			if (input_flag[P1_RIGHT])   value &= ~0x0001;
			if (input_flag[P1_LEFT])    value &= ~0x0002;
			if (input_flag[P1_DOWN])    value &= ~0x0004;
			if (input_flag[P1_UP])      value &= ~0x0008;
			if (input_flag[P1_BUTTON1]) value &= ~0x0010;
			if (input_flag[P1_BUTTON2]) value &= ~0x0020;
			if (input_flag[P1_12])		 value &= ~0x0030;
		}
		else if (option_controller == INPUT_PLAYER2)
		{
			if (input_flag[P1_RIGHT])   value &= ~0x0100;
			if (input_flag[P1_LEFT])    value &= ~0x0200;
			if (input_flag[P1_DOWN])    value &= ~0x0400;
			if (input_flag[P1_UP])      value &= ~0x0800;
			if (input_flag[P1_BUTTON1]) value &= ~0x1000;
			if (input_flag[P1_BUTTON2]) value &= ~0x2000;
			if (input_flag[P1_12])		 value &= ~0x3000;
		}
		break;

	case INPTYPE_cps2:
	case INPTYPE_cybots:
	case INPTYPE_ssf2:
	case INPTYPE_avsp:
	case INPTYPE_sgemf:
	case INPTYPE_daimahoo:
		if (option_controller == INPUT_PLAYER1)
		{
			if (input_flag[P1_RIGHT])   value &= ~0x0001;
			if (input_flag[P1_LEFT])    value &= ~0x0002;
			if (input_flag[P1_DOWN])    value &= ~0x0004;
			if (input_flag[P1_UP])      value &= ~0x0008;
			if (input_flag[P1_BUTTON1]) value &= ~0x0010;
			if (input_flag[P1_BUTTON2]) value &= ~0x0020;
			if (input_flag[P1_BUTTON3]) value &= ~0x0040;
			if (input_flag[P1_12])		 value &= ~0x0030;
			if (input_flag[P1_13])		 value &= ~0x0050;
			if (input_flag[P1_23])		 value &= ~0x0060;
			if (input_flag[P1_123])     value &= ~0x0070;
		}
		else if (option_controller == INPUT_PLAYER2)
		{
			if (input_flag[P1_RIGHT])   value &= ~0x0100;
			if (input_flag[P1_LEFT])    value &= ~0x0200;
			if (input_flag[P1_DOWN])    value &= ~0x0400;
			if (input_flag[P1_UP])      value &= ~0x0800;
			if (input_flag[P1_BUTTON1]) value &= ~0x1000;
			if (input_flag[P1_BUTTON2]) value &= ~0x2000;
			if (input_flag[P1_BUTTON3]) value &= ~0x4000;
			if (input_flag[P1_12])		 value &= ~0x3000;
			if (input_flag[P1_13])		 value &= ~0x5000;
			if (input_flag[P1_23])		 value &= ~0x6000;
			if (input_flag[P1_123])     value &= ~0x7000;
		}
		break;

	case INPTYPE_ddtod:
	case INPTYPE_pzloop2:
		if (option_controller == INPUT_PLAYER1)
		{
			if (input_flag[P1_RIGHT])   value &= ~0x0001;
			if (input_flag[P1_LEFT])    value &= ~0x0002;
			if (input_flag[P1_DOWN])    value &= ~0x0004;
			if (input_flag[P1_UP])      value &= ~0x0008;
			if (input_flag[P1_BUTTON1]) value &= ~0x0010;
			if (input_flag[P1_BUTTON2]) value &= ~0x0020;
			if (input_flag[P1_BUTTON3]) value &= ~0x0040;
			if (input_flag[P1_BUTTON4]) value &= ~0x0080;
			if (input_flag[P1_12])		 value &= ~0x0030;
			if (input_flag[P1_13])		 value &= ~0x0050;
			if (input_flag[P1_14])		 value &= ~0x0090;
			if (input_flag[P1_23])		 value &= ~0x0060;
			if (input_flag[P1_24])		 value &= ~0x00a0;
			if (input_flag[P1_34])		 value &= ~0x00c0;
			if (input_flag[P1_123])	 value &= ~0x0070;
			if (input_flag[P1_124])	 value &= ~0x00b0;
			if (input_flag[P1_134])	 value &= ~0x00d0;
			if (input_flag[P1_234])	 value &= ~0x00e0;
			if (input_flag[P1_1234])	 value &= ~0x00f0;
		}
		else if (option_controller == INPUT_PLAYER2)
		{
			if (input_flag[P1_RIGHT])   value &= ~0x0100;
			if (input_flag[P1_LEFT])    value &= ~0x0200;
			if (input_flag[P1_DOWN])    value &= ~0x0400;
			if (input_flag[P1_UP])      value &= ~0x0800;
			if (input_flag[P1_BUTTON1]) value &= ~0x1000;
			if (input_flag[P1_BUTTON2]) value &= ~0x2000;
			if (input_flag[P1_BUTTON3]) value &= ~0x4000;
			if (input_flag[P1_BUTTON4]) value &= ~0x8000;
			if (input_flag[P1_12])		 value &= ~0x3000;
			if (input_flag[P1_13])		 value &= ~0x5000;
			if (input_flag[P1_14])		 value &= ~0x9000;
			if (input_flag[P1_23])		 value &= ~0x6000;
			if (input_flag[P1_24])		 value &= ~0xa000;
			if (input_flag[P1_34])		 value &= ~0xc000;
			if (input_flag[P1_123])	 value &= ~0x7000;
			if (input_flag[P1_124])	 value &= ~0xb000;
			if (input_flag[P1_134])	 value &= ~0xd000;
			if (input_flag[P1_234])	 value &= ~0xe000;
			if (input_flag[P1_1234])	 value &= ~0xf000;
		}
		break;

	case INPTYPE_qndream:
		if (option_controller == INPUT_PLAYER1)
		{
			if (input_flag[P1_BUTTON4]) value &= ~0x0001;
			if (input_flag[P1_BUTTON3]) value &= ~0x0002;
			if (input_flag[P1_BUTTON2]) value &= ~0x0004;
			if (input_flag[P1_BUTTON1]) value &= ~0x0008;
			if (input_flag[P1_12])		 value &= ~0x000c;
			if (input_flag[P1_13])		 value &= ~0x000a;
			if (input_flag[P1_14])		 value &= ~0x0009;
			if (input_flag[P1_23])		 value &= ~0x0006;
			if (input_flag[P1_24])		 value &= ~0x0005;
			if (input_flag[P1_34])		 value &= ~0x0003;
			if (input_flag[P1_123])	 value &= ~0x000e;
			if (input_flag[P1_124])	 value &= ~0x000d;
			if (input_flag[P1_134])	 value &= ~0x000b;
			if (input_flag[P1_234])	 value &= ~0x0007;
			if (input_flag[P1_1234])	 value &= ~0x000f;
		}
		else if (option_controller == INPUT_PLAYER2)
		{
			if (input_flag[P1_BUTTON4]) value &= ~0x0100;
			if (input_flag[P1_BUTTON3]) value &= ~0x0200;
			if (input_flag[P1_BUTTON2]) value &= ~0x0400;
			if (input_flag[P1_BUTTON1]) value &= ~0x0800;
			if (input_flag[P1_12])		 value &= ~0x0c00;
			if (input_flag[P1_13])		 value &= ~0x0a00;
			if (input_flag[P1_14])		 value &= ~0x0900;
			if (input_flag[P1_23])		 value &= ~0x0600;
			if (input_flag[P1_24])		 value &= ~0x0500;
			if (input_flag[P1_34])		 value &= ~0x0300;
			if (input_flag[P1_123])	 value &= ~0x0e00;
			if (input_flag[P1_124])	 value &= ~0x0d00;
			if (input_flag[P1_134])	 value &= ~0x0b00;
			if (input_flag[P1_234])	 value &= ~0x0700;
			if (input_flag[P1_1234])	 value &= ~0x0f00;
		}
		break;
	}

#ifdef ADHOC
	if (adhoc_enable)
		send_data.port_value[0] = value;
	else
#endif
		cps2_port_value[0] = value;
}


/*------------------------------------------------------
	CPS2 ポート1 (コントローラ3 / 4 / 追加ボタン)
------------------------------------------------------*/

static void update_inputport1(void)
{
	uint16_t value = 0xffff;

	switch (machine_input_type)
	{
	case INPTYPE_cybots:
		if (option_controller == INPUT_PLAYER1)
		{
			if (input_flag[P1_BUTTON4]) value &= ~0x0001;
		}
		else if (option_controller == INPUT_PLAYER2)
		{
			if (input_flag[P1_BUTTON4]) value &= ~0x0100;
		}
		break;

	case INPTYPE_cps2:
	case INPTYPE_ssf2:
		if (option_controller == INPUT_PLAYER1)
		{
			if (input_flag[P1_BUTTON4]) value &= ~0x0001;
			if (input_flag[P1_BUTTON5]) value &= ~0x0002;
			if (input_flag[P1_BUTTON6]) value &= ~0x0004;
			if (input_flag[P1_456])     value &= ~0x0007;
		}
		else if (option_controller == INPUT_PLAYER2)
		{
			if (input_flag[P1_BUTTON4]) value &= ~0x0010;
			if (input_flag[P1_BUTTON5]) value &= ~0x0020;
			if (input_flag[P1_456])     value &= ~0x0030;
		}
		break;

	case INPTYPE_batcir:
		if (option_controller == INPUT_PLAYER3)
		{
			if (input_flag[P1_RIGHT])   value &= ~0x0001;
			if (input_flag[P1_LEFT])    value &= ~0x0002;
			if (input_flag[P1_DOWN])    value &= ~0x0004;
			if (input_flag[P1_UP])      value &= ~0x0008;
			if (input_flag[P1_BUTTON1]) value &= ~0x0010;
			if (input_flag[P1_BUTTON2]) value &= ~0x0020;
			if (input_flag[P1_12])		 value &= ~0x0030;
		}
		else if (option_controller == INPUT_PLAYER4)
		{
			if (input_flag[P1_RIGHT])   value &= ~0x0100;
			if (input_flag[P1_LEFT])    value &= ~0x0200;
			if (input_flag[P1_DOWN])    value &= ~0x0400;
			if (input_flag[P1_UP])      value &= ~0x0800;
			if (input_flag[P1_BUTTON1]) value &= ~0x1000;
			if (input_flag[P1_BUTTON2]) value &= ~0x2000;
			if (input_flag[P1_12])		 value &= ~0x3000;
		}
		break;

	case INPTYPE_avsp:
		if (option_controller == INPUT_PLAYER3)
		{
			if (input_flag[P1_RIGHT])   value &= ~0x0001;
			if (input_flag[P1_LEFT])    value &= ~0x0002;
			if (input_flag[P1_DOWN])    value &= ~0x0004;
			if (input_flag[P1_UP])      value &= ~0x0008;
			if (input_flag[P1_BUTTON1]) value &= ~0x0010;
			if (input_flag[P1_BUTTON2]) value &= ~0x0020;
			if (input_flag[P1_BUTTON3]) value &= ~0x0040;
			if (input_flag[P1_12])		 value &= ~0x0030;
			if (input_flag[P1_13])		 value &= ~0x0050;
			if (input_flag[P1_23])		 value &= ~0x0060;
		}
		break;

	case INPTYPE_ddtod:
		if (option_controller == INPUT_PLAYER3)
		{
			if (input_flag[P1_RIGHT])   value &= ~0x0001;
			if (input_flag[P1_LEFT])    value &= ~0x0002;
			if (input_flag[P1_DOWN])    value &= ~0x0004;
			if (input_flag[P1_UP])      value &= ~0x0008;
			if (input_flag[P1_BUTTON1]) value &= ~0x0010;
			if (input_flag[P1_BUTTON2]) value &= ~0x0020;
			if (input_flag[P1_BUTTON3]) value &= ~0x0040;
			if (input_flag[P1_BUTTON4]) value &= ~0x0080;
			if (input_flag[P1_12])		 value &= ~0x0030;
			if (input_flag[P1_13])		 value &= ~0x0050;
			if (input_flag[P1_14])		 value &= ~0x0090;
			if (input_flag[P1_23])		 value &= ~0x0060;
			if (input_flag[P1_24])		 value &= ~0x00a0;
			if (input_flag[P1_34])		 value &= ~0x00c0;
			if (input_flag[P1_123])	 value &= ~0x0070;
			if (input_flag[P1_124])	 value &= ~0x00b0;
			if (input_flag[P1_134])	 value &= ~0x00d0;
			if (input_flag[P1_234])	 value &= ~0x00e0;
			if (input_flag[P1_1234])	 value &= ~0x00f0;
		}
		else if (option_controller == INPUT_PLAYER4)
		{
			if (input_flag[P1_RIGHT])   value &= ~0x0100;
			if (input_flag[P1_LEFT])    value &= ~0x0200;
			if (input_flag[P1_DOWN])    value &= ~0x0400;
			if (input_flag[P1_UP])      value &= ~0x0800;
			if (input_flag[P1_BUTTON1]) value &= ~0x1000;
			if (input_flag[P1_BUTTON2]) value &= ~0x2000;
			if (input_flag[P1_BUTTON3]) value &= ~0x4000;
			if (input_flag[P1_BUTTON4]) value &= ~0x8000;
			if (input_flag[P1_12])		 value &= ~0x3000;
			if (input_flag[P1_13])		 value &= ~0x5000;
			if (input_flag[P1_14])		 value &= ~0x9000;
			if (input_flag[P1_23])		 value &= ~0x6000;
			if (input_flag[P1_24])		 value &= ~0xa000;
			if (input_flag[P1_34])		 value &= ~0xc000;
			if (input_flag[P1_123])	 value &= ~0x7000;
			if (input_flag[P1_124])	 value &= ~0xb000;
			if (input_flag[P1_134])	 value &= ~0xd000;
			if (input_flag[P1_234])	 value &= ~0xe000;
			if (input_flag[P1_1234])	 value &= ~0xf000;
		}
		break;
	}

#ifdef ADHOC
	if (adhoc_enable)
		send_data.port_value[1] = value;
	else
#endif
		cps2_port_value[1] = value;
}


/*------------------------------------------------------
	CPS2 ポート2 (START / COIN)
------------------------------------------------------*/

static void update_inputport2(void)
{
	uint16_t value = 0xffff;

	if (input_flag[SERV_SWITCH] || service_switch)
	{
		value &= ~0x0002;
	}
	if (input_flag[SERV_COIN])
	{
		value &= ~0x0004;
	}
	if (p12_start_pressed)
	{
		value &= ~(0x0100 | 0x0200);
	}

	if (input_flag[P1_COIN])
	{
		switch (coin_chuter[input_coin_chuter][option_controller])
		{
		case 1: value &= ~0x1000; break;
		case 2: value &= ~0x2000; break;
		case 3: value &= ~0x4000; break;
		case 4: value &= ~0x8000; break;
		}
	}

	if (option_controller == INPUT_PLAYER1)
	{
		if (input_flag[P1_START]) value &= ~0x0100;
		if (input_flag[P2_START]) value &= ~0x0200;
	}
	else if (option_controller == INPUT_PLAYER2)
	{
		if (input_flag[P1_START]) value &= ~0x0200;
		if (input_flag[P2_START]) value &= ~0x0100;
	}

	switch (machine_input_type)
	{
	case INPTYPE_cps2:
	case INPTYPE_ssf2:
		if (option_controller == INPUT_PLAYER2)
		{
			// Player 2 button 6
			if (input_flag[P1_BUTTON6]) value &= ~0x4000;
			if (input_flag[P1_456])     value &= ~0x4000;
		}
		break;

	case INPTYPE_avsp:
		if (option_controller == INPUT_PLAYER3)
		{
			// Player 3 start
			if (input_flag[P1_START]) value &= ~0x0400;
		}
		break;

	case INPTYPE_ddtod:
	case INPTYPE_batcir:
		if (option_controller == INPUT_PLAYER3)
		{
			// Player 3 start
			if (input_flag[P1_START]) value &= ~0x0400;
		}
		else if (option_controller == INPUT_PLAYER4)
		{
			// Player 4 start
			if (input_flag[P1_START]) value &= ~0x0800;
		}
		break;
	}

#ifdef ADHOC
	if (adhoc_enable)
		send_data.port_value[2] = value;
	else
#endif
		cps2_port_value[2] = value;
}


/*------------------------------------------------------
	pzloop2 アナログ入力ポート
------------------------------------------------------*/

static void update_inputport3(void)
{
	int delta = 0;

	if (input_flag[P1_DIAL_L])
	{
		switch (analog_sensitivity)
		{
		case 0: delta -= 10; break;
		case 1: delta -= 15; break;
		case 2: delta -= 20; break;
		}
	}
	if (input_flag[P1_DIAL_R])
	{
		switch (analog_sensitivity)
		{
		case 0: delta += 10; break;
		case 1: delta += 15; break;
		case 2: delta += 20; break;
		}
	}
	input_analog_value[option_controller] = (input_analog_value[option_controller] + delta) & 0xff;

#ifdef ADHOC
	if (adhoc_enable)
		send_data.port_value[0] = input_analog_value[0] | (input_analog_value[1] << 8);
	else
#endif
		cps2_port_value[3] = input_analog_value[0] | (input_analog_value[1] << 8);
}


/*------------------------------------------------------
	入力ボタンを画面方向に合わせて調整
------------------------------------------------------*/

static uint32_t adjust_input(uint32_t buttons)
{
	uint32_t buttons2;

	if (!cps_flip_screen && machine_screen_type != SCREEN_VERTICAL)
		return buttons;

	if (!machine_screen_type)
	{
		if (cps_flip_screen)
		{
			buttons2 = buttons & (PLATFORM_PAD_START | PLATFORM_PAD_SELECT);

			if (buttons & PLATFORM_PAD_UP)       buttons2 |= PLATFORM_PAD_DOWN;
			if (buttons & PLATFORM_PAD_DOWN)     buttons2 |= PLATFORM_PAD_UP;
			if (buttons & PLATFORM_PAD_RIGHT)    buttons2 |= PLATFORM_PAD_LEFT;
			if (buttons & PLATFORM_PAD_LEFT)     buttons2 |= PLATFORM_PAD_RIGHT;
			if (buttons & PLATFORM_PAD_B3)   buttons2 |= PLATFORM_PAD_B1;
			if (buttons & PLATFORM_PAD_B1)   buttons2 |= PLATFORM_PAD_B3;
			if (buttons & PLATFORM_PAD_B4) buttons2 |= PLATFORM_PAD_B2;
			if (buttons & PLATFORM_PAD_B2)    buttons2 |= PLATFORM_PAD_B4;
			if (buttons & PLATFORM_PAD_R) buttons2 |= PLATFORM_PAD_L;
			if (buttons & PLATFORM_PAD_L) buttons2 |= PLATFORM_PAD_R;

			buttons = buttons2;
		}
	}
	else
	{
		if (!cps_rotate_screen)
		{
			buttons2 = buttons & (PLATFORM_PAD_START | PLATFORM_PAD_SELECT | PLATFORM_PAD_R | PLATFORM_PAD_L);

			if (buttons & PLATFORM_PAD_UP)       buttons2 |= PLATFORM_PAD_LEFT;
			if (buttons & PLATFORM_PAD_DOWN)     buttons2 |= PLATFORM_PAD_RIGHT;
			if (buttons & PLATFORM_PAD_RIGHT)    buttons2 |= PLATFORM_PAD_UP;
			if (buttons & PLATFORM_PAD_LEFT)     buttons2 |= PLATFORM_PAD_DOWN;
			if (buttons & PLATFORM_PAD_B4) buttons2 |= PLATFORM_PAD_B3;
			if (buttons & PLATFORM_PAD_B1)   buttons2 |= PLATFORM_PAD_B4;
			if (buttons & PLATFORM_PAD_B3)   buttons2 |= PLATFORM_PAD_B2;
			if (buttons & PLATFORM_PAD_B2)    buttons2 |= PLATFORM_PAD_B1;

			buttons = buttons2;
		}

		if (cps_flip_screen)
		{
			buttons2 = buttons & (PLATFORM_PAD_START | PLATFORM_PAD_SELECT);

			if (buttons & PLATFORM_PAD_UP)       buttons2 |= PLATFORM_PAD_DOWN;
			if (buttons & PLATFORM_PAD_DOWN)     buttons2 |= PLATFORM_PAD_UP;
			if (buttons & PLATFORM_PAD_RIGHT)    buttons2 |= PLATFORM_PAD_LEFT;
			if (buttons & PLATFORM_PAD_LEFT)     buttons2 |= PLATFORM_PAD_RIGHT;
			if (buttons & PLATFORM_PAD_B3)   buttons2 |= PLATFORM_PAD_B1;
			if (buttons & PLATFORM_PAD_B1)   buttons2 |= PLATFORM_PAD_B3;
			if (buttons & PLATFORM_PAD_B4) buttons2 |= PLATFORM_PAD_B2;
			if (buttons & PLATFORM_PAD_B2)    buttons2 |= PLATFORM_PAD_B4;
			if (buttons & PLATFORM_PAD_R) buttons2 |= PLATFORM_PAD_L;
			if (buttons & PLATFORM_PAD_L) buttons2 |= PLATFORM_PAD_R;

			buttons = buttons2;
		}
	}

	return buttons;
}


/******************************************************************************
	入力ポートインタフェース関数
******************************************************************************/

/*------------------------------------------------------
	入力ポートの初期化
------------------------------------------------------*/

int input_init(void)
{
	input_ui_wait = 0;
	service_switch = 0;
	p12_start_pressed = 0;

	memset(cps2_port_value, 0xff, sizeof(cps2_port_value));
	memset(af_counter, 0, sizeof(af_counter));
	memset(input_flag, 0, sizeof(input_flag));

	input_analog_value[0] = 0;
	input_analog_value[1] = 0;

	switch (machine_input_type)
	{
	case INPTYPE_avsp:
		input_max_players = 3;
		break;

	case INPTYPE_ddtod:
	case INPTYPE_batcir:
		input_max_players = 4;
		break;

	default:
		input_max_players = 2;
		break;
	}

	switch (machine_input_type)
	{
	case INPTYPE_19xx:
	case INPTYPE_batcir:
		input_max_buttons = 2;
		break;

	case INPTYPE_cybots:
	case INPTYPE_ddtod:
	case INPTYPE_qndream:
	case INPTYPE_pzloop2:
		input_max_buttons = 4;
		break;

	case INPTYPE_cps2:
	case INPTYPE_ssf2:
		input_max_buttons = 6;
		break;

	default:
		input_max_buttons = 3;
		break;
	}

	input_coin_chuter = COIN_NONE;

#ifdef ADHOC
	if (adhoc_enable)
		return adhoc_start_thread();
#endif

	return 1;
}


/*------------------------------------------------------
	入力ポートの終了
------------------------------------------------------*/

void input_shutdown(void)
{
#ifdef ADHOC
	if (adhoc_enable)
		adhoc_stop_thread();
#endif
}


/*------------------------------------------------------
	入力ポートをリセット
------------------------------------------------------*/

void input_reset(void)
{
	memset(cps2_port_value, 0xff, sizeof(cps2_port_value));
	input_analog_value[0] = 0;
	input_analog_value[1] = 0;
	service_switch = 0;
	p12_start_pressed = 0;

	setup_autofire();

	if (driver->inp_eeprom) check_eeprom_settings(0);

#ifdef ADHOC
	if (adhoc_enable)
		adhoc_reset_thread();
#endif
}


/*------------------------------------------------------
	連射フラグを設定
------------------------------------------------------*/

void setup_autofire(void)
{
	int i;

	for (i = 0; i < CPS2_BUTTON_MAX; i++)
	{
		af_map1[i] = input_map[P1_AF_1 + i];
		af_map2[i] = input_map[P1_BUTTON1 + i];
	}
}


/*------------------------------------------------------
	入力ポートを更新
------------------------------------------------------*/

void update_inputport(void)
{
	int i;
	uint32_t buttons;

#ifdef ADHOC
	if (adhoc_enable)
	{
#if !ADHOC_UPDATE_EVERY_FRAME
		if (adhoc_frame & 1)
		{
			adhoc_frame++;
		}
		else
#endif
		{
			while (adhoc_update && Loop == LOOP_EXEC)
			{
				usleep(1);
			}

			cps2_port_value[0] = send_data.port_value[0] & recv_data.port_value[0];
			cps2_port_value[1] = send_data.port_value[1] & recv_data.port_value[1];
			cps2_port_value[2] = send_data.port_value[2] & recv_data.port_value[2];
			cps2_port_value[3] = send_data.port_value[3] & recv_data.port_value[3];

			if (Loop == LOOP_EXEC)
				Loop = recv_data.loop_flag;

			if (recv_data.paused)
				adhoc_paused = recv_data.paused;

			if (adhoc_paused)
			{
				adhoc_pause();
			}

			service_switch = 0;
			p12_start_pressed = 0;

			if (driver->inp_eeprom) check_eeprom_settings(0);

			buttons = poll_gamepad();

			if (systembuttons_available ? readHomeButton() : (buttons & PLATFORM_PAD_START) && (buttons & PLATFORM_PAD_SELECT))
			{
				buttons = 0;
				adhoc_paused = adhoc_server + 1;
			}
			else if ((buttons & PLATFORM_PAD_L) && (buttons & PLATFORM_PAD_R))
			{
				if (buttons & PLATFORM_PAD_SELECT)
				{
					buttons &= ~(PLATFORM_PAD_SELECT | PLATFORM_PAD_L | PLATFORM_PAD_R);
					service_switch = 1;
				}
				else if (buttons & PLATFORM_PAD_START)
				{
					buttons &= ~(PLATFORM_PAD_START | PLATFORM_PAD_L | PLATFORM_PAD_R);
					p12_start_pressed = 1;
				}
			}

			buttons = adjust_input(buttons);
			buttons = update_autofire(buttons);

			for (i = 0; i < MAX_INPUTS; i++)
				input_flag[i] = (buttons & input_map[i]) != 0;

			update_inputport0();
			update_inputport1();
			update_inputport2();
			if (machine_input_type == INPTYPE_pzloop2) update_inputport3();

			send_data.buttons   = buttons;
			send_data.paused    = adhoc_paused;
			send_data.loop_flag = Loop;
			send_data.frame     = adhoc_frame++;

			usleep(100);

			adhoc_update = 1;
		}
	}
	else
#endif
	{
		service_switch = 0;
		p12_start_pressed = 0;

		if (driver->inp_eeprom) check_eeprom_settings(1);

		buttons = poll_gamepad();

		if (systembuttons_available ? readHomeButton() : (buttons & PLATFORM_PAD_START) && (buttons & PLATFORM_PAD_SELECT))
		{
			showmenu();
			setup_autofire();
			buttons = poll_gamepad();
		}

		if ((buttons & PLATFORM_PAD_L) && (buttons & PLATFORM_PAD_R))
		{
			if (buttons & PLATFORM_PAD_SELECT)
			{
				buttons &= ~(PLATFORM_PAD_SELECT | PLATFORM_PAD_L | PLATFORM_PAD_R);
				service_switch = 1;
			}
			else if (buttons & PLATFORM_PAD_START)
			{
				buttons &= ~(PLATFORM_PAD_START | PLATFORM_PAD_L | PLATFORM_PAD_R);
				p12_start_pressed = 1;
			}
		}

		buttons = adjust_input(buttons);
		buttons = update_autofire(buttons);

		for (i = 0; i < MAX_INPUTS; i++)
			input_flag[i] = (buttons & input_map[i]) != 0;

		update_inputport0();
		update_inputport1();
		update_inputport2();
		if (machine_input_type == INPTYPE_pzloop2) update_inputport3();

		if (input_flag[SNAPSHOT])
		{
			save_snapshot();
		}
		
		if (input_flag[SWPLAYER])
		{
			if (!input_ui_wait)
			{
				option_controller++;
				if (option_controller == input_max_players)
					option_controller = INPUT_PLAYER1;
				ui_popup(TEXT(CONTROLLER_PLAYERx), option_controller + 1);
				input_ui_wait = 30;
			}
		}		
		
#ifdef COMMAND_LIST
		if (input_flag[COMMANDLIST])
		{
			commandlist(1);
			buttons = poll_gamepad();
		}
#endif
		
		if (input_ui_wait > 0) input_ui_wait--;
	}
}


/******************************************************************************
	セーブ/ロード ステート
******************************************************************************/

#ifdef SAVE_STATE

STATE_SAVE( input )
{
	state_save_long(&option_controller, 1);
	state_save_long(&input_analog_value[0], 1);
	state_save_long(&input_analog_value[1], 1);
}

STATE_LOAD( input )
{
	state_load_long(&option_controller, 1);
	state_load_long(&input_analog_value[0], 1);
	state_load_long(&input_analog_value[1], 1);

	setup_autofire();
	input_ui_wait = 0;
	p12_start_pressed = 0;
	service_switch = 0;
}

#endif /* SAVE_STATE */
