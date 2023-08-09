#include <stdio.h>
#include <stdlib.h>
#include <libpad.h>
#include "common/input_driver.h"

#define PS2_MAX_PORT      2 /* each ps2 has 2 ports */
#define PS2_MAX_SLOT      4 /* maximum - 4 slots in one multitap */
#define MAX_CONTROLLERS   (PS2_MAX_PORT * PS2_MAX_SLOT)
#define PS2_ANALOG_STICKS 2
#define PS2_ANALOG_AXIS   2
#define PS2_BUTTONS       16
#define PS2_TOTAL_AXIS    (PS2_ANALOG_STICKS * PS2_ANALOG_AXIS)

#define tolerance 0x30

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
	uint8_t enabled_pads;
} ps2_input_t;

static void *ps2_init(void) {
	ps2_input_t *ps2 = (ps2_input_t*)calloc(1, sizeof(ps2_input_t));

	uint32_t port = 0;
    uint32_t slot = 0;

    if (init_joystick_driver(true) < 0) {
		free(ps2);
        return NULL;
    }

    for (port = 0; port < PS2_MAX_PORT; port++) {
        mtapPortOpen(port);
    }
    /* it can fail - we dont care, we will check it more strictly when padPortOpen */

    for (slot = 0; slot < PS2_MAX_SLOT; slot++) {
        for (port = 0; port < PS2_MAX_PORT; port++) {
            /* 2 main controller ports acts the same with and without multitap
            Port 0,0 -> Connector 1 - the same as Port 0
            Port 1,0 -> Connector 2 - the same as Port 1
            Port 0,1 -> Connector 3
            Port 1,1 -> Connector 4
            Port 0,2 -> Connector 5
            Port 1,2 -> Connector 6
            Port 0,3 -> Connector 7
            Port 1,3 -> Connector 8
            */

            struct JoyInfo *info = &joyInfo[ps2->enabled_pads];
            if (padPortOpen(port, slot, (void *)info->padBuf) > 0) {
                info->port = (uint8_t)port;
                info->slot = (uint8_t)slot;
                info->opened = 1;
                ps2->enabled_pads++;
            }
        }
	}
	
	return ps2;
}

static void ps2_free(void *data) {
	ps2_input_t *ps2 = (ps2_input_t*)data;
	uint32_t i = 0;

	for (i = 0; i < MAX_CONTROLLERS; i++) {
		struct JoyInfo *info = &joyInfo[i];
		if (info->opened) {
			padPortClose(info->port, info->slot);
		}
	}

	deinit_joystick_driver(true);

	free(ps2);
}

static struct  JoyInfo *getFirstJoyInfo(uint32_t pad){
	uint32_t i;
	struct JoyInfo *info = NULL;

	for (i = 0; i < MAX_CONTROLLERS; i++) {
		info = &joyInfo[i];
		if (info->opened) {
				return info;
		}
	}

	return NULL;	
}

static uint32_t basicPoll(struct padButtonStatus *paddata, bool exclusive) {
	uint32_t data = 0;
	struct JoyInfo *info = NULL;

	info = getFirstJoyInfo(0);
	if (info == NULL) {
		return data;
	}
	// Read just first controller for now
	if (padRead(info->port, info->slot, paddata) == 0) {
		data |= (paddata->btns & PAD_UP) ? PLATFORM_PAD_UP : 0;
		data |= (paddata->btns & PAD_DOWN) ? PLATFORM_PAD_DOWN : 0;
		data |= (paddata->btns & PAD_LEFT) ? PLATFORM_PAD_LEFT : 0;
		data |= (paddata->btns & PAD_RIGHT) ? PLATFORM_PAD_RIGHT : 0;

		data |= (paddata->btns & PAD_CIRCLE) ? PLATFORM_PAD_B1 : 0;
		data |= (paddata->btns & PAD_CROSS) ? PLATFORM_PAD_B2 : 0;
		data |= (paddata->btns & PAD_SQUARE) ? PLATFORM_PAD_B3 : 0;
		data |= (paddata->btns & PAD_TRIANGLE) ? PLATFORM_PAD_B4 : 0;

		data |= (paddata->btns & PAD_L1) ? PLATFORM_PAD_L : 0;
		data |= (paddata->btns & PAD_R1) ? PLATFORM_PAD_R : 0;
		
		data |= (paddata->btns & PAD_START) ? PLATFORM_PAD_START : 0;
		data |= (paddata->btns & PAD_SELECT) ? PLATFORM_PAD_SELECT : 0;

		if ((paddata->ljoy_v >= 0xd0) && !(exclusive && (paddata->btns & PAD_UP))) data |=  PLATFORM_PAD_DOWN;
		if ((paddata->ljoy_v <= 0x30) && !(exclusive && (paddata->btns & PAD_DOWN))) data |=  PLATFORM_PAD_UP;
		if ((paddata->ljoy_h <= 0x30) && !(exclusive && (paddata->btns & PAD_LEFT))) data |=  PLATFORM_PAD_LEFT;
		if ((paddata->ljoy_h >= 0xd0) && !(exclusive && (paddata->btns & PAD_RIGHT))) data |=  PLATFORM_PAD_RIGHT;
	}

	return data;
}

static uint32_t ps2_poll(void *data) {
	ps2_input_t *ps2 = (ps2_input_t*)data;
	struct padButtonStatus paddata;
	uint32_t btnsData = 0;

	if (ps2->enabled_pads == 0) {
		return btnsData;
	}

	btnsData = basicPoll(&paddata, false);

	return btnsData;
}

#if (EMU_SYSTEM == MVS)
static uint32_t ps2_pollFatfursp(void *data) {
	struct padButtonStatus paddata;
	uint32_t btnsData = 0;

	btnsData = basicPoll(&paddata, true);

	return btnsData;
}

static uint32_t ps2_pollAnalog(void *data) {
	uint32_t btnsData;
	struct padButtonStatus paddata;

	btnsData = basicPoll(&paddata, false);

	btnsData  = paddata.btns & 0xffff;
	btnsData |= paddata.ljoy_h << 16;
	btnsData |= paddata.ljoy_v << 24;

	return btnsData;
}
#endif


input_driver_t input_ps2 = {
	"ps2",
	ps2_init,
	ps2_free,
	ps2_poll,
#if (EMU_SYSTEM == MVS)
	ps2_pollFatfursp,
	ps2_pollAnalog,
#endif
};