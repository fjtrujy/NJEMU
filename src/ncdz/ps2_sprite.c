/******************************************************************************

	sprite.c

	NEOGEO CDZ Sprite Manager - PS2 Platform

******************************************************************************/

#include "ncdz.h"
#include "sprite_common.h"

#include <gsKit.h>
#include <gsInline.h>


/******************************************************************************
	Platform-specific variables
******************************************************************************/

static RECT mvs_src_clip = { 24, 16, 24 + 304, 16 + 224 };

static RECT mvs_clip[7] =
{
	{  0,  0,  0 + 640,  0 + 448 },	// option_stretch = 0  (640 x 448)
	{ 88, 24, 88 + 304, 24 + 224 },	// option_stretch = 1  (304x224 19:14)
	{ 80, 16, 80 + 320, 16 + 240 },	// option_stretch = 2  (320x240  4:3)
	{ 60,  1, 60 + 360,  1 + 270 },	// option_stretch = 3  (360x270  4:3)
	{ 57,  1, 57 + 360,  1 + 270 },	// option_stretch = 4  (366x270 19:14)
	{ 30,  1, 30 + 420,  1 + 270 },	// option_stretch = 5  (420x270 14:9)
	{  0,  1,  0 + 480,  1 + 270 }	// option_stretch = 6  (480x270 16:9)
};

static bool tex_fix_changed;
static GSPRIMUVPOINTFLAT ALIGN16_DATA vertices_fix[FIX_MAX_SPRITES * 2];
static GSPRIMUVPOINTFLAT ALIGN16_DATA vertices_spr[SPR_MAX_SPRITES * 2];
static uint16_t ALIGN16_DATA spr_flags[SPR_MAX_SPRITES];

static GSGLOBAL *gsGlobal;
// All the textures has the same size, as this variable is used to calculate the vertexes.
// SPR0, SPR1, SPR2 and FIX are all 512x512
GSTEXTURE *atlas;

// In GSKit 0,0 is not the top left corner, it is the center of the top left pixel.
static inline gs_xyz2 vertex_to_XYZ2_pixel_perfect(float x, float y)
{
	return vertex_to_XYZ2(gsGlobal, x - 0.5, y - 0.5, 0);
}


/******************************************************************************
	Sprite drawing interface functions
******************************************************************************/

/*------------------------------------------------------------------------
	Sprite processing reset
------------------------------------------------------------------------*/

void blit_reset(void)
{
	int i;

	scrbitmap  = (uint16_t *)video_driver->workFrame(video_data);
	tex_spr[0] = video_driver->textureLayer(video_data, TEXTURE_LAYER_SPR0);
	tex_spr[1] = video_driver->textureLayer(video_data, TEXTURE_LAYER_SPR1);
	tex_spr[2] = video_driver->textureLayer(video_data, TEXTURE_LAYER_SPR2);
	tex_fix    = video_driver->textureLayer(video_data, TEXTURE_LAYER_FIX);
	tex_fix_changed = false;

	for (i = 0; i < FIX_TEXTURE_SIZE; i++) fix_data[i].index = i;
	for (i = 0; i < SPR_TEXTURE_SIZE; i++) spr_data[i].index = i;

	clip_min_y = FIRST_VISIBLE_LINE;
	clip_max_y = LAST_VISIBLE_LINE;

	clut = (uint16_t *)&video_palettebank[palette_bank];
	video_driver->uploadClut(video_data, clut, palette_bank);

	gsGlobal = video_driver->getNativeObjects(video_data, COMMON_GRAPHIC_OBJECTS_GLOBAL_CONTEXT);
	// All the textures has the same size, so we can use any of them to get the GSTEXTURE structure
	atlas = video_driver->getNativeObjects(video_data, COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER); 

	blit_clear_all_sprite();
}


/*------------------------------------------------------------------------
	Start screen update
------------------------------------------------------------------------*/

void blit_start(int start, int end)
{
	clip_min_y = start;
	clip_max_y = end + 1;

	spr_num   = 0;
	spr_index = 0;

	if (start == FIRST_VISIBLE_LINE)
	{
		clut = (uint16_t *)&video_palettebank[palette_bank];
		video_driver->uploadClut(video_data, clut, palette_bank);

		fix_num = 0;

		if (clear_spr_texture) blit_clear_spr_sprite();
		if (clear_fix_texture) blit_clear_fix_sprite();

		video_driver->startWorkFrame(video_data, CNVCOL15TO32(video_palette[4095]));
		video_driver->scissor(video_data, 24, 16, 336, 240);
	}
}


/*------------------------------------------------------------------------
	End screen update
------------------------------------------------------------------------*/

void blit_finish(void)
{
	video_driver->transferWorkFrame(video_data, &mvs_src_clip, &mvs_clip[option_stretch]);
}


/*------------------------------------------------------------------------
	Add FIX to draw list
------------------------------------------------------------------------*/

void blit_draw_fix(int x, int y, uint32_t code, uint32_t attr)
{
	int16_t idx;
	GSPRIMUVPOINTFLAT *vertices;
	uint32_t key = MAKE_FIX_KEY(code, attr);

	if ((idx = fix_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
		uint8_t *src, *dst, lines, row, column;
		uint32_t datal, datah;

		tex_fix_changed = true;

		if (fix_texture_num == FIX_TEXTURE_SIZE - 1)
			fix_delete_sprite();

		idx = fix_insert_sprite(key);
		src = &memory_region_gfx1[code << 5];
		col = color_table[attr];

		row = idx / TILE_8x8_PER_LINE;
		column = idx % TILE_8x8_PER_LINE;
		for (lines = 0; lines < 8; lines++)
		{
			dst = &tex_fix[((row * 8) + lines) * BUF_WIDTH + (column * 8)];
			tile = *(uint32_t *)(src + 0);
			datal = ((tile & 0x0000000f) >>  0) | ((tile & 0x000000f0) <<  4) | ((tile & 0x00000f00) <<  8) | ((tile & 0x0000f000) << 12) | col;
			datah = ((tile & 0x000f0000) >> 16) | ((tile & 0x00f00000) >> 12) | ((tile & 0x0f000000) >>  8) | ((tile & 0xf0000000) >>  4) | col;
			*(uint32_t *)(dst +  0) = datal;
			*(uint32_t *)(dst +  4) = datah;
			src += 4;
		}
	}

	vertices = &vertices_fix[fix_num];
	fix_num += 2;

	uint32_t x0 = x;
	uint32_t y0 = y;
	uint32_t u0 = (idx & 0x003f) << 3;
	uint32_t v0 = (idx & 0x0fc0) >> 3;
	uint32_t x1 = x + 8;
	uint32_t y1 = y + 8;
	uint32_t u1 = u0 + 8;
	uint32_t v1 = v0 + 8;


	vertices[0].xyz2 = vertex_to_XYZ2_pixel_perfect(x0, y0);
	vertices[0].uv = vertex_to_UV(atlas, u0, v0);

	vertices[1].xyz2 = vertex_to_XYZ2_pixel_perfect(x1, y1);
	vertices[1].uv = vertex_to_UV(atlas, u1, v1);
}


/*------------------------------------------------------------------------
	End FIX drawing
------------------------------------------------------------------------*/

void blit_finish_fix(void)
{
	if (!fix_num) return;
	if (tex_fix_changed) {
		video_driver->uploadMem(video_data, TEXTURE_LAYER_FIX);
		tex_fix_changed = false;
	}

	video_driver->flushCache(video_data, vertices_fix, fix_num * sizeof(GSPRIMUVPOINTFLAT));
	video_driver->blitTexture(video_data, TEXTURE_LAYER_FIX, clut, palette_bank, fix_num, vertices_fix);
}


/*------------------------------------------------------------------------
	Add SPR to draw list
------------------------------------------------------------------------*/

void blit_draw_spr(int x, int y, int w, int h, uint32_t code, uint32_t attr)
{
	int16_t idx;
	GSPRIMUVPOINTFLAT *vertices;
	uint32_t key;

	key = MAKE_SPR_KEY(code, attr);

	if ((idx = spr_get_sprite(key)) < 0)
	{
		uint32_t col, tile, offset;
		uint8_t *src, *dst, lines, row, column;

		if (spr_texture_num == SPR_TEXTURE_SIZE - 1)
			spr_delete_sprite();

		src = &memory_region_gfx2[code << 7];
		idx = spr_insert_sprite(key);
		col = color_table[(attr >> 8) & 0x0f];

		row = idx / TILE_16x16_PER_LINE;
		column = idx % TILE_16x16_PER_LINE;
		for (lines = 0; lines < 16; lines++)
		{
			offset = ((row * 16) + lines) * BUF_WIDTH + (column * 16);
			dst = &tex_spr[0][offset];
			tile = *(uint32_t *)(src + 0);
			*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;
			tile = *(uint32_t *)(src + 4);
			*(uint32_t *)(dst +  8) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst + 12) = ((tile >> 4) & 0x0f0f0f0f) | col;
			src += 8;
		}
	}

	vertices = &vertices_spr[spr_num];
	spr_num += 2;

	spr_flags[spr_index] = (idx >> 10) | ((attr & 0xf000) >> 4);
	spr_index++;

	uint32_t x0 = x;
	uint32_t y0 = y;
	uint32_t u0 = (idx & 0x001f) << 4;
	uint32_t v0 = (idx & 0x03e0) >> 1;
	uint32_t x1 = x0 + w;
	uint32_t y1 = y0 + h;
	uint32_t u1 = u0;
	uint32_t v1 = v0;

	attr ^= 0x03;
	uint32_t index_u = (attr & 0x01) >> 0;
	uint32_t index_v = (attr & 0x02) >> 1;

	u0 += index_u ? 0 : 16;
	v0 += index_v ? 0 : 16;
	u1 += index_u ? 16 : 0;
	v1 += index_v ? 16 : 0;

	vertices[0].xyz2 = vertex_to_XYZ2_pixel_perfect(x0, y0);
	vertices[0].uv = vertex_to_UV(atlas, u0, v0);

	vertices[1].xyz2 = vertex_to_XYZ2_pixel_perfect(x1, y1);
	vertices[1].uv = vertex_to_UV(atlas, u1, v1);
}


/*------------------------------------------------------------------------
	End SPR drawing
------------------------------------------------------------------------*/

void blit_finish_spr(void)
{
	int i, total_sprites = 0;
	uint16_t flags, *pflags = spr_flags;
	GSPRIMUVPOINTFLAT *vertices, *vertices_tmp;
	uint16_t *clut_tmp;
	uint8_t tex_layer_index;

	if (!spr_index) return;

	bool memUploaded[TEXTURE_LAYER_COUNT] = { 0 };
	bool clutUploaded[16] = { 0 };

	flags = *pflags;
	tex_layer_index = flags & 3;
	memUploaded[tex_layer_index] = true;
	clutUploaded[(flags & 0xf00)/256] = true;
	clut_tmp = &clut[flags & 0xf00];
	video_driver->uploadMem(video_data, tex_layer_index);

	vertices_tmp = vertices = &vertices_spr[0];
	video_driver->flushCache(video_data, vertices, spr_num * sizeof(GSPRIMUVPOINTFLAT));

	for (i = 0; i < spr_num; i += 2)
	{
		if (flags != *pflags)
		{
			if (total_sprites)
			{
				video_driver->blitTexture(video_data, tex_layer_index, clut_tmp, palette_bank, total_sprites, vertices);
				total_sprites = 0;
				vertices = vertices_tmp;
			}

			flags = *pflags;
			tex_layer_index = flags & 3;
			clut_tmp = &clut[flags & 0xf00];
			if (memUploaded[tex_layer_index] == false) {
				memUploaded[tex_layer_index] = true;
				video_driver->uploadMem(video_data, tex_layer_index);
			}
			if (clutUploaded[(flags & 0xf00)/256] == false) {
				clutUploaded[(flags & 0xf00)/256] = true;
			}
		}

		vertices_tmp += 2;
		total_sprites += 2;
		pflags++;
	}

	if (total_sprites)
		video_driver->blitTexture(video_data, tex_layer_index, clut_tmp, palette_bank, total_sprites, vertices);
}


/******************************************************************************
	SPR Software rendering
******************************************************************************/

void blit_draw_spr_line(int x, int y, int zoom_x, int sprite_y, uint32_t code, uint16_t attr, uint8_t opaque)
{
	uint32_t src = code << 7;
	uint32_t dst = (y << 9) + x;
	uint8_t flag = (attr & 1) | (opaque & SPRITE_OPAQUE) | ((zoom_x & 0x10) >> 2);

	if (attr & 0x0002) sprite_y ^= 0x0f;
	src += sprite_y << 3;

	(*drawgfxline[flag])((uint32_t *)&memory_region_gfx2[src], &scrbitmap[dst], &video_palette[(attr >> 8) << 4], zoom_x);
}
