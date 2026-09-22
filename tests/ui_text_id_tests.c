#include <assert.h>

#include "common/ui_text_ids.h"
#include "common/ui_language.h"

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
	assert(CACHE_USAGE_GFX == 376);
	assert(CACHE_USAGE_CROM == 377);
	assert(CACHE_USAGE_PCM == 378);
	assert(END_OF_TEXT == 379);
	assert(UI_TEXT_MAX == 380);
	assert(UI_LANG_ENGLISH == 0);
	assert(UI_LANG_JAPANESE == 1);
	assert(UI_LANG_SPANISH == 2);
	assert(UI_LANG_CHINESE_SIMPLIFIED == 3);
	assert(UI_LANG_CHINESE_TRADITIONAL == 4);
	assert(UI_LANG_COUNT == 5);
	return 0;
}
