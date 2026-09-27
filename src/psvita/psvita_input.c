/******************************************************************************

	psvita_input.c

	PS Vita controller sampling

******************************************************************************/

#include <stdlib.h>
#include <psp2/ctrl.h>
#include "common/input_driver.h"

typedef struct psvita_input {
	int unused;
} psvita_input_t;

static void *psvita_input_init(void)
{
	psvita_input_t *psvita = calloc(1, sizeof(psvita_input_t));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	return psvita;
}

static void psvita_input_free(void *data)
{
	free(data);
}

static uint32_t psvita_input_controllerCount(void *data)
{
	return data ? 1 : 0;
}

static uint32_t map_buttons(const SceCtrlData *pad)
{
	uint32_t data = 0;
	data |= (pad->buttons & SCE_CTRL_UP) ? PLATFORM_PAD_UP : 0;
	data |= (pad->buttons & SCE_CTRL_DOWN) ? PLATFORM_PAD_DOWN : 0;
	data |= (pad->buttons & SCE_CTRL_LEFT) ? PLATFORM_PAD_LEFT : 0;
	data |= (pad->buttons & SCE_CTRL_RIGHT) ? PLATFORM_PAD_RIGHT : 0;
	data |= (pad->buttons & SCE_CTRL_CROSS) ? PLATFORM_PAD_B1 : 0;
	data |= (pad->buttons & SCE_CTRL_CIRCLE) ? PLATFORM_PAD_B2 : 0;
	data |= (pad->buttons & SCE_CTRL_SQUARE) ? PLATFORM_PAD_B3 : 0;
	data |= (pad->buttons & SCE_CTRL_TRIANGLE) ? PLATFORM_PAD_B4 : 0;
	data |= (pad->buttons & SCE_CTRL_LTRIGGER) ? PLATFORM_PAD_L : 0;
	data |= (pad->buttons & SCE_CTRL_RTRIGGER) ? PLATFORM_PAD_R : 0;
	data |= (pad->buttons & SCE_CTRL_START) ? PLATFORM_PAD_START : 0;
	data |= (pad->buttons & SCE_CTRL_SELECT) ? PLATFORM_PAD_SELECT : 0;
	return data;
}

static bool psvita_input_sample(void *data, uint32_t controller, input_state_t *state)
{
	SceCtrlData pad = { 0 };
	(void)data;

	if (controller != 0 || state == NULL)
		return false;
	if (sceCtrlPeekBufferPositive(0, &pad, 1) <= 0)
		return false;

	state->buttons = map_buttons(&pad);
	state->lx = pad.lx;
	state->ly = pad.ly;
	state->axis_flags = INPUT_AXIS_LX | INPUT_AXIS_LY;
	return true;
}

input_driver_t input_psvita = {
	"psvita",
	psvita_input_init,
	psvita_input_free,
	psvita_input_controllerCount,
	psvita_input_sample,
};
