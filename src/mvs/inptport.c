/******************************************************************************

	inptport.c

	MVS Input Port Emulation

******************************************************************************/

#include "mvs.h"
#ifdef ADHOC
#include "common/adhoc.h"
#endif
#ifdef COMMAND_LIST
#include "common/cmdlist.h"
#endif
#include "common/emulator_options.h"
#include "common/emulator_runtime.h"
#include "common/input_driver.h"
#include "common/ui_text_driver.h"
#include "common/ui.h"
#include "common/ui_menu.h"
#include <string.h>


/******************************************************************************
	Global Variables
******************************************************************************/

int option_controller;
uint8_t ALIGN16_DATA neogeo_port_value[MVS_PORT_MAX];

int input_map[MAX_INPUTS];
int analog_sensitivity;
int af_interval = 1;

int neogeo_dipswitch;
int neogeo_input_mode;
int input_analog_value[2];


/******************************************************************************
	Local Variables
******************************************************************************/

static const uint8_t hotkey_mask[11] =
{
//	0xef,	// A
//	0xdf,	// B
//	0xbf,	// C
//	0x7f,	// D
	0xcf,	// A+B
	0xaf,	// A+C
	0x6f,	// A+D
	0x9f,	// B+C
	0x5f,	// B+D
	0x3f,	// C+D
	0x8f,	// A+B+C
	0x4f,	// A+B+D
	0x2f,	// A+C+D
	0x1f,	// B+C+D
	0x0f	// A+B+C+D
};

static uint8_t ALIGN16_DATA input_flag[MAX_INPUTS];
static int ALIGN16_DATA af_map1[MVS_BUTTON_MAX];
static int ALIGN16_DATA af_map2[MVS_BUTTON_MAX];
static int ALIGN16_DATA af_counter[2][MVS_BUTTON_MAX];
static int input_ui_wait;
static int service_switch;

typedef enum mvs_input_poll_mode
{
	MVS_INPUT_POLL_NORMAL = 0,
	MVS_INPUT_POLL_FATFURSP,
	MVS_INPUT_POLL_ANALOG
} mvs_input_poll_mode_t;

static mvs_input_poll_mode_t input_poll_mode;

static void update_inputport0(void);
static void update_inputport1(void);
static void update_inputport2(void);
static void update_inputport4(void);
static void update_inputport5(void);
static void irrmaze_update_analog_port(const input_state_t *state);
static void popbounc_update_analog_port(const input_state_t *state);


static uint32_t mvs_fatfursp_buttons(const input_state_t *state)
{
	uint32_t buttons = state->buttons;

	/* Fatal Fury Special uses the analog stick as an alternate direction
	 * source, but an explicitly pressed opposite D-pad direction wins. */
	if (state->axis_flags & INPUT_AXIS_LY)
	{
		if (state->ly >= INPUT_ANALOG_HIGH_THRESHOLD &&
			!(buttons & PLATFORM_PAD_UP))
			buttons |= PLATFORM_PAD_DOWN;
		if (state->ly <= INPUT_ANALOG_LOW_THRESHOLD &&
			!(buttons & PLATFORM_PAD_DOWN))
			buttons |= PLATFORM_PAD_UP;
	}
	if (state->axis_flags & INPUT_AXIS_LX)
	{
		if (state->lx <= INPUT_ANALOG_LOW_THRESHOLD &&
			!(buttons & PLATFORM_PAD_RIGHT))
			buttons |= PLATFORM_PAD_LEFT;
		if (state->lx >= INPUT_ANALOG_HIGH_THRESHOLD &&
			!(buttons & PLATFORM_PAD_LEFT))
			buttons |= PLATFORM_PAD_RIGHT;
	}

	return buttons;
}

static uint32_t poll_mvs_pad_index(uint32_t controller, input_state_t *state)
{
	if (!sample_gamepad_index(controller, state))
		return 0;

	switch (input_poll_mode)
	{
	case MVS_INPUT_POLL_FATFURSP:
		state->buttons = mvs_fatfursp_buttons(state);
		break;

	case MVS_INPUT_POLL_ANALOG:
		/* Raw axes are consumed separately by the MVS-specific analog games. */
		break;

	case MVS_INPUT_POLL_NORMAL:
	default:
		state->buttons = input_state_digital_buttons(state);
		break;
	}

	return state->buttons;
}

static uint32_t poll_mvs_pad(input_state_t *state)
{
	return poll_mvs_pad_index(0, state);
}


/******************************************************************************
	Local Functions
******************************************************************************/

/*------------------------------------------------------
	Input Port Type Check
------------------------------------------------------*/

void check_input_mode(void)
{
	if (machine_init_type == INIT_ms5pcb
	||	machine_init_type == INIT_svcpcb
	||	machine_init_type == INIT_kf2k3pcb
	||	machine_init_type == INIT_jockeygp
	||	neogeo_bios == NEOGEO_GIT)
	{
		neogeo_input_mode = INPUT_MVS;
		return;
	}
	else if (!neogeo_machine_mode)
	{
		if (memory_region_user1[0x00400 >> 1] & 0x8000)
			neogeo_input_mode = INPUT_AES;
		else
			neogeo_input_mode = INPUT_MVS;
	}
	else
	{
		neogeo_input_mode = neogeo_machine_mode - 1;
	}

	if (neogeo_ngh == NGH_irrmaze)
		return;

	switch (neogeo_bios)
	{
	case UNI_V32:
	case UNI_V31:
	case UNI_V30:
	case UNI_V23:
	case UNI_V22:
	case UNI_V21:
	case UNI_V20:
	case UNI_V13:
	case UNI_V12:
	case UNI_V11:
	case UNI_V10:
		neogeo_input_mode = (neogeo_sram16[0x02 >> 1] & 0x8000) != 0;
		break;

	case ASIA_AES:
	case JAPAN_AES:
	case DEBUG_BIOS:
		neogeo_input_mode = INPUT_AES;
		break;
	}
}


/*------------------------------------------------------
	Update Autofire Flag
------------------------------------------------------*/

static uint32_t update_autofire(uint32_t buttons, int controller)
{
	int i;
	int *counter = af_counter[controller & 1];

	for (i = 0; i < MVS_BUTTON_MAX; i++)
	{
		if (af_map1[i])
		{
			if (buttons & af_map1[i])
			{
				buttons &= ~af_map1[i];

				if (counter[i] == 0)
					buttons |= af_map2[i];
				else
					buttons &= ~af_map2[i];

				if (++counter[i] > af_interval)
					counter[i] = 0;
			}
			else
			{
				counter[i] = 0;
			}
		}
	}

	return buttons;
}

static void set_input_flags(uint32_t buttons)
{
	int i;

	for (i = 0; i < MAX_INPUTS; i++)
		input_flag[i] = (buttons & input_map[i]) != 0;
}

static void clear_secondary_system_flags(void)
{
	input_flag[SERV_COIN] = 0;
	input_flag[TEST_SWITCH] = 0;
}

static bool supports_physical_multiplayer(void)
{
	switch (neogeo_ngh)
	{
	case NGH_irrmaze:
	case NGH_vliner:
	case NGH_jockeygp:
		return false;
	default:
		return true;
	}
}

static void update_inputport_multi(uint32_t controller_count)
{
	uint8_t combined_port0 = 0xff;
	uint8_t combined_port1 = 0xff;
	uint8_t combined_port2 = 0xff;
	uint8_t combined_port4 = 0xff;
	uint8_t combined_port5 = 0xff;
	input_state_t primary_state;
	uint32_t primary_buttons;
	uint32_t primary_processed = 0;
	int saved_controller;
	uint32_t controller;

	if (controller_count > 2)
		controller_count = 2;

	service_switch = 0;
	primary_buttons = poll_mvs_pad_index(0, &primary_state);

	if (pad_menu_combo_pressed(primary_buttons))
	{
		showmenu();
		setup_autofire();

		if (neogeo_input_mode)
			neogeo_port_value[3] = neogeo_dipswitch & 0xff;
		else
			neogeo_port_value[3] = 0xff;

		primary_buttons = poll_mvs_pad_index(0, &primary_state);
	}
	else if ((primary_buttons & PLATFORM_PAD_L) &&
	         (primary_buttons & PLATFORM_PAD_R) &&
	         (primary_buttons & PLATFORM_PAD_SELECT))
	{
		primary_buttons &= ~(PLATFORM_PAD_SELECT |
			PLATFORM_PAD_L | PLATFORM_PAD_R);
		service_switch = 1;
	}

	saved_controller = option_controller;

	for (controller = 0; controller < controller_count; controller++)
	{
		input_state_t state;
		uint32_t buttons;

		if (controller == 0)
		{
			state = primary_state;
			buttons = primary_buttons;
		}
		else
		{
			buttons = poll_mvs_pad_index(controller, &state);
		}

		option_controller = (int)controller;

		if (neogeo_ngh == NGH_popbounc)
			popbounc_update_analog_port(&state);

		buttons = update_autofire(buttons, (int)controller);
		set_input_flags(buttons);
		if (controller != 0)
			clear_secondary_system_flags();

		update_inputport0();
		update_inputport1();
		update_inputport2();
		update_inputport4();
		update_inputport5();

		combined_port0 &= neogeo_port_value[0];
		combined_port1 &= neogeo_port_value[1];
		combined_port2 &= neogeo_port_value[2];
		combined_port4 &= neogeo_port_value[4];
		combined_port5 &= neogeo_port_value[5];

		if (controller == 0)
		{
			primary_processed = buttons;

			if (input_flag[SNAPSHOT])
				save_snapshot();

#ifdef COMMAND_LIST
			if (input_flag[COMMANDLIST])
				commandlist(1);
#endif
		}
	}

	neogeo_port_value[0] = combined_port0;
	neogeo_port_value[1] = combined_port1;
	neogeo_port_value[2] = combined_port2;
	neogeo_port_value[4] = combined_port4;
	neogeo_port_value[5] = combined_port5;

	option_controller = saved_controller;
	set_input_flags(primary_processed);

	/* Switch Player is a single-pad compatibility feature. With multiple
	 * physical pads attached, pad N already owns emulated player N. */
	if (input_ui_wait > 0)
		input_ui_wait--;
}


/*------------------------------------------------------
	Apply Hotkey Flag
------------------------------------------------------*/

static uint8_t apply_hotkey(uint8_t value)
{
	int i, button;

	button = P1_AB;
	for (i = 0; i < 11; i++)
	{
		if (input_flag[button]) value &= hotkey_mask[i];
		button++;
	}

	return value;
}


/*------------------------------------------------------
	MVS Controller 1
------------------------------------------------------*/

static void update_inputport0(void)
{
	uint8_t value = 0xff;

	switch (neogeo_ngh)
	{
	case NGH_irrmaze:
		if (!option_controller)
		{
			if (input_flag[P1_UP])      value &= ~0x01;
			if (input_flag[P1_DOWN])    value &= ~0x02;
			if (input_flag[P1_LEFT])    value &= ~0x04;
			if (input_flag[P1_RIGHT])   value &= ~0x08;
			if (input_flag[P1_BUTTONA]) value &= ~0x10;
			if (input_flag[P1_BUTTONB]) value &= ~0x20;
		}
		break;

	case NGH_popbounc:
		if (!option_controller)
		{
			if (input_flag[P1_UP])      value &= ~0x01;
			if (input_flag[P1_DOWN])    value &= ~0x02;
			if (input_flag[P1_LEFT])    value &= ~0x04;
			if (input_flag[P1_RIGHT])   value &= ~0x08;
			if (input_flag[P1_BUTTONA]) value &= ~(0x10|0x80);
			if (input_flag[P1_BUTTONB]) value &= ~0x20;
			if (input_flag[P1_BUTTONC]) value &= ~0x40;
		}
		break;

	default:
		if (!option_controller)
		{
			if (input_flag[P1_UP])      value &= ~0x01;
			if (input_flag[P1_DOWN])    value &= ~0x02;
			if (input_flag[P1_LEFT])    value &= ~0x04;
			if (input_flag[P1_RIGHT])   value &= ~0x08;
			if (input_flag[P1_BUTTONA]) value &= ~0x10;
			if (input_flag[P1_BUTTONB]) value &= ~0x20;
			if (input_flag[P1_BUTTONC]) value &= ~0x40;
			if (input_flag[P1_BUTTOND]) value &= ~0x80;

			value = apply_hotkey(value);
		}
		break;
	}

#ifdef ADHOC
	if (adhoc_enable)
		send_data.port_value[0] = value;
	else
#endif
		neogeo_port_value[0] = value;
}


/*------------------------------------------------------
	MVS Controller 2
------------------------------------------------------*/

static void update_inputport1(void)
{
	uint8_t value = 0xff;

	switch (neogeo_ngh)
	{
	case NGH_irrmaze:
		{
			if (input_flag[P1_UP])      value &= ~0x01;
			if (input_flag[P1_DOWN])    value &= ~0x02;
			if (input_flag[P1_LEFT])    value &= ~0x04;
			if (input_flag[P1_RIGHT])   value &= ~0x08;
			if (input_flag[P1_BUTTONA]) value &= ~0x10;
			if (input_flag[P1_BUTTONB]) value &= ~0x20;
		}
		break;

	case NGH_popbounc:
		if (option_controller)
		{
			if (input_flag[P1_UP])      value &= ~0x01;
			if (input_flag[P1_DOWN])    value &= ~0x02;
			if (input_flag[P1_LEFT])    value &= ~0x04;
			if (input_flag[P1_RIGHT])   value &= ~0x08;
			if (input_flag[P1_BUTTONA]) value &= ~(0x10|0x80);
			if (input_flag[P1_BUTTONB]) value &= ~0x20;
			if (input_flag[P1_BUTTONC]) value &= ~0x40;
		}
		break;

	default:
		if (option_controller)
		{
			if (input_flag[P1_UP])      value &= ~0x01;
			if (input_flag[P1_DOWN])    value &= ~0x02;
			if (input_flag[P1_LEFT])    value &= ~0x04;
			if (input_flag[P1_RIGHT])   value &= ~0x08;
			if (input_flag[P1_BUTTONA]) value &= ~0x10;
			if (input_flag[P1_BUTTONB]) value &= ~0x20;
			if (input_flag[P1_BUTTONC]) value &= ~0x40;
			if (input_flag[P1_BUTTOND]) value &= ~0x80;

			value = apply_hotkey(value);
		}
		break;
	}

#ifdef ADHOC
	if (adhoc_enable)
		send_data.port_value[1] = value;
	else
#endif
		neogeo_port_value[1] = value;
}


/*------------------------------------------------------
	MVS Start Button
------------------------------------------------------*/

static void update_inputport2(void)
{
	uint8_t value = 0xff;

	switch (neogeo_ngh)
	{
	case NGH_vliner:
		if (input_flag[P1_START]) value &= ~0x01;
		break;

	case NGH_jockeygp:
		break;

	default:
		if (option_controller)
		{
			if (input_flag[P1_START]) value &= ~0x04;
			if (!neogeo_input_mode)
			{
				if (input_flag[P1_COIN]) value &= ~0x08;
			}
		}
		else
		{
			if (input_flag[P1_START]) value &= ~0x01;
			if (!neogeo_input_mode)
			{
				if (input_flag[P1_COIN]) value &= ~0x02;
			}
		}
		break;
	}

#ifdef ADHOC
	if (adhoc_enable)
		send_data.port_value[2] = value;
	else
#endif
		neogeo_port_value[2] = value;
}


/*------------------------------------------------------
	MVS Coin/Service Switch
------------------------------------------------------*/

static void update_inputport4(void)
{
	uint8_t value;

	switch (neogeo_ngh)
	{
	case NGH_vliner:
		{
			static int coin_wait = 0;

			value = 0xff;
			if (coin_wait == 0)
			{
				if (input_flag[P1_COIN])
				{
					value &= ~0x01;
					coin_wait = 12;	// Coin insert wait
				}
			}
			else if (coin_wait)
			{
				// Coin insert wait processing
				if (coin_wait > 4) value &= ~0x01;
				coin_wait--;
			}
			if (input_flag[OTHER1]) value &= ~0x10;
			if (input_flag[OTHER2]) value &= ~0x20;
			if (input_flag[OTHER3]) value &= ~0x80;
		}
		break;

	default:
		value = 0x3f;
		if (neogeo_input_mode)
		{
			if (option_controller)
			{
				if (input_flag[P1_COIN]) value &= ~0x02;
			}
			else
			{
				if (input_flag[P1_COIN]) value &= ~0x01;
				if (input_flag[SERV_COIN]) value &= ~0x04;
			}
		}
		break;
	}

#ifdef ADHOC
	if (adhoc_enable)
		send_data.port_value[4] = value;
	else
#endif
		neogeo_port_value[4] = value;
}


/*------------------------------------------------------
	MVS Test Switch
------------------------------------------------------*/

static void update_inputport5(void)
{
	uint8_t value = 0xc0;

	if (neogeo_input_mode)
	{
		if (input_flag[TEST_SWITCH] || service_switch) value &= ~0x80;
	}

#ifdef ADHOC
	if (adhoc_enable)
		send_data.port_value[5] = value;
	else
#endif
		neogeo_port_value[5] = value;
}


/*------------------------------------------------------
	irrmaze Analog Input Port
------------------------------------------------------*/

static void irrmaze_update_analog_port(const input_state_t *state)
{
	int axis, delta;
	int current;
	int pad_value[2] = { state->lx, state->ly };

	for (axis = 0; axis < 2; axis++)
	{
		current = pad_value[axis];

		delta = 0;
		if (axis)
		{
			if (input_flag[P1_UP]) delta = -1;
			if (input_flag[P1_DOWN]) delta = 1;
		}
		else
		{
			if (input_flag[P1_LEFT]) delta = -1;
			if (input_flag[P1_RIGHT]) delta = 1;
		}
		switch (analog_sensitivity)
		{
		case 0:
			if (current > 0x80)
			{
				if (current >= 0xe0) delta = 2;
				else if (current >= 0xa0) delta = 1;
			}
			else
			{
				if (current <= 0x1f) delta = -3;
				else if (current <= 0x5f) delta = -1;
			}
			break;

		case 1:
			if (current > 0x80)
			{
				if (current >= 0xf0) delta = 3;
				else if (current >= 0xd0) delta = 2;
				else if (current >= 0xa0) delta = 1;
			}
			else
			{
				if (current <= 0x0f) delta = -3;
				else if (current <= 0x2f) delta = -2;
				else if (current <= 0x5f) delta = -1;
			}
			break;

		case 2:
			if (current > 0x80)
			{
				if (current >= 0xf8) delta = 4;
				else if (current >= 0xe8) delta = 3;
				else if (current >= 0xd0) delta = 2;
				else if (current >= 0x98) delta = 1;
			}
			else
			{
				if (current <= 0x07) delta = -4;
				else if (current <= 0x17) delta = -3;
				else if (current <= 0x2f) delta = -2;
				else if (current <= 0x67) delta = -1;
			}
			break;
		}

		// reverse
		delta = -delta;

		input_analog_value[axis] += delta;
		input_analog_value[axis] &= 0xff;
	}
}


/*------------------------------------------------------
	popbounc Analog Input Port
------------------------------------------------------*/

static void popbounc_update_analog_port(const input_state_t *state)
{
	int delta, current;

	delta = 0;
	current = state->lx;

	switch (analog_sensitivity)
	{
	case 0:
		if (current > 0x80)
		{
			if (current >= 0xf0) delta = 3;
			else if (current >= 0xd0) delta = 2;
			else if (current >= 0xa0) delta = 1;
		}
		else
		{
			if (current <= 0x0f) delta = -3;
			else if (current <= 0x2f) delta = -2;
			else if (current <= 0x5f) delta = -1;
		}
		break;

	case 1:
		if (current > 0x80)
		{
			if (current >= 0xf8) delta = 4;
			else if (current >= 0xe8) delta = 3;
			else if (current >= 0xd0) delta = 2;
			else if (current >= 0x98) delta = 1;
		}
		else
		{
			if (current <= 0x07) delta = -4;
			else if (current <= 0x17) delta = -3;
			else if (current <= 0x2f) delta = -2;
			else if (current <= 0x67) delta = -1;
		}
		break;

	case 2:
		if (current > 0x80)
		{
			if (current >= 0xf8) delta = 5;
			else if (current >= 0xe8) delta = 4;
			else if (current >= 0xd8) delta = 3;
			else if (current >= 0xc0) delta = 2;
			else if (current >= 0x98) delta = 1;
		}
		else
		{
			if (current <= 0x07) delta = -5;
			else if (current <= 0x17) delta = -4;
			else if (current <= 0x27) delta = -3;
			else if (current <= 0x3f) delta = -2;
			else if (current <= 0x67) delta = -1;
		}
		break;
	}

	input_analog_value[option_controller] += delta;
	if (input_analog_value[option_controller] < 0)
		input_analog_value[option_controller] = 0;
	if (input_analog_value[option_controller] > 0xff)
		input_analog_value[option_controller] = 0xff;
}


/******************************************************************************
	Input Port Interface Functions
******************************************************************************/

/*------------------------------------------------------
	Initialize Input Port
------------------------------------------------------*/

int input_init(void)
{
	input_ui_wait = 0;
	service_switch = 0;

	memset(neogeo_port_value, 0xff, sizeof(neogeo_port_value));
	memset(af_counter, 0, sizeof(af_counter));
	memset(input_flag, 0, sizeof(input_flag));

	input_analog_value[0] = 0x7f;
	input_analog_value[1] = 0x7f;

	neogeo_dipswitch = 0xff;

	input_poll_mode = MVS_INPUT_POLL_NORMAL;
	if (neogeo_ngh == NGH_irrmaze || neogeo_ngh == NGH_popbounc)
	{
#ifdef ADHOC
		if (!adhoc_enable)
#endif
			input_poll_mode = MVS_INPUT_POLL_ANALOG;
	}
	else if (neogeo_ngh == NGH_fatfursp)
		input_poll_mode = MVS_INPUT_POLL_FATFURSP;

#ifdef ADHOC
	if (adhoc_enable)
		return adhoc_start_thread();
#endif

	return 1;
}


/*------------------------------------------------------
	Shutdown Input Port
------------------------------------------------------*/

void input_shutdown(void)
{
#ifdef ADHOC
	if (adhoc_enable)
		adhoc_stop_thread();
#endif
}


/*------------------------------------------------------
	Reset Input Port
------------------------------------------------------*/

void input_reset(void)
{
	memset(neogeo_port_value, 0xff, sizeof(neogeo_port_value));
	input_analog_value[0] = 0x7f;
	input_analog_value[1] = 0x7f;
	service_switch = 0;

	check_input_mode();

	setup_autofire();

	if (neogeo_input_mode)
		neogeo_port_value[3] = neogeo_dipswitch & 0xff;

#ifdef ADHOC
	if (adhoc_enable)
		adhoc_reset_thread();
#endif
}


/*------------------------------------------------------
	Set Autofire Flag
------------------------------------------------------*/

void setup_autofire(void)
{
	int i;

	for (i = 0; i < MVS_BUTTON_MAX; i++)
	{
		af_map1[i] = input_map[P1_AF_A + i];
		af_map2[i] = input_map[P1_BUTTONA + i];
	}
}


/*------------------------------------------------------
	Update Input Port
------------------------------------------------------*/

void update_inputport(void)
{
	int i;
	input_state_t state;
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

			neogeo_port_value[0] = send_data.port_value[0] & recv_data.port_value[0];
			neogeo_port_value[1] = send_data.port_value[1] & recv_data.port_value[1];
			neogeo_port_value[2] = send_data.port_value[2] & recv_data.port_value[2];
			neogeo_port_value[4] = send_data.port_value[4] & recv_data.port_value[4];
			neogeo_port_value[5] = send_data.port_value[5] & recv_data.port_value[5];

			if (Loop == LOOP_EXEC)
				Loop = recv_data.loop_flag;

			if (recv_data.paused)
				adhoc_paused = recv_data.paused;

			if (adhoc_paused)
			{
				adhoc_pause();
			}

			service_switch = 0;

			buttons = poll_mvs_pad(&state);

			if (pad_menu_combo_pressed(buttons))
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
			}

			buttons = update_autofire(buttons, 0);

			for (i = 0; i < MAX_INPUTS; i++)
				input_flag[i] = (buttons & input_map[i]) != 0;

			update_inputport0();
			update_inputport1();
			update_inputport2();
			update_inputport4();
			update_inputport5();

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
		uint32_t controller_count = gamepad_count();

		if (controller_count > 1 && supports_physical_multiplayer())
		{
			update_inputport_multi(controller_count);
			return;
		}

		service_switch = 0;

		buttons = poll_mvs_pad(&state);

		if (pad_menu_combo_pressed(buttons))
		{
			showmenu();
			setup_autofire();

			if (neogeo_input_mode)
				neogeo_port_value[3] = neogeo_dipswitch & 0xff;
			else
				neogeo_port_value[3] = 0xff;

			buttons = poll_mvs_pad(&state);
		}
		else if ((buttons & PLATFORM_PAD_L) && (buttons & PLATFORM_PAD_R))
		{
			if (buttons & PLATFORM_PAD_SELECT)
			{
				buttons &= ~(PLATFORM_PAD_SELECT | PLATFORM_PAD_L | PLATFORM_PAD_R);
				service_switch = 1;
			}
		}

		if (neogeo_ngh == NGH_irrmaze)
			irrmaze_update_analog_port(&state);
		else if (neogeo_ngh == NGH_popbounc)
			popbounc_update_analog_port(&state);

		buttons = update_autofire(buttons, 0);

		for (i = 0; i < MAX_INPUTS; i++)
			input_flag[i] = (buttons & input_map[i]) != 0;

		update_inputport0();
		update_inputport1();
		update_inputport2();
		update_inputport4();
		update_inputport5();

		if (input_flag[SNAPSHOT])
		{
			save_snapshot();
		}
		
		if (input_flag[SWPLAYER])
		{
			if (!input_ui_wait)
			{
				option_controller ^= 1;
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
	Save/Load State
******************************************************************************/

#ifdef SAVE_STATE

STATE_SAVE( input )
{
	state_save_long(&option_controller, 1);
	state_save_long(&neogeo_dipswitch, 1);
	state_save_long(&input_analog_value[0], 1);
	state_save_long(&input_analog_value[1], 1);
}

STATE_LOAD( input )
{
	state_load_long(&option_controller, 1);
	state_load_long(&neogeo_dipswitch, 1);
	state_load_long(&input_analog_value[0], 1);
	state_load_long(&input_analog_value[1], 1);

	check_input_mode();
}

#endif /* SAVE_STATE */
