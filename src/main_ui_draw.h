#ifndef MAIN_UI_DRAW_H
#define MAIN_UI_DRAW_H

int ui_init(void);
void ui_exit(void);

void small_font_print(int sx, int sy, const char *s, int bg);
void small_font_printf(int x, int y, const char *text, ...);

void small_icon_shadow(int sx, int sy, int r, int g, int b, int no);
void small_icon(int sx, int sy, int r, int g, int b, int no);

void boxfill_alpha(int sx, int sy, int ex, int ey, int r, int g, int b, int alpha);


void uifont_print_center(int sy, int r, int g, int b, const char *s);
void uifont_print(int sx, int sy, int r, int g, int b, const char *s);
void uifont_print_shadow(int sx, int sy, int r, int g, int b, const char *s);
void uifont_print_shadow_center(int sy, int r, int g, int b, const char *s);
int uifont_get_string_width(const char *s);

void draw_dialog(int sx, int sy, int ex, int ey);
int draw_volume_status(int draw);
int draw_battery_status(int draw);


int ui_show_popup(int draw);
void ui_popup(const char *text, ...);
void ui_popup_reset(void);

void load_background(int number);
void show_background(void);
void showmenu(void);


int load_png(const char *name, int number);
int save_png(const char *path);

void file_browser(void);

void msg_printf(const char *text, ...);

void msg_screen_init(int wallpaper, int icon, const char *title);
void msg_screen_clear(void);

void load_gamecfg(const char *name);
void save_gamecfg(const char *name);

void show_exit_screen(void);

int help(int number);

void delete_files(const char *dirname, const char *pattern);

#endif /* MAIN_UI_DRAW_H */
