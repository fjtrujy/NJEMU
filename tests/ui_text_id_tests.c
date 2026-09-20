#include <assert.h>

#include "common/ui_text_ids.h"

int main(void)
{
	/* Stable sentinels across the namespace, including all normalized groups. */
	assert(EOM == 0);
	assert(STRETCH_320X224_4_3 == 71);
	assert(STRETCH_480X270_16_9 == 77);
	assert(INPUT_BUTTON_1 == 159);
	assert(INPUT_BUTTON_A == 165);
	assert(AUTOFIRE_1 == 175);
	assert(AUTOFIRE_A == 181);
	assert(MENUHELP_RESET_EMULATION_CPS1 == 253);
	assert(MENUHELP_RESET_EMULATION_NCDZ == 256);
	assert(ROMINFO_NOT_FOUND_CPS1 == 347);
	assert(ROMINFO_NOT_FOUND_MVS == 349);
	assert(END_OF_TEXT == 376);
	assert(UI_TEXT_MAX == 377);
	return 0;
}
