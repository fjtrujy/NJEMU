#include <assert.h>

#include "common/ui_text_ids.h"
#include "common/ui_language.h"

int main(void)
{
	/* Stable sentinels across the namespace, including all normalized groups. */
	assert(EOM == 0);
	assert(DISPLAY_ORIGINAL_SIZE == 71);
	assert(DISPLAY_RESERVED_3 == 77);
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
	assert(SYSTEM_VIDEO_SETTINGS_MENU == 380);
	assert(VIDEO_OUTPUT_480P_LABEL == 386);
	assert(SYSTEM_PERFORMANCE_SETTINGS_MENU == 387);
	assert(CACHE_READ_SIZE_AUTO_LABEL == 391);
	assert(VIDEO_BACKEND == 392);
	assert(MENU_VIDEO_BACKEND_SETTINGS == 393);
	assert(MENUHELP_VIDEO_BACKEND_SETTINGS == 394);
	assert(FPS_OVERLAY_SETTINGS_MENU == 395);
	assert(FPS_X_OFFSET == 396);
	assert(FPS_Y_OFFSET == 397);
	assert(MENU_FPS_OVERLAY_SETTINGS == 398);
	assert(MENUHELP_FPS_OVERLAY_SETTINGS == 399);
	assert(AUDIO_PROCESSOR == 400);
	assert(AUDIO_PROCESSOR_AUTO_LABEL == 401);
	assert(AUDIO_PROCESSOR_MAIN_CPU_LABEL == 402);
	assert(AUDIO_PROCESSOR_MEDIA_ENGINE_LABEL == 403);
	assert(MENU_AUDIO_PROCESSOR_SETTINGS == 404);
	assert(MENUHELP_AUDIO_PROCESSOR_SETTINGS == 405);
	assert(ADAPTIVE == 406);
	assert(VIDEO_X_OFFSET == 407);
	assert(VIDEO_Y_OFFSET == 408);
	assert(UI_TEXT_MAX == 409);
	assert(UI_LANG_ENGLISH == 0);
	assert(UI_LANG_JAPANESE == 1);
	assert(UI_LANG_SPANISH == 2);
	assert(UI_LANG_CHINESE_SIMPLIFIED == 3);
	assert(UI_LANG_CHINESE_TRADITIONAL == 4);
	assert(UI_LANG_COUNT == 5);
	return 0;
}
