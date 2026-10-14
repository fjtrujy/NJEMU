#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <libpad.h>
#include <libmtap.h>
#include <ps2_joystick_driver.h>
#if defined(PS2_EXTERNAL_IRX_IMAGE)
#include <ps2_drivers_img.h>
#endif
#include "common/input_driver.h"
#include "ps2/ps2_input_sample_cache.h"
#include "ps2/ps2_pad_presence.h"
#include "ps2/ps2_pad_packet.h"

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
    ps2_input_sample_cache_t sample_cache;
    uint16_t btns;
    uint8_t analog_state[PS2_TOTAL_AXIS];
    uint8_t port;
    uint8_t slot;
    uint8_t analog_mode_configured;
    uint8_t was_ready;
    int8_t rumble_ready;
    int8_t opened;
} __attribute__((aligned(64)));

struct JoyInfo joyInfo[MAX_CONTROLLERS];

typedef struct ps2_input {
	uint8_t mtap_opened[PS2_MAX_PORT];
} ps2_input_t;

static uint32_t decodePadButtons(uint16_t pad_buttons)
{
	uint32_t buttons = 0;
	uint32_t pressed = 0xffffu ^ pad_buttons;
	buttons |= (pressed & PAD_UP) ? PLATFORM_PAD_UP : 0;
	buttons |= (pressed & PAD_DOWN) ? PLATFORM_PAD_DOWN : 0;
	buttons |= (pressed & PAD_LEFT) ? PLATFORM_PAD_LEFT : 0;
	buttons |= (pressed & PAD_RIGHT) ? PLATFORM_PAD_RIGHT : 0;
	buttons |= (pressed & PAD_CIRCLE) ? PLATFORM_PAD_B1 : 0;
	buttons |= (pressed & PAD_CROSS) ? PLATFORM_PAD_B2 : 0;
	buttons |= (pressed & PAD_SQUARE) ? PLATFORM_PAD_B3 : 0;
	buttons |= (pressed & PAD_TRIANGLE) ? PLATFORM_PAD_B4 : 0;
	buttons |= (pressed & PAD_L1) ? PLATFORM_PAD_L : 0;
	buttons |= (pressed & PAD_R1) ? PLATFORM_PAD_R : 0;
	buttons |= (pressed & PAD_START) ? PLATFORM_PAD_START : 0;
	buttons |= (pressed & PAD_SELECT) ? PLATFORM_PAD_SELECT : 0;
	return buttons;
}

static void decodePadState(const struct padButtonStatus *pad, input_state_t *out)
{
	input_state_reset(out);
	out->buttons = decodePadButtons(pad->btns);
	if (pad->ljoy_h || pad->ljoy_v || pad->rjoy_h || pad->rjoy_v) {
		out->lx = pad->ljoy_h;
		out->ly = pad->ljoy_v;
		out->axis_flags = INPUT_AXIS_LX | INPUT_AXIS_LY;
	}
}

static struct JoyInfo *getJoyInfo(uint32_t port, uint32_t slot)
{
	return &joyInfo[port * PS2_MAX_SLOT + slot];
}

static void closeJoyInfo(struct JoyInfo *info)
{
	if (!info->opened)
		return;

	padPortClose(info->port, info->slot);
	info->sample_cache.valid = 0;
	info->sample_cache.missed = 0;
	info->was_ready = 0;
	info->analog_mode_configured = 0;
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

static int32_t joyInfoState(struct JoyInfo *info)
{
	int32_t state;

	if (!info->opened)
		return PAD_STATE_DISCONN;

	state = padGetState(info->port, info->slot);
	if (state == PAD_STATE_DISCONN || state == PAD_STATE_ERROR ||
		state == PAD_STATE_FINDPAD)
	{
		info->analog_mode_configured = 0;
		info->sample_cache.valid = 0;
		info->sample_cache.missed = 0;
		info->was_ready = 0;
	}
	return state;
}

static bool joyInfoReady(struct JoyInfo *info)
{
	int32_t state = joyInfoState(info);
	return state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1;
}

static bool joyInfoSelectable(struct JoyInfo *info)
{
	int32_t state = joyInfoState(info);
	bool ready = state == PAD_STATE_STABLE || state == PAD_STATE_FINDCTP1;
	return ps2_pad_presence_update(&info->was_ready, info->opened,
		ready, state == PAD_STATE_EXECCMD,
		state == PAD_STATE_DISCONN || state == PAD_STATE_ERROR ||
		state == PAD_STATE_FINDPAD);
}

static bool configureJoyInfoAnalogMode(struct JoyInfo *info)
{
	int modes;
	int mode;
	int32_t state;

	if (info->analog_mode_configured)
		return true;
	if (!joyInfoReady(info))
		return false;
	state = padGetState(info->port, info->slot);

	/* A DualShock can power up in digital mode independently on every port
	 * (and multitap slot). Explicitly enable analog mode for each physical pad
	 * instead of relying on the controller's previous/default mode. */
	modes = padInfoMode(info->port, info->slot, PAD_MODETABLE, -1);
	for (mode = 0; mode < modes; mode++) {
		if (padInfoMode(info->port, info->slot, PAD_MODETABLE, mode) ==
			PAD_TYPE_DUALSHOCK) {
			if (padInfoMode(info->port, info->slot, PAD_MODECURID, 0) ==
				PAD_TYPE_DUALSHOCK) {
				info->analog_mode_configured = 1;
				return true;
			}

			/* A successful mode request is asynchronous. Skip this sample and
			 * wait for PADMAN to become stable again before reading axes. */
			if (padSetMainMode(info->port, info->slot,
				PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK) == 1)
				return false;

			/* Keep buttons usable if the request fails, but leave the mode
			 * unconfigured so a later sample can retry the analog transition. */
			return true;
		}
	}

	/* A newly connected pad can report FINDCTP1 before its mode table is
	 * populated. Retry after it becomes stable instead of caching it as a
	 * digital-only controller too early. */
	if (state == PAD_STATE_STABLE)
		info->analog_mode_configured = 1;
	return true;
}

static uint32_t activeJoyInfoCount(void)
{
	uint32_t count = 0;
	uint32_t slot;

	for (slot = 0; slot < PS2_MAX_SLOT; slot++) {
		uint32_t port;

		for (port = 0; port < PS2_MAX_PORT; port++) {
			struct JoyInfo *info = getJoyInfo(port, slot);
			if (joyInfoSelectable(info))
				count++;
		}
	}

	return count;
}

static struct JoyInfo *getActiveJoyInfo(uint32_t controller)
{
	uint32_t active_index = 0;
	uint32_t slot;

	/* Preserve the physical connector order used by the old backend:
	 * (0,0), (1,0), (0,1), (1,1), ... */
	for (slot = 0; slot < PS2_MAX_SLOT; slot++) {
		uint32_t port;

		for (port = 0; port < PS2_MAX_PORT; port++) {
			struct JoyInfo *info = getJoyInfo(port, slot);
			if (joyInfoSelectable(info)) {
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

	if (!ps2)
		return 0;

	return activeJoyInfoCount();
}

static void ps2_refresh(void *data)
{
	ps2_input_t *ps2 = (ps2_input_t *)data;

	if (ps2) {
		refreshJoyInfo(ps2);
	}
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
	input_state_t fresh;
	bool valid;
	int32_t ret;
	struct JoyInfo *info = NULL;

	if (ps2 == NULL || state == NULL)
		return false;

	info = getActiveJoyInfo(controller);
	if (info == NULL) {
		return false;
	}
	if (!configureJoyInfoAnalogMode(info)) {
		valid = ps2_input_sample_cache_update(&info->sample_cache, true,
			false, NULL, state);
		return valid;
	}

	ret = padRead(info->port, info->slot, &paddata);
	/* A positive short packet must not turn zero-initialized btns into
	 * a spurious 0xffff set of pressed buttons. */
	if (ret < (int)(offsetof(struct padButtonStatus, btns) +
		sizeof(paddata.btns))) {
		valid = ps2_input_sample_cache_update(&info->sample_cache, true,
			false, NULL, state);
		return valid;
	}

	if (!ps2_pad_packet_valid(ret,
		offsetof(struct padButtonStatus, btns) + sizeof(paddata.btns),
		paddata.mode, paddata.btns)) {
		valid = ps2_input_sample_cache_update(&info->sample_cache, true,
			false, NULL, state);
		return valid;
	}

	decodePadState(&paddata, &fresh);

	valid = ps2_input_sample_cache_update(&info->sample_cache, true,
		true, &fresh, state);
	return valid;
}

input_driver_t input_ps2 = {
	"ps2",
	ps2_init,
	ps2_free,
	ps2_refresh,
	ps2_controllerCount,
	ps2_sample,
};
