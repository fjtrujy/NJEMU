#include <stdio.h>
#include <stdlib.h>
#include <pspctrl.h>
#include "common/input_driver.h"

typedef struct psp_input {
	int32_t channel;
} psp_input_t;

static void *psp_init(void) {
	psp_input_t *psp = (psp_input_t*)calloc(1, sizeof(psp_input_t));

	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
	
	return psp;
}

static void psp_free(void *data) {
	psp_input_t *psp = (psp_input_t*)data;
	free(psp);
}

static uint32_t psp_controllerCount(void *data) {
	return data ? 1 : 0;
}

static uint32_t mapButtons(const SceCtrlData *paddata) {
	uint32_t data = 0;

	data |= (paddata->Buttons & PSP_CTRL_UP) ? PLATFORM_PAD_UP : 0;
	data |= (paddata->Buttons & PSP_CTRL_DOWN) ? PLATFORM_PAD_DOWN : 0;
	data |= (paddata->Buttons & PSP_CTRL_LEFT) ? PLATFORM_PAD_LEFT : 0;
	data |= (paddata->Buttons & PSP_CTRL_RIGHT) ? PLATFORM_PAD_RIGHT : 0;

	data |= (paddata->Buttons & PSP_CTRL_CIRCLE) ? PLATFORM_PAD_B1 : 0;
	data |= (paddata->Buttons & PSP_CTRL_CROSS) ? PLATFORM_PAD_B2 : 0;
	data |= (paddata->Buttons & PSP_CTRL_SQUARE) ? PLATFORM_PAD_B3 : 0;
	data |= (paddata->Buttons & PSP_CTRL_TRIANGLE) ? PLATFORM_PAD_B4 : 0;

	data |= (paddata->Buttons & PSP_CTRL_LTRIGGER) ? PLATFORM_PAD_L : 0;
	data |= (paddata->Buttons & PSP_CTRL_RTRIGGER) ? PLATFORM_PAD_R : 0;
	
	data |= (paddata->Buttons & PSP_CTRL_START) ? PLATFORM_PAD_START : 0;
	data |= (paddata->Buttons & PSP_CTRL_SELECT) ? PLATFORM_PAD_SELECT : 0;

	return data;
}

static bool psp_sample(void *data, uint32_t controller, input_state_t *state) {
	SceCtrlData paddata = {0};
	(void)data;

	if (controller != 0 || state == NULL)
		return false;

	if (sceCtrlPeekBufferPositive(&paddata, 1) <= 0)
		return false;

	state->buttons = mapButtons(&paddata);
	state->lx = paddata.Lx;
	state->ly = paddata.Ly;
	state->axis_flags = INPUT_AXIS_LX | INPUT_AXIS_LY;
	return true;
}


input_driver_t input_psp = {
	"psp",
	psp_init,
	psp_free,
	psp_controllerCount,
	psp_sample,
};
