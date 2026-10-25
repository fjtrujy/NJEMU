/******************************************************************************

	psvita_input.c

	PS Vita controller sampling

******************************************************************************/

#include <stdlib.h>
#include <psp2/apputil.h>
#include <psp2/ctrl.h>
#include <psp2/system_param.h>
#include "common/input_driver.h"

typedef struct psvita_input {
	/* Follows the console's enter button: cross on western systems, circle on
	 * Japanese ones. The accept button is PLATFORM_PAD_B1, cancel B2. */
	bool accept_is_cross;
} psvita_input_t;

static void *psvita_input_init(void)
{
	psvita_input_t *psvita = calloc(1, sizeof(psvita_input_t));
	int enter_button = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;

	if (psvita == NULL)
		return NULL;
	/* The platform has already initialised SceAppUtil. */
	sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &enter_button);
	psvita->accept_is_cross = enter_button != SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE;
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

static uint32_t map_buttons(const psvita_input_t *psvita, const SceCtrlData *pad)
{
	const uint32_t cross = psvita->accept_is_cross ? PLATFORM_PAD_B1 : PLATFORM_PAD_B2;
	const uint32_t circle = psvita->accept_is_cross ? PLATFORM_PAD_B2 : PLATFORM_PAD_B1;
	uint32_t data = 0;
	data |= (pad->buttons & SCE_CTRL_UP) ? PLATFORM_PAD_UP : 0;
	data |= (pad->buttons & SCE_CTRL_DOWN) ? PLATFORM_PAD_DOWN : 0;
	data |= (pad->buttons & SCE_CTRL_LEFT) ? PLATFORM_PAD_LEFT : 0;
	data |= (pad->buttons & SCE_CTRL_RIGHT) ? PLATFORM_PAD_RIGHT : 0;
	data |= (pad->buttons & SCE_CTRL_CROSS) ? cross : 0;
	data |= (pad->buttons & SCE_CTRL_CIRCLE) ? circle : 0;
	data |= (pad->buttons & SCE_CTRL_SQUARE) ? PLATFORM_PAD_B3 : 0;
	data |= (pad->buttons & SCE_CTRL_TRIANGLE) ? PLATFORM_PAD_B4 : 0;
	data |= (pad->buttons & SCE_CTRL_LTRIGGER) ? PLATFORM_PAD_L : 0;
	data |= (pad->buttons & SCE_CTRL_RTRIGGER) ? PLATFORM_PAD_R : 0;
	data |= (pad->buttons & SCE_CTRL_START) ? PLATFORM_PAD_START : 0;
	data |= (pad->buttons & SCE_CTRL_SELECT) ? PLATFORM_PAD_SELECT : 0;
	return data;
}

static bool psvita_input_acceptIsCross(void *data)
{
	return ((psvita_input_t *)data)->accept_is_cross;
}

static bool psvita_input_sample(void *data, uint32_t controller, input_state_t *state)
{
	SceCtrlData pad = { 0 };

	if (controller != 0 || state == NULL)
		return false;
	if (sceCtrlPeekBufferPositive(0, &pad, 1) <= 0)
		return false;

	state->buttons = map_buttons(data, &pad);
	state->lx = pad.lx;
	state->ly = pad.ly;
	state->axis_flags = INPUT_AXIS_LX | INPUT_AXIS_LY;
	return true;
}

input_driver_t input_psvita = {
	"psvita",
	psvita_input_init,
	psvita_input_free,
	NULL,
	psvita_input_controllerCount,
	psvita_input_sample,
	NULL,
	psvita_input_acceptIsCross,
};
