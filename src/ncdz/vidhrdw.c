/******************************************************************************

	vidhrdw.c

	NEOGEO CD/CDZ Video Emulation

	Neo Geo Video System Reference:
	https://wiki.neogeodev.org/index.php?title=Sprites
	https://wiki.neogeodev.org/index.php?title=Fix_layer
	https://wiki.neogeodev.org/index.php?title=VRAM

	The Neo Geo CD uses the same video hardware as the MVS/AES:
	- 381 sprites max per frame, 96 per scanline
	- Sprites are 16 pixels wide, up to 512 pixels tall (32 tiles)
	- Sprites can only SHRINK (not zoom/scale up)
	- Fix layer: 40x32 tiles of 8x8 pixels, uses first 16 palettes only
	- 2 palette banks with 256 palettes each (16 colors per palette)

	CD-specific differences:
	- Graphics loaded from CD to RAM (not ROM)
	- Supports 32,768 sprite tiles (15-bit addressing vs 20-bit on cart)
	- 4,096 fix tiles (12-bit addressing)

******************************************************************************/

#include "ncdz.h"
#include "common/memory_sizes.h"
#include <unistd.h>


/******************************************************************************
	Global Variables
******************************************************************************/

uint16_t ALIGN16_DATA neogeo_videoram[NEOGEO_VRAM_SIZE / 2];
uint16_t videoram_read_buffer;
uint16_t videoram_offset;
uint16_t videoram_modulo;

uint16_t ALIGN16_DATA palettes[2][NEOGEO_PALETTE_SIZE / 2];
uint32_t palette_bank;

uint16_t *video_palette;
uint16_t ALIGN16_DATA video_palettebank[2][NEOGEO_PALETTE_SIZE / 2];
uint16_t ALIGN16_DATA video_clut16[NEOGEO_CLUT_SIZE];

uint8_t fix_pen_usage[0x20000 / 32];
uint8_t spr_pen_usage[0x400000 / 128];

int video_enable;
int spr_disable;
int fix_disable;


/******************************************************************************
	Local Variables
******************************************************************************/

static int next_update_first_line;

/*
 * Sprite Control Block pointers (reference: VRAM layout in memory_sizes.h)
 * SCB2: Shrink coefficients - lower byte = vertical ($FF=full), upper nibble = horizontal ($F=full)
 * SCB3: Y position (bits 7-15) and sprite height/sticky bit (bits 0-6)
 * SCB4: X position (bits 7-15)
 */
static uint16_t *sprite_zoom_control = &neogeo_videoram[NEOGEO_VRAM_SCB2];
static uint16_t *sprite_y_control = &neogeo_videoram[NEOGEO_VRAM_SCB3];
static uint16_t *sprite_x_control = &neogeo_videoram[NEOGEO_VRAM_SCB4];

static const uint8_t *skip_fullmode0;
static const uint8_t *skip_fullmode1;
static const uint8_t *tile_fullmode0;
static const uint8_t *tile_fullmode1;


/******************************************************************************
	Prototypes
******************************************************************************/

static void patch_vram_rbff2(void);
static void patch_vram_adkworld(void);
static void patch_vram_crsword2(void);


/******************************************************************************
	Local Functions
******************************************************************************/

/*------------------------------------------------------
	Fix Layer Drawing

	The fix layer is a non-scrollable 40x32 tile layer (8x8 pixels each)
	that overlays all sprites. It's typically used for score, timer, HUD.
	Fix tiles can only use the first 16 palettes.
	VRAM layout: $7000-$74FF, each word = palette (4 bits) + tile number (12 bits)
------------------------------------------------------*/

static void draw_fix(void)
{
	uint16_t x, y, code, attr;

	for (x = 8/8; x < 312/8; x++)
	{
		uint16_t *vram = &neogeo_videoram[NEOGEO_VRAM_FIX + 2 + (x << 5)];

		for (y = 16/8; y < 240/8; y++)
		{
			code = *vram++;
			attr = code >> 12;
			code &= 0x0fff;

			if (fix_pen_usage[code])
				blit_draw_fix((x << 3) + 16, y << 3, code, attr);
		}
	}

	blit_finish_fix();
}


/*------------------------------------------------------
	Sprite Drawing

	Neo Geo sprites are 16 pixels wide and can be up to 512 pixels tall.
	They consist of vertical strips of 16x16 tiles that can be chained
	horizontally via the "sticky bit" (bit 6 of SCB3).

	Sprite Shrinking (NOT zooming - sprites can only get smaller):
	- Horizontal: 4-bit value ($F=full 16px, $0=1px) in SCB2 upper nibble
	- Vertical: 8-bit value ($FF=full, $00=smallest) in SCB2 lower byte

	The hardware uses pixel-skipping for shrinking (no interpolation).
	Each shrink level has a specific pattern of which pixels to show.

	NCDZ uses 15-bit tile addressing (code & 0x7fff) vs MVS's 20-bit.
------------------------------------------------------*/

typedef struct
{
	int16_t  x;
	uint16_t zoom_x;		/* Horizontal shrink: 1-16 pixels */
	uint16_t *base;			/* Pointer to sprite's SCB1 data */
} SPRITE_LIST;

static SPRITE_LIST sprite_list[MAX_SPRITES_PER_LINE];

/*
 * Sprite rendering
 */
static void draw_sprites(uint32_t start, uint32_t end, int min_y, int max_y)
{
	int y = 0;
	int x = 0;
	int rows = 0;
	int zoom_y = 0;
	int zoom_x = 0;

	uint16_t sprite_number = start;
	uint16_t num_sprites = 0;
	uint16_t fullmode = 0;
	uint16_t attr;
	uint32_t code;

	const uint8_t *skip = NULL;
	const uint8_t *tile = NULL;

	do
	{
		for (;sprite_number < end; sprite_number++)
		{
			uint16_t y_control = sprite_y_control[sprite_number];
			uint16_t zoom_control = sprite_zoom_control[sprite_number];

			if (y_control & 0x40)
			{
				if (rows == 0) continue;

				x += zoom_x + 1;
			}
			else
			{
				if (num_sprites != 0) break;

				if ((rows = y_control & 0x3f) == 0) continue;

				y = 0x200 - (y_control >> 7);
				x = (sprite_x_control[sprite_number] >> 7) + 16;
				zoom_y = (zoom_control & 0xff) << 6;

				if (rows > 0x20)
				{
					rows = 0x200;
					fullmode = 1;
					skip = &skip_fullmode1[zoom_y];
					tile = &tile_fullmode1[zoom_y];
				}
				else
				{
					rows <<= 4;
					fullmode = 0;
					skip = &skip_fullmode0[zoom_y];
					tile = &tile_fullmode0[zoom_y];
				}
			}

			x &= 0x1ff;

			zoom_x = (zoom_control >> 8) & 0x0f;

			if ((x + zoom_x >= 24) && (x < 336))
			{
				sprite_list[num_sprites].x = x;
				sprite_list[num_sprites].zoom_x = zoom_x + 1;
				sprite_list[num_sprites].base = &neogeo_videoram[sprite_number << 6];
				num_sprites++;
			}
		}

		if (num_sprites)
		{
			uint16_t sprite_line = 0;
			uint16_t invert = 0;
			uint16_t sy, yskip;

			while (sprite_line < rows)
			{
				sy = (y + sprite_line) & 0x1ff;
				yskip = *skip;

				if (fullmode)
				{
					if (yskip == 0)
					{
						skip = &skip_fullmode1[zoom_y];
						tile = &tile_fullmode1[zoom_y];
						yskip = *skip;
					}
				}
				else if (yskip > 0x10)
				{
					yskip = skip_fullmode0[zoom_y + 0x3f];

					if (invert)
						sy = (sy + (*skip - yskip)) & 0x1ff;
					else
						invert = 1;
				}

				if (sy + yskip > min_y && sy <= max_y)
				{
					SPRITE_LIST *sprite = sprite_list;

					for (x = 0; x < num_sprites; x++)
					{
						attr = sprite->base[*tile + 1];
						code = sprite->base[*tile + 0];

						if (!auto_animation_disabled)
						{
							if (attr & 0x0008)
								code = (code & ~0x07) | (auto_animation_counter & 0x07);
							else if (attr & 0x0004)
								code = (code & ~0x03) | (auto_animation_counter & 0x03);
						}

						code &= 0x7fff;

						if (spr_pen_usage[code])
							blit_draw_spr(sprite->x, sy, sprite->zoom_x, yskip, code, attr);

						sprite++;
					}
				}

				sprite_line += *skip;
				skip++;
				tile++;
			}

			num_sprites = 0;
			rows = 0;
		}
	} while (sprite_number < end);

	blit_finish_spr();
}


static inline int sprite_on_scanline(int scanline, int y, int rows)
{
	/* check if the current scanline falls inside this sprite,
       two possible scenerios, wrap around or not */
	int max_y = (y + rows - 1) & 0x1ff;

	return (((max_y >= y) &&  (scanline >= y) && (scanline <= max_y)) ||
			((max_y <  y) && ((scanline >= y) || (scanline <= max_y))));
}


/*------------------------------------------------------
	SPR Sprite Drawing (priority order / for ssrpg)
------------------------------------------------------*/

static void draw_spr_prio(int min_y, int max_y)
{
	int start = 0, end = MAX_SPRITES_PER_SCREEN;

	if ((sprite_y_control[2] & 0x40) == 0 && (sprite_y_control[3] & 0x40) != 0)
	{
		start = 3;

		while ((sprite_y_control[start] & 0x40) != 0)
		    start++;

		if (start == 3) start = 0;
	}

    do
	{
		draw_sprites(start, end, min_y, max_y);

		end = start;
		start = 0;
	} while (end != 0);
}


/******************************************************************************
	NEOGEO CDZ Video Drawing Processing
******************************************************************************/

/*------------------------------------------------------
	Video Emulation Initialization
------------------------------------------------------*/

void neogeo_video_init(void)
{
	int i, r, g, b;

	for (r = 0; r < 32; r++)
	{
		for (g = 0; g < 32; g++)
		{
			for (b = 0; b < 32; b++)
			{
				int r1 = (r << 3) | (r >> 2);
				int g1 = (g << 3) | (g >> 2);
				int b1 = (b << 3) | (b >> 2);

				uint16_t color = ((r & 1) << 14) | ((r & 0x1e) << 7)
							 | ((g & 1) << 13) | ((g & 0x1e) << 3)
							 | ((b & 1) << 12) | ((b & 0x1e) >> 1);

				video_clut16[color] = MAKECOL15(r1, g1, b1);
			}
		}
	}

	skip_fullmode0 = &memory_region_user2[0x100*0x40*0];
	tile_fullmode0 = &memory_region_user2[0x100*0x40*1];
	skip_fullmode1 = &memory_region_user2[0x100*0x40*2];
	tile_fullmode1 = &memory_region_user2[0x100*0x40*3];

	memset(neogeo_videoram, 0, sizeof(neogeo_videoram));
	memset(palettes, 0, sizeof(palettes));
	memset(video_palettebank, 0, sizeof(video_palettebank));

	for (i = 0; i < 0x1000; i += 16)
	{
		video_palettebank[0][i] = 0x8000;
		video_palettebank[1][i] = 0x8000;
	}

	neogeo_video_reset();
}


/*------------------------------------------------------
	Video Emulation Shutdown
------------------------------------------------------*/

void neogeo_video_exit(void)
{
}


/*------------------------------------------------------
	Video Emulation Reset
------------------------------------------------------*/

void neogeo_video_reset(void)
{
	video_palette = video_palettebank[0];
	palette_bank = 0;
	videoram_read_buffer = 0;
	videoram_modulo = 0;
	videoram_offset = 0;

	next_update_first_line = FIRST_VISIBLE_LINE;

	video_enable = 1;
	spr_disable = 0;
	fix_disable = 0;

	blit_reset();
}


/******************************************************************************
	Screen Update Processing
******************************************************************************/

/*------------------------------------------------------
	Screen Update
------------------------------------------------------*/

void neogeo_screenrefresh(void)
{
	if (video_enable)
	{
		if (!spr_disable)
		{
			neogeo_partial_screenrefresh(LAST_VISIBLE_LINE);
		}
		else
		{
			blit_start(FIRST_VISIBLE_LINE, LAST_VISIBLE_LINE);
		}

		if (!fix_disable)
		{
			draw_fix();
		}

		blit_finish();
	}
	else
	{
		video_driver->clearFrame(video_data, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER);
	}

	next_update_first_line = FIRST_VISIBLE_LINE;
}


/*------------------------------------------------------
	Partial Screen Update
------------------------------------------------------*/

void neogeo_partial_screenrefresh(int current_line)
{
	if (current_line >= FIRST_VISIBLE_LINE)
	{
		if (video_enable)
		{
			if (!spr_disable)
			{
				if (current_line >= next_update_first_line)
				{
					if (current_line > LAST_VISIBLE_LINE)
						current_line = LAST_VISIBLE_LINE;

					blit_start(next_update_first_line, current_line);

					if (NGH_NUMBER(NGH_ssrpg))
					{
						draw_spr_prio(next_update_first_line, current_line);
					}
					else
					{
						switch (neogeo_ngh)
						{
						case NGH_crsword2: patch_vram_crsword2(); break;
						case NGH_adkworld: patch_vram_adkworld(); break;
						case NGH_rbff2: patch_vram_rbff2(); break;
						}

						draw_sprites(0, MAX_SPRITES_PER_SCREEN, next_update_first_line, current_line);
					}

					next_update_first_line = current_line + 1;
				}
			}
		}
	}
}


/*------------------------------------------------------
	Screen Update (CD-ROM Loading)
------------------------------------------------------*/

int neogeo_loading_screenrefresh(int flag, int draw)
{
	static uint64_t prev;
	static int limit;

	if (flag)
	{
		prev = ticker_driver->currentUs(ticker_data) - CLOCKS_PER_SEC / FPS;
		limit = neogeo_cdspeed_limit;
	}

	if (limit)
	{
		uint64_t target = prev + CLOCKS_PER_SEC / FPS;
		uint64_t curr = ticker_driver->currentUs(ticker_data);

		usleep(target - curr);
		curr = ticker_driver->currentUs(ticker_data);

		prev = curr;
	}

	pad_update();

	if (limit && pad_pressed(PLATFORM_PAD_L))
	{
		limit = 0;
		ui_popup(TEXT(CDROM_SPEED_LIMIT_OFF));
	}
	else if (!limit && pad_pressed(PLATFORM_PAD_R))
	{
		limit = 1;
		ui_popup(TEXT(CDROM_SPEED_LIMIT_ON));
	}

	if (limit || draw)
	{
		blit_start(FIRST_VISIBLE_LINE, LAST_VISIBLE_LINE);
		if (video_enable && !fix_disable) draw_fix();
		blit_finish();
		draw = ui_show_popup(1);
		video_driver->flipScreen(video_data, 0);
	}
	else draw = ui_show_popup(0);

	return draw;
}


/*------------------------------------------------------
	VRAM Patch (Realbout Fatal Fury 2)
------------------------------------------------------*/

static void patch_vram_rbff2(void)
{
	uint16_t offs;

	for (offs = 0; offs < ((0x300 >> 1) << 6) ; offs += 2)
	{
		uint16_t tileno  = neogeo_videoram[offs];
		uint16_t tileatr = neogeo_videoram[offs + 1];

		if (tileno == 0x7a00 && (tileatr == 0x4b00 || tileatr == 0x1400))
		{
			neogeo_videoram[offs] = 0x7ae9;
			return;
		}
	}
}


/*------------------------------------------------------
	VRAM Patch (ADK World)
------------------------------------------------------*/

static void patch_vram_adkworld(void)
{
	uint16_t offs;

	for (offs = 0; offs < ((0x300 >> 1) << 6) ; offs += 2)
	{
		uint16_t tileno  = neogeo_videoram[offs];
		uint16_t tileatr = neogeo_videoram[offs + 1];

		if ((tileno == 0x14c0 || tileno == 0x1520) && tileatr == 0x0000)
			neogeo_videoram[offs] = 0x0000;
	}
}


/*------------------------------------------------------
	VRAM Patch (Crossed Swords 2)
------------------------------------------------------*/

static void patch_vram_crsword2(void)
{
	uint16_t offs;

	for (offs = 0; offs < ((0x300 >> 1) << 6) ; offs += 2)
	{
		uint16_t tileno  = neogeo_videoram[offs];
		uint16_t tileatr = neogeo_videoram[offs + 1];

		if (tileno == 0x52a0 && tileatr == 0x0000)
			neogeo_videoram[offs] = 0x0000;
	}
}


/*------------------------------------------------------
	Save/Load State
------------------------------------------------------*/

#ifdef SAVE_STATE

STATE_SAVE( video )
{
	state_save_word(neogeo_videoram, 0x10000);
	state_save_word(palettes[0], 0x1000);
	state_save_word(palettes[1], 0x1000);
	state_save_word(&videoram_read_buffer, 1);
	state_save_word(&videoram_offset, 1);
	state_save_word(&videoram_modulo, 1);
	state_save_long(&palette_bank, 1);

	state_save_long(&video_enable, 1);
	state_save_long(&fix_disable, 1);
	state_save_long(&spr_disable, 1);
}

STATE_LOAD( video )
{
	int i;

	state_load_word(neogeo_videoram, 0x10000);
	state_load_word(palettes[0], 0x1000);
	state_load_word(palettes[1], 0x1000);
	state_load_word(&videoram_read_buffer, 1);
	state_load_word(&videoram_offset, 1);
	state_load_word(&videoram_modulo, 1);
	state_load_long(&palette_bank, 1);

	state_load_long(&video_enable, 1);
	state_load_long(&fix_disable, 1);
	state_load_long(&spr_disable, 1);

	for (i = 0; i < 0x1000; i++)
	{
		if (i & 0x0f)
		{
			video_palettebank[0][i] = video_clut16[palettes[0][i] & 0x7fff];
			video_palettebank[1][i] = video_clut16[palettes[1][i] & 0x7fff];
		}
	}

	video_palette = video_palettebank[palette_bank];
}

#endif /* SAVE_STATE */
