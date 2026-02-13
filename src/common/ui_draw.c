/******************************************************************************

	ui_draw.c

	Cross-platform User Interface Drawing Functions

	All GPU-specific calls are routed through ui_draw_driver so that
	platform backends (PSP/sceGu, Desktop/SDL2, PS2/gsKit) can each
	provide their own implementation.

******************************************************************************/

#include "common/ui_draw.h"
#include "common/ui_draw_driver.h"
#include "common/ui.h"
#include "stdarg.h"


/******************************************************************************
	Constants / Macros
******************************************************************************/

#define MAKECOL16(r, g, b)	(((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4))
#define GETR16(color)		(color & 0x0f)
#define GETG16(color)		((color >> 4) & 0x0f)
#define GETB16(color)		((color >> 8) & 0x0f)

#define CMD_RED				(1 << 8)
#define CMD_YELLOW			(2 << 8)
#define CMD_GREEN			(3 << 8)
#define CMD_BLUE			(4 << 8)
#define CMD_MAGENTA			(5 << 8)
#define CMD_PURPLE			(6 << 8)
#define CMD_ORANGE			(7 << 8)
#define CMD_GRAY			(8 << 8)
#define CMD_CYAN			(9 << 8)
#define CMD_PINK			(10 << 8)

#define CODE_HASCOLOR(c)	(c & 0xff00)
#define CODE_NOTFOUND		0xffff
#define CODE_UNDERBAR		0xfffe

#define isascii(c)			((c)  >= 0x20 && (c) <= 0x7e)
#define islatin1(c)			((c)  >= 0x80)
#define isgbk1(c)			(((c) >= 0x81 && (c) <= 0xfe))
#define isgbk2(c)			((c)  >= 0x40 && (c) <= 0xfe && (c) != 0x7f && (c) != 0xff)

#define NUM_SMALL_FONTS		0x60
#define MAX_STR_LEN			256

enum
{
	FONT_TYPE_CONTROL = 0,
	FONT_TYPE_ASCII,
	FONT_TYPE_GRAPHIC,
	FONT_TYPE_GBKSIMHEI,
#ifdef COMMAND_LIST
	FONT_TYPE_LATIN1,
	FONT_TYPE_COMMAND,
#endif
	FONT_TYPE_MAX
};


/******************************************************************************
	Global data
******************************************************************************/

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


/******************************************************************************
	Local data
******************************************************************************/

static int light_level = 0;

#ifdef COMMAND_LIST
static uint16_t command_font_color[11] =
{
	MAKECOL16(255,255,255),
	MAKECOL16(255, 32,  0),
	MAKECOL16(255,200,  0),
	MAKECOL16(  0,200, 80),
	MAKECOL16(  0, 64,255),
	MAKECOL16(255,  0,128),
	MAKECOL16(200,  0,200),
	MAKECOL16(255,128,  0),
	MAKECOL16(160,160,160),
	MAKECOL16( 64,200,200),
	MAKECOL16(255, 64,128)
};
#endif

/*
 * CPU-side texture buffers.
 *
 * On PSP the driver's getTextureBasePtr returns a VRAM pointer that is
 * directly written by make_font_texture / make_shadow_texture / ui_init.
 * On other platforms, getTextureBasePtr returns a CPU staging buffer and
 * the driver flushes it to the GPU in drawSprite.
 */
static uint16_t *tex_font;
static uint16_t *tex_volicon;
static uint16_t *tex_smallfont;
static uint16_t *tex_boxshadow;

#define GAUSS_WIDTH	4

static int gauss_sum;
static const int gauss_fact[12][12] = {
	{  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
	{  1,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
	{  1,  2,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
	{  1,  3,  3,  1,  0,  0,  0,  0,  0,  0,  0,  0 },
	{  1,  4,  6,  4,  1,  0,  0,  0,  0,  0,  0,  0 },
	{  1,  5, 10, 10,  5,  1,  0,  0,  0,  0,  0,  0 },
	{  1,  6, 15, 20, 15,  6,  1,  0,  0,  0,  0,  0 },
	{  1,  7, 21, 35, 35, 21,  7,  1,  0,  0,  0,  0 },
	{  1,  8, 28, 56, 70, 56, 28,  8,  1,  0,  0,  0 },
	{  1,  9, 36, 84,126,126, 84, 36,  9,  1,  0,  0 },
	{  1, 10, 45,120,210,252,210,120, 45, 10,  1,  0 },
	{  1, 11, 55,165,330,462,462,330,165, 55, 11,  1 }
};

/******************************************************************************
	Initialization
******************************************************************************/

#include "common/font/volume_icon.c"

void ui_init(void)
{
	int code, x, y, alpha;
	uint16_t *dst;
	uint16_t color[8] = {
		MAKECOL15(248,248,248),
		MAKECOL15(240,240,240),
		MAKECOL15(232,232,232),
		MAKECOL15(224,224,224),
		MAKECOL15(216,216,216),
		MAKECOL15(208,208,208),
		MAKECOL15(200,200,200),
		MAKECOL15(192,192,192)
	};

	/* Initialize the driver — allocates platform-specific texture storage */
	ui_draw_data = ui_draw_driver->init(video_data);

	/* Get CPU-writable base pointer for the font scratch texture */
	tex_font = ui_draw_driver->getTextureBasePtr(ui_draw_data, UI_TEXTURE_FONT);

	/* Clear the font scratch area */
	ui_draw_driver->clearTexture(ui_draw_data, UI_TEXTURE_FONT, BUF_WIDTH, 48, BUF_WIDTH);

	/* Volume icons (conditional on platform support) */
	if (platform_driver->getDevkitVersion(platform_data) >= 0x03050210)
	{
		tex_volicon = ui_draw_driver->getTextureBasePtr(ui_draw_data, UI_TEXTURE_VOLICON);

		if (tex_volicon)
		{
			dst = tex_volicon + SPEEKER_X;
			for (y = 0; y < 32; y++)
			{
				for (x = 0; x < 32; x++)
				{
					if (x & 1)
						alpha = icon_speeker[y][(x >> 1)] >> 4;
					else
						alpha = icon_speeker[y][(x >> 1)] & 0x0f;

					dst[x] = (alpha << 12) | 0x0fff;
				}

				dst += BUF_WIDTH;
			}

			dst = tex_volicon + SPEEKER_SHADOW_X;
			for (y = 0; y < 32; y++)
			{
				for (x = 0; x < 32; x++)
				{
					if (x & 1)
						alpha = icon_speeker_shadow[y][(x >> 1)] >> 4;
					else
						alpha = icon_speeker_shadow[y][(x >> 1)] & 0x0f;

					dst[x] = alpha << 12;
				}

				dst += BUF_WIDTH;
			}

			dst = tex_volicon + VOLUME_BAR_X;
			for (y = 0; y < 32; y++)
			{
				for (x = 0; x < 12; x++)
				{
					if (x & 1)
						alpha = icon_bar[y][(x >> 1)] >> 4;
					else
						alpha = icon_bar[y][(x >> 1)] & 0x0f;

					dst[x] = (alpha << 12) | 0x0fff;
				}

				dst += BUF_WIDTH;
			}

			dst = tex_volicon + VOLUME_BAR_SHADOW_X;
			for (y = 0; y < 32; y++)
			{
				for (x = 0; x < 12; x++)
				{
					if (x & 1)
						alpha = icon_bar_shadow[y][(x >> 1)] >> 4;
					else
						alpha = icon_bar_shadow[y][(x >> 1)] & 0x0f;

					dst[x] = alpha << 12;
				}

				dst += BUF_WIDTH;
			}

			dst = tex_volicon + VOLUME_DOT_X;
			for (y = 0; y < 32; y++)
			{
				for (x = 0; x < 12; x++)
				{
					if (x & 1)
						alpha = icon_dot[y][(x >> 1)] >> 4;
					else
						alpha = icon_dot[y][(x >> 1)] & 0x0f;

					dst[x] = (alpha << 12) | 0x0fff;
				}

				dst += BUF_WIDTH;
			}

			dst = tex_volicon + VOLUME_DOT_SHADOW_X;
			for (y = 0; y < 32; y++)
			{
				for (x = 0; x < 12; x++)
				{
					if (x & 1)
						alpha = icon_dot_shadow[y][(x >> 1)] >> 4;
					else
						alpha = icon_dot_shadow[y][(x >> 1)] & 0x0f;

					dst[x] = alpha << 12;
				}

				dst += BUF_WIDTH;
			}

			/* Tell driver volume icon data is ready */
			ui_draw_driver->uploadTexture(ui_draw_data, UI_TEXTURE_VOLICON,
				tex_volicon, BUF_WIDTH, 32, BUF_WIDTH, UI_PIXFMT_4444, 0);
		}
	}

	/* Build smallfont atlas */
	tex_smallfont = ui_draw_driver->getTextureBasePtr(ui_draw_data, UI_TEXTURE_SMALLFONT);

	if (tex_smallfont)
	{
		dst = tex_smallfont;
		for (code = 0; code < NUM_SMALL_FONTS; code++)
		{
			for (y = 0; y < 8; y++)
			{
				uint8_t data = font_s[(code << 3) + y];
				uint8_t mask = 0x80;

				for (x = 0; x < 8; x++)
				{
					*dst++ = (data & mask) ? color[y] : 0x8000;
					mask >>= 1;
				}
			}
		}

		/* Build box shadow tiles immediately after smallfont */
		tex_boxshadow = dst;
		for (code = 0; code < 9; code++)
		{
			for (y = 0; y < 8; y++)
			{
				for (x = 0; x < 8; x++)
				{
					if (x & 1)
						alpha = shadow[code][y][x >> 1] >> 4;
					else
						alpha = shadow[code][y][x >> 1] & 0x0f;

					alpha *= 0.8;

					*dst++ = alpha << 12;
				}
			}
		}

		/* Upload smallfont atlas (includes boxshadow data) */
		ui_draw_driver->uploadTexture(ui_draw_data, UI_TEXTURE_SMALLFONT,
			tex_smallfont, BUF_WIDTH, 16, BUF_WIDTH, UI_PIXFMT_5551, 1);

		/* Upload boxshadow */
		ui_draw_driver->uploadTexture(ui_draw_data, UI_TEXTURE_BOXSHADOW,
			tex_boxshadow, 72, 8, 72, UI_PIXFMT_4444, 1);
	}

	gauss_sum = 0;

	for (x = 0; x < 12; x++)
		gauss_sum += gauss_fact[GAUSS_WIDTH][x];
}


/******************************************************************************
	Font code lookup
******************************************************************************/

#ifdef COMMAND_LIST

/*------------------------------------------------------
	Font code lookup (command list)
------------------------------------------------------*/

static uint16_t command_font_get_code(const uint8_t *s)
{
	uint8_t c1 = s[0];
	uint8_t c2 = s[1];

	if (c1 == '_')
	{
		switch (c2)
		{
		case 'A': return 0x00 | CMD_RED;
		case 'B': return 0x01 | CMD_YELLOW;
		case 'C': return 0x02 | CMD_GREEN;
		case 'D': return 0x03 | CMD_BLUE;
		case 'P': return 0x04 | CMD_MAGENTA;
		case 'K': return 0x05 | CMD_PURPLE;
		case 'G': return 0x07 | CMD_BLUE;
		case 'H': return 0x08 | CMD_PINK;
		case 'Z': return 0x09 | CMD_PURPLE;

		case 'S': return 0x14 | CMD_RED;

		case 'a': return 0x16 | CMD_RED;
		case 'b': return 0x17 | CMD_YELLOW;
		case 'c': return 0x18 | CMD_GREEN;
		case 'd': return 0x19 | CMD_BLUE;
		case 'e': return 0x1a | CMD_MAGENTA;
		case 'f': return 0x1b | CMD_PURPLE;
		case 'g': return 0x1c | CMD_CYAN;
		case 'h': return 0x1d | CMD_PINK;
		case 'i': return 0x1e | CMD_ORANGE;
		case 'j': return 0x1f | CMD_GRAY;

		case '1': return 0x20;
		case '2': return 0x21;
		case '3': return 0x22;
		case '4': return 0x23;
		case '5': return 0x24 | CMD_RED;
		case '6': return 0x25;
		case '7': return 0x26;
		case '8': return 0x27;
		case '9': return 0x28;
		case 'N': return 0x29;
		case '+': return 0x2a;
		case '.': return 0x2b;

		case '!': return 0x35;

		case 'k': return 0x36;
		case 'l': return 0x37;
		case 'm': return 0x38;
		case 'n': return 0x39;
		case 'o': return 0x3a;
		case 'p': return 0x3b;
		case 'q': return 0x3c;
		case 'r': return 0x3d;
		case 's': return 0x3e;
		case 't': return 0x3f;
		case 'u': return 0x40;
		case 'v': return 0x41;

		case 'w': return 0x42;
		case 'x': return 0x43;
		case 'y': return 0x44;
		case 'z': return 0x45;
		case 'L': return 0x46;
		case 'M': return 0x47;
		case 'Q': return 0x48;
		case 'R': return 0x49;
		case '^': return 0x4a;
		case '?': return 0x4b;
		case 'X': return 0x4c;

		case 'I': return 0x4e;
		case 'O': return 0x4f;
		case '-': return 0x50;
		case '=': return 0x51;
		case '~': return 0x54;
		case '`': return 0x57;

		case '@': return 0x58;
		case ')': return 0x59;
		case '(': return 0x5a;
		case '*': return 0x5b;
		case '&': return 0x5c;
		case '%': return 0x5d;
		case '$': return 0x5e;
		case '#': return 0x5f;
		case ']': return 0x60;
		case '[': return 0x61;
		case '{': return 0x62;
		case '}': return 0x63;
		case '<': return 0x64;
		case '>': return 0x65;
		case '_': return CODE_UNDERBAR;
		}
	}
	else if (c1 == '^')
	{
		switch (c2)
		{
		case 's': return 0x06 | CMD_ORANGE;
		case 'E': return 0x0a | CMD_YELLOW;
		case 'F': return 0x0b | CMD_ORANGE;
		case 'G': return 0x0c | CMD_RED;
		case 'H': return 0x0d | CMD_GRAY;
		case 'I': return 0x0e | CMD_CYAN;
		case 'J': return 0x0f | CMD_BLUE;
		case 'T': return 0x10 | CMD_PURPLE;
		case 'U': return 0x11 | CMD_MAGENTA;
		case 'V': return 0x12 | CMD_PURPLE;
		case 'W': return 0x13 | CMD_MAGENTA;
		case 'S': return 0x15 | CMD_YELLOW;

		case '1': return 0x2c;
		case '2': return 0x2d;
		case '3': return 0x2e;
		case '4': return 0x2f;
		case '6': return 0x30;
		case '7': return 0x31;
		case '8': return 0x32;
		case '9': return 0x33;
		case '!': return 0x34;

		case 'M': return 0x4d;

		case '-': return 0x52;
		case '=': return 0x53;
		case '*': return 0x55;
		case '?': return 0x56;
		}
	}
	return CODE_NOTFOUND;
}


/*------------------------------------------------------
	Font code lookup (Latin-1 decode)
------------------------------------------------------*/

static uint16_t latin1_get_code(const uint8_t *s, int *type)
{
	uint16_t code;

	if ((code = command_font_get_code(s)) != CODE_NOTFOUND)
	{
		*type = FONT_TYPE_COMMAND;
		return code;
	}
	else if (isascii(*s))
	{
		*type = FONT_TYPE_ASCII;
		return *s - 0x20;
	}
	else if (islatin1(*s))
	{
		*type = FONT_TYPE_LATIN1;
		return *s - 0x80;
	}
	*type = FONT_TYPE_CONTROL;
	return *s;
}

/*------------------------------------------------------
	Font code lookup (GBK decode)
------------------------------------------------------*/

static uint16_t gbk_get_code(const uint8_t *s, int *type)
{
	uint8_t c1 = s[0];
	uint8_t c2 = s[1];
	uint16_t code;

	if ((code = command_font_get_code(s)) != CODE_NOTFOUND)
	{
		*type = FONT_TYPE_COMMAND;
		return code;
	}
	else if (isgbk1(c1) && isgbk2(c2))
	{
		*type = FONT_TYPE_GBKSIMHEI;
		return gbk_table[(c2 | (c1 << 8)) - 0x8140];
	}
	else if (isascii(c1))
	{
		if (c1 != '\\')
		{
			*type = FONT_TYPE_ASCII;
			return c1 - 0x20;
		}
	}
	*type = FONT_TYPE_CONTROL;
	return c1;
}

#endif /* COMMAND_LIST */


/*------------------------------------------------------
	Font code lookup (user interface)
------------------------------------------------------*/

static inline uint16_t uifont_get_code(const uint8_t *s, int *type)
{
	uint8_t c1 = s[0];
	uint8_t c2 = s[1];

	if (isgbk1(c1) && isgbk2(c2))
	{
		*type = FONT_TYPE_GBKSIMHEI;
		return gbk_table[(c2 | (c1 << 8)) - 0x8140];
	}
	else if (isascii(c1))
	{
		*type = FONT_TYPE_ASCII;
		return c1 - 0x20;
	}
	else if ((c1 >= 0x10 && c1 <= 0x1e) && c1 != 0x1a)
	{
		*type = FONT_TYPE_GRAPHIC;
		if (c1 < 0x1a)
			return c1 - 0x10;
		else
			return c1 - 0x11;
	}
	*type = FONT_TYPE_CONTROL;
	return c1;
}


/******************************************************************************
	Font string width
******************************************************************************/

int uifont_get_string_width(const char *s)
{
	int width, type;
	uint16_t code;
	const uint8_t *p = (const uint8_t *)s;

	width = 0;

	while (*p)
	{
		if ((code = uifont_get_code(p, &type)) != CODE_NOTFOUND)
		{
			switch (type)
			{
			case FONT_TYPE_ASCII:
				width += ascii_14p_get_pitch(code);
				p++;
				break;

			case FONT_TYPE_GRAPHIC:
				width += graphic_font_get_pitch(code);
				p++;
				break;

			case FONT_TYPE_GBKSIMHEI:
				width += gbk_s14p_get_pitch(code);
				p += 2;
				break;

			case FONT_TYPE_CONTROL:
				width += ascii_14p_get_pitch(0);
				p++;
				break;
			}
		}
		else break;
	}

	return width;
}


/******************************************************************************
	Internal font texture generation
******************************************************************************/

/*------------------------------------------------------
	Generate font glyph texture (color)
------------------------------------------------------*/

static void make_font_texture(struct font_t *font, int r, int g, int b)
{
	int x, y, p;
	uint16_t *dst, color, alpha;
	uint8_t data;

	color = (b << 8) | (g << 4) | r;

	dst = tex_font;
	p = 0;

	for (y = 0; y < font->height; y++)
	{
		for (x = 0; x < font->width;)
		{
			data = font->data[p++];

			alpha  = data & 0x0f;
			dst[x] = (alpha << 12) | color;
			x++;

			alpha = data >> 4;
			dst[x] = (alpha << 12) | color;
			x++;
		}
		dst += BUF_WIDTH;
	}
}


/*------------------------------------------------------
	Generate font shadow texture (Gaussian blur)
------------------------------------------------------*/

static void make_shadow_texture(struct font_t *font)
{
	int x, y, i, sum, alpha;
	uint16_t *dst = tex_font;
	uint8_t data;
	uint8_t temp1[32][40], temp2[32][40];

	memset(temp1, 0, sizeof(temp1));
	memset(temp2, 0, sizeof(temp2));

	i = 0;
	for (y = 0; y < font->height; y++)
	{
		for (x = 0; x < font->width; x += 2)
		{
			data = font->data[i++];

			temp1[y + 4][x + 4] = (data & 0x0f) ? 0xff : 0x00;
			temp1[y + 4][x + 5] = (data >> 4) ? 0xff : 0x00;
		}
	}

	for (x = 1; x < (font->width + 4) - 1; x++)
	{
		for (y = 1; y < (font->height + 4) - 1; y++)
		{
			sum = 0;

			for (i = 0; i < GAUSS_WIDTH; i++)
			{
				alpha = temp1[2 + y][2 + x - ((GAUSS_WIDTH - 1) >> 1) + i];
				sum += alpha * gauss_fact[GAUSS_WIDTH][i];
			}

			alpha = sum / gauss_sum;

			temp2[2 + y][2 + x] = alpha;
		}
	}

	for (x = 1; x < (font->width + 4) - 1; x++)
	{
		for (y = 1; y < (font->height + 4) - 1; y++)
		{
			sum = 0;

			for (i = 0; i < GAUSS_WIDTH; i++)
			{
				alpha = temp2[2 + y - ((GAUSS_WIDTH - 1) >> 1) + i][2 + x];
				sum += alpha * gauss_fact[GAUSS_WIDTH][i];
			}

			sum /= gauss_sum;
			if (sum > 255) sum = 255;

			temp1[2 + y][2 + x] = sum;
		}
	}

	for (y = 0; y < font->height + 4; y++)
	{
		for (x = 0; x < font->width + 4; x++)
		{
			alpha = temp1[2 + y][2 + x] >> 5;
			dst[x] = (alpha & 0x0f) << 12;
		}

		dst += BUF_WIDTH;
	}
}


/*------------------------------------------------------
	Generate font light/glow texture
------------------------------------------------------*/

static void make_light_texture(struct font_t *font)
{
	int x, y, p, alpha, level;
	uint16_t *dst;
	uint8_t data;

	dst = tex_font;
	level = light_level >> 1;
	p = 0;

	for (y = 0; y < font->height; y++)
	{
		for (x = 0; x < font->width;)
		{
			data = font->data[p++];

			alpha  = (data & 0x0f) - level;
			if (alpha < 0) alpha = 0;
			dst[x] = (alpha << 12) | 0x0fff;
			x++;

			alpha = (data >> 4) - level;
			if (alpha < 0) alpha = 0;
			dst[x] = (alpha << 12) | 0x0fff;
			x++;
		}
		dst += BUF_WIDTH;
	}
}


/******************************************************************************
	Internal font drawing — uses ui_draw_driver
******************************************************************************/

/*------------------------------------------------------
	Draw one glyph (colored)
------------------------------------------------------*/

static int internal_font_putc(struct font_t *font, int sx, int sy, int r, int g, int b)
{
	if (sx + font->pitch < 0 || sx >= SCR_WIDTH)
		return 0;

	make_font_texture(font, r, g, b);

	sx += font->skipx;
	sy += font->skipy;

	ui_draw_driver->drawSprite(ui_draw_data, UI_TEXTURE_FONT,
		0, 0, font->width, font->height,
		sx, sy, font->width, font->height,
		0xFFFFFFFF, 1);

	return 1;
}


/*------------------------------------------------------
	Draw one glyph shadow
------------------------------------------------------*/

static int internal_shadow_putc(struct font_t *font, int sx, int sy)
{
	if (sx + font->pitch < 0 || sx >= SCR_WIDTH)
		return 0;

	make_shadow_texture(font);

	sx += font->skipx;
	sy += font->skipy;

	ui_draw_driver->drawSprite(ui_draw_data, UI_TEXTURE_FONT,
		0, 0, font->width + 4, font->height + 4,
		sx, sy, font->width + 4, font->height + 4,
		0xFFFFFFFF, 1);

	return 1;
}


/*------------------------------------------------------
	Draw one glyph light/glow
------------------------------------------------------*/

static int internal_light_putc(struct font_t *font, int sx, int sy)
{
	if (sx + font->pitch < 0 || sx >= SCR_WIDTH)
		return 0;

	make_light_texture(font);

	sx += font->skipx;
	sy += font->skipy;

	ui_draw_driver->drawSprite(ui_draw_data, UI_TEXTURE_FONT,
		0, 0, font->width, font->height,
		sx, sy, font->width, font->height,
		0xFFFFFFFF, 1);

	return 1;
}


/******************************************************************************
	UI font string drawing
******************************************************************************/

/*------------------------------------------------------
	Draw UI string
------------------------------------------------------*/

static inline void uifont_draw(int sx, int sy, int r, int g, int b, const char *s)
{
	int type, res = 1;
	uint16_t code;
	const uint8_t *p = (const uint8_t *)s;
	struct font_t font;

	r >>= 4;
	g >>= 4;
	b >>= 4;

	while (*p && res)
	{
		code = uifont_get_code(p, &type);

		switch (type)
		{
		case FONT_TYPE_ASCII:
			if (ascii_14p_get_gryph(&font, code))
			{
				res = internal_font_putc(&font, sx, sy, r, g, b);
				sx += font.pitch;
			}
			p++;
			break;

		case FONT_TYPE_GRAPHIC:
			if (graphic_font_get_gryph(&font, code))
			{
				res = internal_font_putc(&font, sx, sy, r, g, b);
				sx += font.pitch;
			}
			p++;
			break;

		case FONT_TYPE_GBKSIMHEI:
			if (gbk_s14p_get_gryph(&font, code))
			{
				res = internal_font_putc(&font, sx, sy, r, g, b);
				sx += font.pitch;
			}
			p += 2;
			break;

		default:
			p++;
			break;
		}
	}
}


/*------------------------------------------------------
	Draw shadow for UI string
------------------------------------------------------*/

static inline void uifont_draw_shadow(int sx, int sy, const char *s)
{
	int type, res = 1;
	uint16_t code;
	const uint8_t *p = (const uint8_t *)s;
	struct font_t font;

	while (*p && res)
	{
		code = uifont_get_code(p, &type);

		switch (type)
		{
		case FONT_TYPE_ASCII:
			if ((res = ascii_14p_get_gryph(&font, code)) != 0)
			{
				res = internal_shadow_putc(&font, sx, sy);
				sx += font.pitch;
			}
			p++;
			break;

		case FONT_TYPE_GRAPHIC:
			if ((res = graphic_font_get_gryph(&font, code)) != 0)
			{
				res = internal_shadow_putc(&font, sx, sy);
				sx += font.pitch;
			}
			p++;
			break;

		case FONT_TYPE_GBKSIMHEI:
			if ((res = gbk_s14p_get_gryph(&font, code)) != 0)
			{
				res = internal_shadow_putc(&font, sx, sy);
				sx += font.pitch;
			}
			p += 2;
			break;

		default:
			res = 0;
			break;
		}
	}
}


/*------------------------------------------------------
	Print string
------------------------------------------------------*/

void uifont_print(int sx, int sy, int r, int g, int b, const char *s)
{
	uifont_draw(sx, sy, r, g, b, s);
}


/*------------------------------------------------------
	Print string (centered)
------------------------------------------------------*/

void uifont_print_center(int sy, int r, int g, int b, const char *s)
{
	int width = uifont_get_string_width(s);
	int sx = (SCR_WIDTH - width) / 2;

	uifont_print(sx, sy, r, g, b, s);
}


/*------------------------------------------------------
	Print string with shadow
------------------------------------------------------*/

void uifont_print_shadow(int sx, int sy, int r, int g, int b, const char *s)
{
	uifont_draw_shadow(sx, sy, s);
	uifont_print(sx, sy, r, g, b, s);
}


/*------------------------------------------------------
	Print string with shadow (centered)
------------------------------------------------------*/

void uifont_print_shadow_center(int sy, int r, int g, int b, const char *s)
{
	int width = uifont_get_string_width(s);
	int sx = (SCR_WIDTH - width) / 2;

	uifont_print_shadow(sx, sy, r, g, b, s);
}


/******************************************************************************
	Fixed-pitch font drawing (command list)
******************************************************************************/

#ifdef COMMAND_LIST

/*------------------------------------------------------
	Draw Latin-1 string
------------------------------------------------------*/

static inline void latin1_draw(int sx, int sy, int r, int g, int b, const char *s)
{
	int type, res = 1;
	uint16_t code;
	const uint8_t *p = (const uint8_t *)s;
	struct font_t font;

	r >>= 4;
	g >>= 4;
	b >>= 4;

	while (*p && res)
	{
		code = latin1_get_code(p, &type);

		switch (type)
		{
		case FONT_TYPE_ASCII:
			if ((res = ascii_14_get_gryph(&font, code)) != 0)
			{
				res = internal_font_putc(&font, sx, sy, r, g, b);
			}
			sx += FONTSIZE / 2;
			p++;
			break;

		case FONT_TYPE_LATIN1:
			if ((res = latin1_14_get_gryph(&font, code)) != 0)
			{
				res = internal_font_putc(&font, sx, sy, r, g, b);
			}
			sx += FONTSIZE / 2;
			p++;
			break;

		case FONT_TYPE_COMMAND:
			if (code == CODE_UNDERBAR)
			{
				code = *p - 0x20;
				if ((res = ascii_14_get_gryph(&font, code)) != 0)
				{
					res = internal_font_putc(&font, sx, sy, r, g, b);
				}
				sx += FONTSIZE/2;
			}
			else
			{
				int r2, g2, b2;

				if (CODE_HASCOLOR(code))
				{
					uint32_t color = command_font_color[code >> 8];

					r2 = GETR16(color);
					g2 = GETG16(color);
					b2 = GETB16(color);
					code &= 0xff;
				}
				else
				{
					r2 = r;
					g2 = g;
					b2 = b;
				}

				if ((res = command_font_get_gryph(&font, code)) != 0)
				{
					res = internal_font_putc(&font, sx, sy, r2, g2, b2);
				}
				sx += FONTSIZE;
			}
			p += 2;
			break;

		default:
			res = 0;
			break;
		}
	}
}


/*------------------------------------------------------
	Draw GBK string
------------------------------------------------------*/

static inline void gbk_draw(int sx, int sy, int r, int g, int b, const char *s)
{
	int type, res = 1;
	uint16_t code;
	const uint8_t *p = (const uint8_t *)s;
	struct font_t font;

	r >>= 4;
	g >>= 4;
	b >>= 4;

	while (*p && res)
	{
		code = gbk_get_code(p, &type);

		switch (type)
		{
		case FONT_TYPE_ASCII:
			if ((res = ascii_14_get_gryph(&font, code)) != 0)
			{
				res = internal_font_putc(&font, sx, sy, r, g, b);
			}
			sx += FONTSIZE / 2;
			p++;
			break;

		case FONT_TYPE_GBKSIMHEI:
			if ((res = gbk_s14_get_gryph(&font, code)) != 0)
			{
				res = internal_font_putc(&font, sx, sy, r, g, b);
			}
			sx += FONTSIZE;
			p += 2;
			break;

		case FONT_TYPE_COMMAND:
			if (code == CODE_UNDERBAR)
			{
				code = *p - 0x20;
				if ((res = ascii_14_get_gryph(&font, code)) != 0)
				{
					res = internal_font_putc(&font, sx, sy, r, g, b);
				}
				sx += FONTSIZE/2;
			}
			else
			{
				int r2, g2, b2;

				if (CODE_HASCOLOR(code))
				{
					uint32_t color = command_font_color[code >> 8];

					r2 = GETR16(color);
					g2 = GETG16(color);
					b2 = GETB16(color);
					code &= 0xff;
				}
				else
				{
					r2 = r;
					g2 = g;
					b2 = b;
				}

				if ((res = command_font_get_gryph(&font, code)) != 0)
				{
					res = internal_font_putc(&font, sx, sy, r2, g2, b2);
				}
				sx += FONTSIZE;
			}
			p += 2;
			break;

		default:
			res = 0;
			break;
		}
	}
}


/*------------------------------------------------------
	Print text font
------------------------------------------------------*/

void textfont_print(int sx, int sy, int r, int g, int b, const char *s, int flag)
{
	if (flag & CHARSET_GBK)
		gbk_draw(sx, sy, r, g, b, s);
	else
		latin1_draw(sx, sy, r, g, b, s);
}

#endif /* COMMAND_LIST */


/******************************************************************************
	Icon drawing
******************************************************************************/

/*------------------------------------------------------
	Draw small icon
------------------------------------------------------*/

void small_icon(int sx, int sy, int r, int g, int b, int no)
{
	struct font_t font;

	r >>= 4;
	g >>= 4;
	b >>= 4;

	if (icon_s_get_gryph(&font, no))
		internal_font_putc(&font, sx, sy, r, g, b);
}


/*------------------------------------------------------
	Draw small icon with shadow
------------------------------------------------------*/

void small_icon_shadow(int sx, int sy, int r, int g, int b, int no)
{
	struct font_t font;

	r >>= 4;
	g >>= 4;
	b >>= 4;

	if (icon_s_get_gryph(&font, no))
	{
		internal_shadow_putc(&font, sx, sy);
		internal_font_putc(&font, sx, sy, r, g, b);
	}
}


/*------------------------------------------------------
	Draw small icon with shadow and light
------------------------------------------------------*/

void small_icon_light(int sx, int sy, int r, int g, int b, int no)
{
	struct font_t font;
	struct font_t font_light;

	r >>= 4;
	g >>= 4;
	b >>= 4;

	if (icon_s_get_gryph(&font, no) && icon_s_get_light(&font_light, no))
	{
		internal_shadow_putc(&font, sx, sy);
		internal_light_putc(&font_light, sx - 4, sy - 4);
		internal_font_putc(&font, sx, sy, r, g, b);
	}
}


/*------------------------------------------------------
	Draw large icon
------------------------------------------------------*/

void large_icon(int sx, int sy, int r, int g, int b, int no)
{
	struct font_t font;

	r >>= 4;
	g >>= 4;
	b >>= 4;

	if (icon_l_get_gryph(&font, no))
		internal_font_putc(&font, sx, sy, r, g, b);
}


/*------------------------------------------------------
	Draw large icon with shadow
------------------------------------------------------*/

void large_icon_shadow(int sx, int sy, int r, int g, int b, int no)
{
	struct font_t font;

	r >>= 4;
	g >>= 4;
	b >>= 4;

	if (icon_l_get_gryph(&font, no))
	{
		internal_shadow_putc(&font, sx, sy);
		internal_font_putc(&font, sx, sy, r, g, b);
	}
}


/*------------------------------------------------------
	Draw large icon with shadow and light
------------------------------------------------------*/

void large_icon_light(int sx, int sy, int r, int g, int b, int no)
{
	struct font_t font;
	struct font_t font_light;

	r >>= 4;
	g >>= 4;
	b >>= 4;

	if (icon_l_get_gryph(&font, no) && icon_l_get_light(&font_light, no))
	{
		internal_shadow_putc(&font, sx, sy);
		internal_light_putc(&font_light, sx - 4, sy - 4);
		internal_font_putc(&font, sx, sy, r, g, b);
	}
}


/*------------------------------------------------------
	Update icon light animation
------------------------------------------------------*/

int ui_light_update(void)
{
	static int light_dir = 1;
	int prev_level;

	prev_level = light_level >> 1;

	light_level += light_dir;
	if (light_level > 31)
	{
		light_level = 31;
		light_dir = -1;
	}
	else if (light_level < 0)
	{
		light_level = 0;
		light_dir = 1;
	}

	return (prev_level != (light_level >> 1)) ? UI_PARTIAL_REFRESH : 0;
}


/******************************************************************************
	Volume drawing
******************************************************************************/

/*------------------------------------------------------
	Draw volume bar (CFW 3.52+ user mode only)
------------------------------------------------------*/

void draw_volume(int volume)
{
	int i, x;

	/* Speaker shadow */
	ui_draw_driver->drawSprite(ui_draw_data, UI_TEXTURE_VOLICON,
		SPEEKER_SHADOW_X, 0, 32, 32,
		3 + 24, 3 + 230, 32, 32,
		0xFFFFFFFF, 1);

	/* Speaker icon */
	ui_draw_driver->drawSprite(ui_draw_data, UI_TEXTURE_VOLICON,
		SPEEKER_X, 0, 32, 32,
		24, 230, 32, 32,
		0xFFFFFFFF, 1);

	x = 64;

	/* Filled bars */
	for (i = 0; i < volume; i++)
	{
		ui_draw_driver->drawSprite(ui_draw_data, UI_TEXTURE_VOLICON,
			VOLUME_BAR_SHADOW_X, 0, 12, 32,
			3 + x, 3 + 230, 12, 32,
			0xFFFFFFFF, 1);

		ui_draw_driver->drawSprite(ui_draw_data, UI_TEXTURE_VOLICON,
			VOLUME_BAR_X, 0, 12, 32,
			x, 230, 12, 32,
			0xFFFFFFFF, 1);

		x += 12;
	}

	/* Empty dots */
	for (; i < 30; i++)
	{
		ui_draw_driver->drawSprite(ui_draw_data, UI_TEXTURE_VOLICON,
			VOLUME_DOT_SHADOW_X, 0, 12, 32,
			3 + x, 3 + 230, 12, 32,
			0xFFFFFFFF, 1);

		ui_draw_driver->drawSprite(ui_draw_data, UI_TEXTURE_VOLICON,
			VOLUME_DOT_X, 0, 12, 32,
			x, 230, 12, 32,
			0xFFFFFFFF, 1);

		x += 12;
	}
}


/******************************************************************************
	Small bitmap font drawing
******************************************************************************/

/*------------------------------------------------------
	Print small font string
------------------------------------------------------*/

void small_font_print(int sx, int sy, const char *s, int bg)
{
	int i;
	int len = strlen(s);

	ui_draw_driver->setScissor(ui_draw_data, sx, sy, 8 * len, 8);

	for (i = 0; i < len; i++)
	{
		uint8_t code = isascii((uint8_t)s[i]) ? s[i] - 0x20 : 0x20;
		int u = (code & 63) << 3;
		int v = (code >> 6) << 3;

		ui_draw_driver->drawSprite(ui_draw_data, UI_TEXTURE_SMALLFONT,
			u, v, 8, 8,
			sx, sy, 8, 8,
			0xFFFFFFFF, bg ? 0 : 1);

		sx += 8;
	}

	/* Reset scissor to full screen */
	ui_draw_driver->setScissor(ui_draw_data, 0, 0, SCR_WIDTH, SCR_HEIGHT);
}


/*------------------------------------------------------
	Formatted small font print
------------------------------------------------------*/

void small_font_printf(int x, int y, const char *text, ...)
{
	char buf[256];
	va_list arg;

	va_start(arg, text);
	vsprintf(buf, text, arg);
	va_end(arg);

	small_font_print(x << 3, y << 3, buf, 1);
}


/*------------------------------------------------------
	Debug font print (to specific frame)
------------------------------------------------------*/

static void debug_font_print(void *frame, int sx, int sy, const char *s, int bg)
{
	int i;
	int len = strlen(s);
	(void)frame; /* frame target handled by driver */

	ui_draw_driver->setScissor(ui_draw_data, sx, sy, 8 * len, 8);

	for (i = 0; i < len; i++)
	{
		uint8_t code = isascii((uint8_t)s[i]) ? s[i] - 0x20 : 0x20;
		int u = (code & 63) << 3;
		int v = (code >> 6) << 3;

		ui_draw_driver->drawSprite(ui_draw_data, UI_TEXTURE_SMALLFONT,
			u, v, 8, 8,
			sx, sy, 8, 8,
			0xFFFFFFFF, bg ? 0 : 1);

		sx += 8;
	}

	ui_draw_driver->setScissor(ui_draw_data, 0, 0, SCR_WIDTH, SCR_HEIGHT);
}


/*------------------------------------------------------
	Formatted debug font print
------------------------------------------------------*/

void debug_font_printf(void *frame, int x, int y, const char *text, ...)
{
	char buf[256];
	va_list arg;

	va_start(arg, text);
	vsprintf(buf, text, arg);
	va_end(arg);

	debug_font_print(frame, x << 3, y << 3, buf, 1);
}


/******************************************************************************
	Shape drawing
******************************************************************************/

/*------------------------------------------------------
	Horizontal line
------------------------------------------------------*/

void hline(int sx, int ex, int y, int r, int g, int b)
{
	uint32_t color = MAKECOL32(r, g, b);

	ui_draw_driver->drawLine(ui_draw_data,
		sx, y, ex + 1, y, color);
}


/*------------------------------------------------------
	Horizontal line (alpha blend)
------------------------------------------------------*/

void hline_alpha(int sx, int ex, int y, int r, int g, int b, int alpha)
{
	uint32_t color = MAKECOL32A(r, g, b, ((alpha << 4) - 1));

	ui_draw_driver->drawLine(ui_draw_data,
		sx, y, ex + 1, y, color);
}


/*------------------------------------------------------
	Horizontal line (alpha blend / gradient)
------------------------------------------------------*/

void hline_gradation(int sx, int ex, int y, int r1, int g1, int b1, int r2, int g2, int b2, int alpha)
{
	uint32_t color1 = MAKECOL32A(r1, g1, b1, ((alpha << 4) - 1));
	uint32_t color2 = MAKECOL32A(r2, g2, b2, ((alpha << 4) - 1));

	ui_draw_driver->drawLineGradient(ui_draw_data,
		sx, y, ex + 1, y, color1, color2);
}


/*------------------------------------------------------
	Vertical line
------------------------------------------------------*/

void vline(int x, int sy, int ey, int r, int g, int b)
{
	uint32_t color = MAKECOL32(r, g, b);

	ui_draw_driver->drawLine(ui_draw_data,
		x, sy, x, ey + 1, color);
}


/*------------------------------------------------------
	Vertical line (alpha blend)
------------------------------------------------------*/

void vline_alpha(int x, int sy, int ey, int r, int g, int b, int alpha)
{
	uint32_t color = MAKECOL32A(r, g, b, ((alpha << 4) - 1));

	ui_draw_driver->drawLine(ui_draw_data,
		x, sy, x, ey + 1, color);
}


/*------------------------------------------------------
	Vertical line (alpha blend / gradient)
------------------------------------------------------*/

void vline_gradation(int x, int sy, int ey, int r1, int g1, int b1, int r2, int g2, int b2, int alpha)
{
	uint32_t color1 = MAKECOL32A(r1, g1, b1, ((alpha << 4) - 1));
	uint32_t color2 = MAKECOL32A(r2, g2, b2, ((alpha << 4) - 1));

	ui_draw_driver->drawLineGradient(ui_draw_data,
		x, sy, x, ey + 1, color1, color2);
}


/*------------------------------------------------------
	Rectangle outline
------------------------------------------------------*/

void box(int sx, int sy, int ex, int ey, int r, int g, int b)
{
	uint32_t color = MAKECOL32(r, g, b);

	ui_draw_driver->drawRect(ui_draw_data,
		sx, sy, ex - sx, ey - sy + 1, color);
}


/*------------------------------------------------------
	Filled rectangle
------------------------------------------------------*/

void boxfill(int sx, int sy, int ex, int ey, int r, int g, int b)
{
	uint32_t color = MAKECOL32(r, g, b);

	ui_draw_driver->fillRect(ui_draw_data,
		sx, sy, ex - sx + 1, ey - sy + 1, color);
}


/*------------------------------------------------------
	Filled rectangle (alpha blend)
------------------------------------------------------*/

void boxfill_alpha(int sx, int sy, int ex, int ey, int r, int g, int b, int alpha)
{
	uint32_t color = MAKECOL32A(r, g, b, ((alpha << 4) - 1));

	ui_draw_driver->fillRect(ui_draw_data,
		sx, sy, ex - sx + 1, ey - sy + 1, color);
}


/*------------------------------------------------------
	Filled rectangle (alpha blend / gradient)
------------------------------------------------------*/

void boxfill_gradation(int sx, int sy, int ex, int ey, int r1, int g1, int b1, int r2, int g2, int b2, int alpha, int dir)
{
	uint32_t color1 = MAKECOL32A(r1, g1, b1, ((alpha << 4) - 1));
	uint32_t color2 = MAKECOL32A(r2, g2, b2, ((alpha << 4) - 1));

	ui_draw_driver->fillRectGradient(ui_draw_data,
		sx, sy, ex - sx + 1, ey - sy + 1,
		color1, color2, dir);
}


/******************************************************************************
	UI component drawing
******************************************************************************/

/*------------------------------------------------------
	Draw box shadow tile
------------------------------------------------------*/

static void draw_boxshadow(int sx, int sy, int w, int h, int code)
{
	ui_draw_driver->setScissor(ui_draw_data, sx, sy, w, h);

	ui_draw_driver->drawSprite(ui_draw_data, UI_TEXTURE_BOXSHADOW,
		code << 3, 0, 8, 8,
		sx, sy, 8, 8,
		0xFFFFFFFF, 1);

	ui_draw_driver->setScissor(ui_draw_data, 0, 0, SCR_WIDTH, SCR_HEIGHT);
}


/*------------------------------------------------------
	Draw 9-slice box shadow
------------------------------------------------------*/

void draw_box_shadow(int sx, int sy, int ex, int ey)
{
	int i, j, x, y, width, height;
	int w, h, nw, nh;

	width = (ex - sx) + 1;
	height = (ey - sy) + 1;

	width  -= 14;
	height -= 14;

	nw = width / 8;
	nh = height / 8;

	w = width % 8;
	h = height % 8;

	sx += 2;
	sy += 2;

	x = sx;
	y = sy;

	draw_boxshadow(x, y, 8, 8, 0);
	x += 8;

	for (i = 0; i < nw; i++)
	{
		draw_boxshadow(x, y, 8, 8, 1);
		x += 8;
	}

	draw_boxshadow(x, y, w, 8, 1);
	x += w;

	draw_boxshadow(x, y, 8, 8, 2);
	y += 8;

	for (j = 0; j < nh; j++)
	{
		x = sx;

		draw_boxshadow(x, y, 8, 8, 3);
		x += 8;

		for (i = 0; i < nw; i++)
		{
			draw_boxshadow(x, y, 8, 8, 4);
			x += 8;
		}

		draw_boxshadow(x, y, w, 8, 4);
		x += w;

		draw_boxshadow(x, y, 8, 8, 5);
		y += 8;
	}

	x = sx;

	draw_boxshadow(x, y, 8, h, 3);
	x += 8;

	for (i = 0; i < nw; i++)
	{
		draw_boxshadow(x, y, 8, h, 4);
		x += 8;
	}

	draw_boxshadow(x, y, w, h, 4);
	x += w;

	draw_boxshadow(x, y, 8, h, 5);
	y += h;

	x = sx;

	draw_boxshadow(x, y, 8, 8, 6);
	x += 8;

	for (i = 0; i < nw; i++)
	{
		draw_boxshadow(x, y, 8, 8, 7);
		x += 8;
	}

	draw_boxshadow(x, y, w, 8, 7);
	x += w;

	draw_boxshadow(x, y, 8, 8, 8);
}


/*------------------------------------------------------
	Draw top bar shadow
------------------------------------------------------*/

void draw_bar_shadow(void)
{
	int x;

	for (x = 0; x < SCR_WIDTH; x += 8)
	{
		draw_boxshadow(x,  0, 8, 8, 4);
		draw_boxshadow(x,  8, 8, 8, 4);
		draw_boxshadow(x, 16, 8, 4, 4);
		draw_boxshadow(x, 20, 8, 8, 7);
	}
}

/******************************************************************************
	Logo drawing
******************************************************************************/

#include "common/font/logo.c"

/*------------------------------------------------------
	Draw logo
------------------------------------------------------*/

void logo(int sx, int sy, int r, int g, int b)
{
	int x, y, alpha;
	uint16_t color, *dst;

	r >>= 4;
	g >>= 4;
	b >>= 4;

	dst = tex_font;
	color = (b << 8) | (g << 4) | r;

	for (y = 0; y < 14; y++)
	{
#if (EMU_SYSTEM == MVS)
		for (x = 0; x < 208; x++)
#else
		for (x = 0; x < 232; x++)
#endif
		{
			if (x & 1)
				alpha = logo_data[y][x >> 1] >> 4;
			else
				alpha = logo_data[y][x >> 1] & 0x0f;

			dst[x] = (alpha << 12) | color;
		}
		dst += BUF_WIDTH;
	}

#if (EMU_SYSTEM == MVS)
	ui_draw_driver->drawSprite(ui_draw_data, UI_TEXTURE_FONT,
		0, 0, 208, 14,
		sx, sy, 208, 14,
		0xFFFFFFFF, 1);
#else
	ui_draw_driver->drawSprite(ui_draw_data, UI_TEXTURE_FONT,
		0, 0, 232, 14,
		sx, sy, 232, 14,
		0xFFFFFFFF, 1);
#endif
}
