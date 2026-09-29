#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libpad.h>
#include <libmtap.h>
#include <ps2_joystick_driver.h>
#if defined(PS2_EXTERNAL_IRX_IMAGE)
#include <ps2_drivers_img.h>
#endif
#include "common/input_driver.h"

#define PS2_MAX_PORT      2 /* each ps2 has 2 ports */
#define PS2_MAX_SLOT      4 /* maximum - 4 slots in one multitap */
#define MAX_CONTROLLERS   (PS2_MAX_PORT * PS2_MAX_SLOT)
#define PS2_ANALOG_STICKS 2
#define PS2_ANALOG_AXIS   2
#define PS2_BUTTONS       16
#define PS2_TOTAL_AXIS    (PS2_ANALOG_STICKS * PS2_ANALOG_AXIS)

struct JoyInfo
{
    uint8_t padBuf[256];
    uint16_t btns;
    uint8_t analog_state[PS2_TOTAL_AXIS];
    uint8_t port;
    uint8_t slot;
    int8_t rumble_ready;
    int8_t opened;
} __attribute__((aligned(64)));

struct JoyInfo joyInfo[MAX_CONTROLLERS];

typedef struct ps2_input {
	uint8_t mtap_opened[PS2_MAX_PORT];
	uint32_t refresh_counter;
} ps2_input_t;

static struct JoyInfo *getJoyInfo(uint32_t port, uint32_t slot)
{
	return &joyInfo[port * PS2_MAX_SLOT + slot];
}

static void closeJoyInfo(struct JoyInfo *info)
{
	if (!info->opened)
		return;

	padPortClose(info->port, info->slot);
	info->opened = 0;
}

static void refreshJoyInfo(ps2_input_t *ps2)
{
	uint32_t port;

	for (port = 0; port < PS2_MAX_PORT; port++) {
		uint32_t slot;

		/* mtapPortOpen() can fail when no multitap is present at startup. Retry
		 * it during the periodic refresh so a multitap connected later becomes
		 * discoverable without requiring an emulator restart. */
		if (!ps2->mtap_opened[port])
			ps2->mtap_opened[port] = (mtapPortOpen(port) == 1);

		uint32_t max_slots =
			(ps2->mtap_opened[port] && mtapGetConnection(port) == 1) ?
			PS2_MAX_SLOT : 1;

		for (slot = 0; slot < PS2_MAX_SLOT; slot++) {
			struct JoyInfo *info = getJoyInfo(port, slot);

			if (slot >= max_slots) {
				closeJoyInfo(info);
				continue;
			}

			if (!info->opened) {
				info->port = (uint8_t)port;
				info->slot = (uint8_t)slot;
				if (padPortOpen(port, slot, (void *)info->padBuf) > 0) {
					info->opened = 1;
				}
			}
		}
	}
}

static bool joyInfoReady(const struct JoyInfo *info)
{
	int32_t state;

	if (!info->opened)
		return false;

	state = padGetState(info->port, info->slot);
	return state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1;
}

static uint32_t activeJoyInfoCount(void)
{
	uint32_t count = 0;
	uint32_t slot;

	for (slot = 0; slot < PS2_MAX_SLOT; slot++) {
		uint32_t port;

		for (port = 0; port < PS2_MAX_PORT; port++) {
			struct JoyInfo *info = getJoyInfo(port, slot);
			if (joyInfoReady(info))
				count++;
		}
	}

	return count;
}

static struct JoyInfo *getActiveJoyInfo(ps2_input_t *ps2, uint32_t controller)
{
	uint32_t active_index = 0;
	uint32_t slot;

	/* Preserve the physical connector order used by the old backend:
	 * (0,0), (1,0), (0,1), (1,1), ... */
	for (slot = 0; slot < PS2_MAX_SLOT; slot++) {
		uint32_t port;

		for (port = 0; port < PS2_MAX_PORT; port++) {
			struct JoyInfo *info = getJoyInfo(port, slot);
			if (joyInfoReady(info)) {
				if (active_index == controller)
					return info;
				active_index++;
			}
		}
	}

	/* The requested pad may have appeared since the last refresh. */
	refreshJoyInfo(ps2);
	active_index = 0;

	for (slot = 0; slot < PS2_MAX_SLOT; slot++) {
		uint32_t port;

		for (port = 0; port < PS2_MAX_PORT; port++) {
			struct JoyInfo *info = getJoyInfo(port, slot);
			if (joyInfoReady(info)) {
				if (active_index == controller)
					return info;
				active_index++;
			}
		}
	}

	return NULL;
}

static uint32_t ps2_controllerCount(void *data)
{
	ps2_input_t *ps2 = (ps2_input_t *)data;
	uint32_t count;

	if (!ps2)
		return 0;

	count = activeJoyInfoCount();
	/* Periodically refresh slot availability so a multitap attached after
	 * startup becomes visible without paying device-setup RPC cost every frame. */
	if (count == 0 || ++ps2->refresh_counter >= 60) {
		ps2->refresh_counter = 0;
		refreshJoyInfo(ps2);
		count = activeJoyInfoCount();
	}

	return count;
}

static void *ps2_init(void)
{
	ps2_input_t *ps2 = (ps2_input_t*)calloc(1, sizeof(ps2_input_t));
	uint32_t port = 0;

	if (ps2 == NULL)
		return NULL;

	if (init_joystick_driver(true) < 0) {
#if defined(PS2_EXTERNAL_IRX_IMAGE)
		ps2_drivers_img_discard_staged();
		ps2_drivers_img_forget_source();
#endif
		free(ps2);
		return NULL;
	}

#if defined(PS2_EXTERNAL_IRX_IMAGE)
	if (ps2_drivers_img_staged_modules() != 0u ||
		ps2_drivers_img_staged_bytes() != 0u) {
		printf("[ps2_drivers] startup left staged modules=%u bytes=%u\n",
			(unsigned int)ps2_drivers_img_staged_modules(),
			(unsigned int)ps2_drivers_img_staged_bytes());
		deinit_joystick_driver(true);
		ps2_drivers_img_discard_staged();
		ps2_drivers_img_forget_source();
		free(ps2);
		return NULL;
	}

	ps2_drivers_img_forget_source();
#endif

	memset(joyInfo, 0, sizeof(joyInfo));

	for (port = 0; port < PS2_MAX_PORT; port++)
		ps2->mtap_opened[port] = (mtapPortOpen(port) == 1);

	refreshJoyInfo(ps2);

	return ps2;
}

static void ps2_free(void *data)
{
	ps2_input_t *ps2 = (ps2_input_t*)data;
	uint32_t i = 0;

	if (ps2 == NULL)
		return;

	for (i = 0; i < MAX_CONTROLLERS; i++) {
		struct JoyInfo *info = &joyInfo[i];
		closeJoyInfo(info);
	}

	for (i = 0; i < PS2_MAX_PORT; i++) {
		if (ps2->mtap_opened[i])
			mtapPortClose(i);
	}

	deinit_joystick_driver(true);

	free(ps2);
}

static bool ps2_sample(void *data, uint32_t controller, input_state_t *state) {
	ps2_input_t *ps2 = (ps2_input_t*)data;
	struct padButtonStatus paddata = {0};
	uint32_t buttons = 0;
	int32_t pressed_buttons, ret;
	struct JoyInfo *info = NULL;

	if (ps2 == NULL || state == NULL)
		return false;

	info = getActiveJoyInfo(ps2, controller);
	if (info == NULL)
		return false;

	ret = padRead(info->port, info->slot, &paddata);
	if (ret == 0)
		return false;

	pressed_buttons = 0xffff ^ paddata.btns;

	buttons |= (pressed_buttons & PAD_UP) ? PLATFORM_PAD_UP : 0;
	buttons |= (pressed_buttons & PAD_DOWN) ? PLATFORM_PAD_DOWN : 0;
	buttons |= (pressed_buttons & PAD_LEFT) ? PLATFORM_PAD_LEFT : 0;
	buttons |= (pressed_buttons & PAD_RIGHT) ? PLATFORM_PAD_RIGHT : 0;

	buttons |= (pressed_buttons & PAD_CIRCLE) ? PLATFORM_PAD_B1 : 0;
	buttons |= (pressed_buttons & PAD_CROSS) ? PLATFORM_PAD_B2 : 0;
	buttons |= (pressed_buttons & PAD_SQUARE) ? PLATFORM_PAD_B3 : 0;
	buttons |= (pressed_buttons & PAD_TRIANGLE) ? PLATFORM_PAD_B4 : 0;

	buttons |= (pressed_buttons & PAD_L1) ? PLATFORM_PAD_L : 0;
	buttons |= (pressed_buttons & PAD_R1) ? PLATFORM_PAD_R : 0;

	buttons |= (pressed_buttons & PAD_START) ? PLATFORM_PAD_START : 0;
	buttons |= (pressed_buttons & PAD_SELECT) ? PLATFORM_PAD_SELECT : 0;

	state->buttons = buttons;
	/* Zeroed analog bytes mean the pad is not reporting analog-stick data.
	 * Leave the common neutral axes untouched in that case. */
	if (paddata.ljoy_h || paddata.ljoy_v || paddata.rjoy_h || paddata.rjoy_v) {
		state->lx = paddata.ljoy_h;
		state->ly = paddata.ljoy_v;
		state->axis_flags = INPUT_AXIS_LX | INPUT_AXIS_LY;
	}

	return true;
}


input_driver_t input_ps2 = {
	"ps2",
	ps2_init,
	ps2_free,
	ps2_controllerCount,
	ps2_sample,
};
