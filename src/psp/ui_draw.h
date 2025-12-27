/******************************************************************************

	ui_draw.c

	PSP User Interface Drawing Functions

******************************************************************************/

#ifndef PSP_UI_DRAW_H
#define PSP_UI_DRAW_H

#include "emumain.h"

struct font_t
{
	const uint8_t *data;
	int width;
	int height;
	int pitch;
	int skipx;
	int skipy;
};

#define CHARSET_DEFAULT		0
#define CHARSET_ISO8859_1	1
#define CHARSET_LATIN1		1
#define CHARSET_SHIFTJIS	2
#define CHARSET_GBK			2

#define UI_COLOR(no)	ui_palette[no].r,ui_palette[no].g,ui_palette[no].b

#define ui_fill_frame(frame, no)		video_driver->fillFrame(video_data, frame, MAKECOL32(ui_palette[no].r,ui_palette[no].g,ui_palette[no].b))


/*------------------------------------------------------
	Tables
------------------------------------------------------*/

// User Interface Palette Data
extern UI_PALETTE ui_palette[UI_PAL_MAX];

// Small Bitmap Font Data
extern const uint8_t font_s[];

// Shadow Data
extern const uint8_t shadow[9][8][4];

// GBK Font Table
extern const uint16_t gbk_table[];


/*------------------------------------------------------
	Font Functions
------------------------------------------------------*/

// Proportional Font
int graphic_font_get_gryph(struct font_t *font, uint16_t code);
int graphic_font_get_pitch(uint16_t code);

int ascii_14p_get_gryph(struct font_t *font, uint16_t code);
int ascii_14p_get_pitch(uint16_t code);

//int jpn_h14p_get_gryph(struct font_t *font, uint16_t code);
//int jpn_h14p_get_pitch(uint16_t code);

int gbk_s14p_get_gryph(struct font_t *font, uint16_t code);
int gbk_s14p_get_pitch(uint16_t code);

// Bitmap Icon (Small)
int icon_s_get_gryph(struct font_t *font, uint16_t code);
int icon_s_get_light(struct font_t *font, uint16_t code);

// Bitmap Icon (Large)
int icon_l_get_gryph(struct font_t *font, uint16_t code);
int icon_l_get_light(struct font_t *font, uint16_t code);

// Fixed-width Font
#ifdef COMMAND_LIST
int command_font_get_gryph(struct font_t *font, uint16_t code);
int ascii_14_get_gryph(struct font_t *font, uint16_t code);
int latin1_14_get_gryph(struct font_t *font, uint16_t code);
//int jpn_h14_get_gryph(struct font_t *font, uint16_t code);
int gbk_s14_get_gryph(struct font_t *font, uint16_t code);
#endif


/*------------------------------------------------------
	Font Drawing (Text Display)
------------------------------------------------------*/

#ifdef COMMAND_LIST
void textfont_print(int sx, int sy, int r, int g, int b, const char *s, int flag);
#endif


/*------------------------------------------------------
	Icon Drawing (User Interface)
------------------------------------------------------*/

void small_icon_light(int sx, int sy, int r, int g, int b, int no);

void large_icon(int sx, int sy, int r, int g, int b, int no);
void large_icon_shadow(int sx, int sy, int r, int g, int b, int no);
void large_icon_light(int sx, int sy, int r, int g, int b, int no);

int ui_light_update(void);


/*------------------------------------------------------
	Volume Drawing (CFW 3.52 Volume Bar Only)
------------------------------------------------------*/

void draw_volume(int volume);


/*------------------------------------------------------
	Font Drawing (Small Text)
------------------------------------------------------*/

void debug_font_printf(void *frame, int x, int y, const char *text, ...);


/*------------------------------------------------------
	Line Drawing (User Interface)
------------------------------------------------------*/

void hline(int sx, int ex, int y, int r, int g, int b);
void hline_alpha(int sx, int ex, int y, int r, int g, int b, int alpha);
void hline_gradation(int sx, int ex, int y, int r1, int g1, int b1, int r2, int g2, int b2, int alpha);

void vline(int x, int sy, int ey, int r, int g, int b);
void vline_alpha(int x, int sy, int ey, int r, int g, int b, int alpha);
void vline_gradation(int x, int sy, int ey, int r1, int g1, int b1, int r2, int g2, int b2, int alpha);

void box(int sx, int sy, int ex, int ey, int r, int g, int b);

void boxfill(int sx, int sy, int ex, int ey, int r, int g, int b);
void boxfill_gradation(int sx, int sy, int ex, int ey, int r1, int g1, int b1, int r2, int g2, int b2, int alpha, int dir);

void draw_bar_shadow(void);
void draw_box_shadow(int sx, int sy, int ex, int ey);

/*------------------------------------------------------
	User Interface Color Settings
------------------------------------------------------*/

#if VIDEO_32BPP
void get_ui_color(UI_PALETTE *pal, int *r, int *g, int *b);
void set_ui_color(UI_PALETTE *pal, int r, int g, int b);
#endif

/*------------------------------------------------------
	Logo Drawing
------------------------------------------------------*/

void logo(int sx, int sy, int r, int g, int b);

#endif /* PSP_UI_DRAW_H */
