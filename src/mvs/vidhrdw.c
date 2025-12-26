/******************************************************************************

	vidhrdw.c

	MVS Video Emulation

	Neo Geo Video System Reference:
	https://wiki.neogeodev.org/index.php?title=Sprites
	https://wiki.neogeodev.org/index.php?title=Fix_layer
	https://wiki.neogeodev.org/index.php?title=VRAM

	Hardware Capabilities:
	- 381 sprites max per frame, 96 per scanline
	- Sprites are 16 pixels wide, up to 512 pixels tall (32 tiles)
	- Sprites can only SHRINK (not zoom/scale up)
	- Fix layer: 40x32 tiles of 8x8 pixels, uses first 16 palettes only
	- 2 palette banks with 256 palettes each (16 colors per palette)

******************************************************************************/

#include "mvs.h"
#include "common/memory_sizes.h"


/******************************************************************************
	Global Variables
******************************************************************************/

uint16_t ALIGN_DATA neogeo_videoram[NEOGEO_VRAM_SIZE / 2];
uint16_t videoram_read_buffer;
uint16_t videoram_offset;
uint16_t videoram_modulo;

uint16_t ALIGN_DATA palettes[2][NEOGEO_PALETTE_SIZE / 2];
uint8_t palette_bank;

uint16_t *video_palette;
uint16_t ALIGN16_DATA video_palettebank[PALETTE_BANKS][PALETTE_BANK_SIZE];
uint16_t ALIGN_DATA video_clut16[NEOGEO_CLUT_SIZE];

uint8_t *gfx_pen_usage[3];

uint8_t fix_bank;
uint8_t *fix_usage;
uint8_t *fix_memory;
int neogeo_fix_bank_type;

uint16_t max_sprite_number;


/******************************************************************************
	Local Variables
******************************************************************************/

static uint32_t high_tile_mask;
static uint32_t sprite_gfx_code_mask;
static int next_update_first_line;

static uint8_t *spr_pen_usage;

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

static void (*draw_fixed_layer_func[2])(void);
static void (*draw_fixed_layer)(void);


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

static void draw_fixed_layer_type0(void)
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

			if (fix_usage[code])
				blit_draw_fix((x << 3) + 16, y << 3, code, attr);
		}
	}

	blit_finish_fix();
}


static void draw_fixed_layer_type1(void)
{
	uint16_t x, y, code, attr;
	int garouoffsets[32];
	int garoubank = 0;
	int i = 0;

	/* Garou: Mark of the Wolves uses extended fix bankswitching at $7500+ */
	y = 0;
	while (y < 32)
	{
		if (neogeo_videoram[NEOGEO_VRAM_EXT + i] == 0x0200 && (neogeo_videoram[NEOGEO_VRAM_EXT + 0x80 + i] & 0xff00) == 0xff00)
		{
			garoubank = neogeo_videoram[NEOGEO_VRAM_EXT + 0x80 + i] & 3;
			garouoffsets[y++] = garoubank;
		}
		garouoffsets[y++] = garoubank;
		i += 2;
	}

	for (x = 8/8; x < 312/8; x++)
	{
		uint16_t *vram = &neogeo_videoram[NEOGEO_VRAM_FIX + 2 + (x << 5)];

		for (y = 16/8; y < 240/8; y++)
		{
			code = *vram++;
			attr = code >> 12;
			code &= 0x0fff;

			code += (garouoffsets[(y - 2) & 31] ^ 3) << 12;

			if (fix_usage[code])
				blit_draw_fix((x << 3) + 16, y << 3, code, attr);
		}
	}

	blit_finish_fix();
}


static void draw_fixed_layer_type2(void)
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

			/* KOF2000+ style fix bankswitching via extension area */
			code += (((neogeo_videoram[NEOGEO_VRAM_EXT + ((y - 1) & 31) + ((x / 6) << 5)] >> (5 - (x % 6)) * 2) & 3) ^ 3) << 12;

			if (fix_usage[code])
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
------------------------------------------------------*/

typedef struct
{
	int16_t  x;
	uint16_t zoom_x;		/* Horizontal shrink: 1-16 pixels */
	uint16_t *base;			/* Pointer to sprite's SCB1 data */
} SPRITE_LIST;

static SPRITE_LIST sprite_list[MAX_SPRITES_PER_LINE];

/*
 * Hardware-accelerated sprite rendering
 * Used for full-screen updates (>15 lines changed)
 */
static void draw_sprites_hardware(int min_y, int max_y)
{
	int y = 0;
	int x = 0;
	int rows = 0;
	int zoom_y = 0;
	int zoom_x = 0;

	uint16_t sprite_number = 0;
	uint16_t num_sprites = 0;
	uint16_t fullmode = 0;
	uint16_t attr;
	uint32_t code;

	const uint8_t *skip = NULL;
	const uint8_t *tile = NULL;

	do
	{
		for (;sprite_number < max_sprite_number; sprite_number++)
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

				if (sy + yskip > min_y && yskip <= max_y)
				{
					SPRITE_LIST *sprite = sprite_list;

					for (x = 0; x < num_sprites; x++)
					{
						attr = sprite->base[*tile + 1];
						code = sprite->base[*tile + 0] | ((attr << 12) & high_tile_mask);

						if (!auto_animation_disabled)
						{
							if (attr & 0x0008)
								code = (code & ~0x07) | (auto_animation_counter & 0x07);
							else if (attr & 0x0004)
								code = (code & ~0x03) | (auto_animation_counter & 0x03);
						}

						code &= sprite_gfx_code_mask;

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
	} while (sprite_number < max_sprite_number);

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


/*
 * Software sprite rendering (scanline-by-scanline)
 * Used for partial updates (<15 lines) and shrunk sprites
 * This path uses zoom_x_tables for pixel-skip patterns
 */
static void draw_sprites_software(int min_y, int max_y)
{
	int y = 0;
	int x = 0;
	int rows = 0;
	int zoom_y = 0;
	int zoom_x = 0;

	uint16_t sprite_number = 0;
	uint16_t num_sprites = 0;
	uint8_t fullmode = 0;
	uint16_t attr;
	uint32_t code;

	do
	{
		for (;sprite_number < max_sprite_number; sprite_number++)
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
				zoom_y = zoom_control & 0xff;

				if (rows > 0x20)
				{
					rows = 0x200;
					fullmode = 1;
				}
				else
				{
					rows <<= 4;
					fullmode = 0;
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
			uint16_t scanline;
			uint8_t *zoom_y_table = memory_region_gfx4 + (zoom_y << 8);
			uint8_t sprite_y;
			uint8_t tile;
			uint8_t sprite_y_and_tile;
			uint8_t attr_and_code_offs;

			for (scanline = min_y; scanline <= max_y; scanline++)
			{
				SPRITE_LIST *sprite = sprite_list;

				if (sprite_on_scanline(scanline, y, rows))
				{
					uint16_t sprite_line = (scanline - y) & 0x1ff;
					uint16_t zoom_line = sprite_line & 0xff;
					uint16_t invert = sprite_line & 0x100;

					if (invert)
						zoom_line ^= 0xff;

					if (fullmode)
					{
						zoom_line = zoom_line % ((zoom_y + 1) << 1);

						if (zoom_line > zoom_y)
						{
							zoom_line = ((zoom_y + 1) << 1) - 1 - zoom_line;
							invert = !invert;
						}
					}

					sprite_y_and_tile = zoom_y_table[zoom_line];
					sprite_y = sprite_y_and_tile & 0x0f;
					tile = sprite_y_and_tile >> 4;

					if (invert)
					{
						sprite_y ^= 0x0f;
						tile ^= 0x1f;
					}

					attr_and_code_offs = tile << 1;

					for (x = 0; x < num_sprites; x++)
					{
						attr = sprite->base[attr_and_code_offs + 1];
						code = sprite->base[attr_and_code_offs + 0] | ((attr << 12) & high_tile_mask);

						if (!auto_animation_disabled)
						{
							if (attr & 0x0008)
								code = (code & ~0x07) | (auto_animation_counter & 0x07);
							else if (attr & 0x0004)
								code = (code & ~0x03) | (auto_animation_counter & 0x03);
						}

						code &= sprite_gfx_code_mask;

						if (spr_pen_usage[code])
							blit_draw_spr_line(sprite->x, scanline, sprite->zoom_x, sprite_y, code, attr, spr_pen_usage[code]);

						sprite++;
					}
				}
			}

			num_sprites = 0;
			rows = 0;
		}
	} while (sprite_number < max_sprite_number);
}


/******************************************************************************
	MVS Video Drawing Processing
******************************************************************************/

/*------------------------------------------------------
	Video Emulation Initialization
------------------------------------------------------*/

void neogeo_video_init(void)
{
	int i, r, g, b;
	uint32_t no_of_tiles, bit;

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

	no_of_tiles = memory_length_gfx3 / 128;
	high_tile_mask  = (no_of_tiles > 0x10000) ? 0x10000 : 0;
	high_tile_mask |= (no_of_tiles > 0x20000) ? 0x20000 : 0;
	high_tile_mask |= (no_of_tiles > 0x40000) ? 0x40000 : 0;
	high_tile_mask |= (no_of_tiles > 0x60000) ? 0x60000 : 0;// Support extended CROM
	high_tile_mask |= (no_of_tiles > 0x80000) ? 0x80000 : 0;// Support extended CROM
	high_tile_mask |= (no_of_tiles > 0xa0000) ? 0xa0000 : 0;// Support extended CROM
	high_tile_mask |= (no_of_tiles > 0xc0000) ? 0xc0000 : 0;// Support extended CROM

	sprite_gfx_code_mask = 0xffffffff;

	for (bit = 0x80000000; bit != 0; bit >>= 1)
	{
		if ((no_of_tiles - 1) & bit)
			break;

		sprite_gfx_code_mask >>= 1;
	}

	skip_fullmode0 = &memory_region_user3[0x100*0x40*0];
	tile_fullmode0 = &memory_region_user3[0x100*0x40*1];
	skip_fullmode1 = &memory_region_user3[0x100*0x40*2];
	tile_fullmode1 = &memory_region_user3[0x100*0x40*3];

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

	if (neogeo_bios > JAPAN_AES && neogeo_bios < DEBUG_BIOS)
		max_sprite_number = 32;
	else if (neogeo_bios == ASIA_AES
	|| neogeo_bios == JAPAN_AES
	|| neogeo_bios == DEBUG_BIOS)
		max_sprite_number = MAX_SPRITES_PER_SCREEN;
	else
		max_sprite_number = 0;

	draw_fixed_layer_func[0] = draw_fixed_layer_type0;

	switch (neogeo_fix_bank_type)
	{
	case 1:  draw_fixed_layer_func[1] = draw_fixed_layer_type1; break;
	case 2:  draw_fixed_layer_func[1] = draw_fixed_layer_type2; break;
	default: draw_fixed_layer_func[1] = draw_fixed_layer_type0; break;
	}

	if (neogeo_bios == ASIA_AES
	|| neogeo_bios == JAPAN_AES
	|| neogeo_bios == DEBUG_BIOS)
		neogeo_set_fixed_layer_source(1);
	else
		neogeo_set_fixed_layer_source(0);

	spr_pen_usage = gfx_pen_usage[2];

	blit_reset();
}


/******************************************************************************
	FIX Layer Bank Processing
******************************************************************************/

/*------------------------------------------------------
	Select fix bank (0x3a000a / 9x3001a)
------------------------------------------------------*/

void neogeo_set_fixed_layer_source(uint8_t data)
{
	fix_bank   = data;
	fix_usage  = gfx_pen_usage[data];
	fix_memory = data ? memory_region_gfx2 : memory_region_gfx1;

	draw_fixed_layer = draw_fixed_layer_func[data];

	blit_set_fix_clear_flag();
}


/******************************************************************************
	Screen Update Processing
******************************************************************************/

/*------------------------------------------------------
	Screen Update
------------------------------------------------------*/

void neogeo_screenrefresh(void)
{
	if (next_update_first_line <= LAST_VISIBLE_LINE)
	{
		blit_start(next_update_first_line, LAST_VISIBLE_LINE);

		if (LAST_VISIBLE_LINE - next_update_first_line < 15)
			draw_sprites_software(next_update_first_line, LAST_VISIBLE_LINE);
		else
			draw_sprites_hardware(next_update_first_line, LAST_VISIBLE_LINE);
	}

	draw_fixed_layer();

	blit_finish();

	next_update_first_line = FIRST_VISIBLE_LINE;
}


/*------------------------------------------------------
	Partial Screen Update
------------------------------------------------------*/

void neogeo_partial_screenrefresh(int current_line)
{
	if (current_line >= FIRST_VISIBLE_LINE)
	{
		if (current_line >= next_update_first_line)
		{
			if (current_line > LAST_VISIBLE_LINE)
				current_line = LAST_VISIBLE_LINE;

			blit_start(next_update_first_line, current_line);

			if (current_line - next_update_first_line < 15)
				draw_sprites_software(next_update_first_line, current_line);
			else
				draw_sprites_hardware(next_update_first_line, current_line);
		}

		next_update_first_line = current_line + 1;
	}
}


/******************************************************************************
	Save/Load State
******************************************************************************/

#ifdef SAVE_STATE

STATE_SAVE( video )
{
	state_save_word(neogeo_videoram, 0x10000);
	state_save_word(palettes[0], 0x1000);
	state_save_word(palettes[1], 0x1000);
	state_save_word(&videoram_read_buffer, 1);
	state_save_word(&videoram_offset, 1);
	state_save_word(&videoram_modulo, 1);

	state_save_byte(&palette_bank, 1);
	state_save_byte(&fix_bank, 1);
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

	state_load_byte(&palette_bank, 1);
	state_load_byte(&fix_bank, 1);

	for (i = 0; i < 0x1000; i++)
	{
		if (i & 0x0f)
		{
			video_palettebank[0][i] = video_clut16[palettes[0][i] & 0x7fff];
			video_palettebank[1][i] = video_clut16[palettes[1][i] & 0x7fff];
		}
	}

	video_palette = video_palettebank[palette_bank];

	neogeo_set_fixed_layer_source(fix_bank);
}

#endif /* SAVE_STATE */
