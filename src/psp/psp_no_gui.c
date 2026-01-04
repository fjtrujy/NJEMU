/******************************************************************************

	ui.c

	User Interface Processing

******************************************************************************/

#include "psp.h"
#include "emumain.h"
#include "stdarg.h"


UI_PALETTE ui_palette[UI_PAL_MAX] =
{
	{ 255, 255, 255 },	// UI_PAL_TITLE
	{ 255, 255, 255 },	// UI_PAL_SELECT
	{ 180, 180, 180 },	// UI_PAL_NORMAL
	{ 255, 255,  64 },	// UI_PAL_INFO
	{ 255,  64,  64 },	// UI_PAL_WARNING
	{  48,  48,  48 },	// UI_PAL_BG1
	{   0,   0, 160 },	// UI_PAL_BG2
	{   0,   0,   0 },	// UI_PAL_FRAME
	{  40,  40,  40 },	// UI_PAL_FILESEL1
	{ 120, 120, 120 }	// UI_PAL_FILESEL2
};

int cheat_num = 0;
gamecheat_t* gamecheat[MAX_CHEATS];

void msg_printf(const char *text, ...) {
	// Let's use directly printf instead
	va_list args;
    va_start(args, text);

    vprintf(text, args);  // Use vprintf to handle variable arguments

    va_end(args);
}

void show_progress(const char *text)
{
	printf("show_progress: %s\n", text);
}

void update_progress(void)
{
}

void showmenu(void)
{
}

int draw_volume_status(int draw) {
	return 0;
}

int draw_battery_status(int draw) {
	return 0;	
}

void msg_screen_clear(void) {

}

void show_exit_screen(void) {

}

void load_background(int number)
{

}

int ui_show_popup(int draw) {
	return 0;
}

void file_browser(void) {
	Loop = LOOP_EXEC;
	strcpy(game_dir, "roms");
#if USE_CACHE
	sprintf(cache_dir, "cache");
#endif
	// Get the game name from a file called game_name.ini
	FILE *fp = fopen("game_name.ini", "r");
	if (fp) {
		fgets(game_name, sizeof(game_name), fp);
		fclose(fp);
	}
#if (EMU_SYSTEM == NCDZ)
	strcat(game_dir, "/");
	strcat(game_dir, game_name);

	sprintf(mp3_dir, "%s/mp3", game_dir);
#endif
	emu_main();
}

void small_font_print(int sx, int sy, const char *s, int bg) {

}

void uifont_print_center(int sy, int r, int g, int b, const char *s) {

}

void uifont_print_shadow(int sx, int sy, int r, int g, int b, const char *s) {

}

void textfont_print(int sx, int sy, int r, int g, int b, const char *s, int flag) {
	printf("textfont_print: %s\n", s);
}

int uifont_get_string_width(const char *s) {
	return 1;
}

void uifont_print_shadow_center(int sy, int r, int g, int b, const char *s) {
	printf("uifont_print_shadow_center: %s\n", s);
}

void uifont_print(int sx, int sy, int r, int g, int b, const char *s) {
	printf("uifont_print: %s\n", s);
}

void small_icon_shadow(int sx, int sy, int r, int g, int b, int no) {

}

void show_background(void) {

}

void boxfill_alpha(int sx, int sy, int ex, int ey, int r, int g, int b, int alpha) {

}

void ui_popup_reset(void) {

}

void draw_dialog(int sx, int sy, int ex, int ey) {

}

int save_png(const char *path) {
	return 0;
}

void msg_screen_init(int wallpaper, int icon, const char *title) {

}

void draw_scrollbar(int sx, int sy, int ex, int ey, int disp_lines, int total_lines, int current_line) {

}

void ui_popup(const char *text, ...) {

}

int help(int number) {
	return 0;
}

void save_gamecfg(const char *name) {

}

void ui_init(void) {

}

void load_gamecfg(const char *name) {

}

void delete_files(const char *dirname, const char *pattern) {

}

void small_icon(int sx, int sy, int r, int g, int b, int no) {

}