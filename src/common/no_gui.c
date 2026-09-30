/******************************************************************************

	ui.c

	User Interface Processing

******************************************************************************/

#include "emucfg.h"
#include "common/emulator_runtime.h"
#include "common/runtime_paths.h"
#include "common/ui_defs.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#if (EMU_SYSTEM == NCDZ)
#include <strings.h>
#include "common/mp3.h"
#include "ncdz/resource_source.h"
#endif

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

void init_progress(int total, const char *text)
{
	(void)total;
	(void)text;
}

void update_progress(void)
{
}

void showmenu(void)
{
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
	{
		int fd = open("game_name.ini", O_RDONLY);
		if (fd >= 0) {
			ssize_t n = read(fd, game_name, sizeof(game_name) - 1);
			if (n > 0) {
				game_name[n] = '\0';
				char *nl = strchr(game_name, '\n');
				if (nl) *nl = '\0';
				char *cr = strchr(game_name, '\r');
				if (cr) *cr = '\0';
			}
			close(fd);
		}
	}
#if (EMU_SYSTEM == NCDZ)
	{
		const char *ext;
		char *slash;

	strcat(game_dir, "/");
	strcat(game_dir, game_name);

		resource_source_close(&ncdz_game_source);
		ext = strrchr(game_dir, '.');
		if (ext != NULL && strcasecmp(ext, ".zip") == 0)
		{
			if (!resource_source_open_zip(&ncdz_game_source, game_dir))
				return;
			strcpy(mp3_dir, game_dir);
			slash = strrchr(mp3_dir, '/');
			if (slash != NULL)
				strcpy(slash + 1, "mp3");
		}
		else
		{
			if (!resource_source_open_directory(&ncdz_game_source, game_dir))
				return;
			sprintf(mp3_dir, "%s/mp3", game_dir);
		}
	}
#endif
	emu_main();
#if (EMU_SYSTEM == NCDZ)
	resource_source_close(&ncdz_game_source);
#endif
}

void show_background(void) {

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
