/******************************************************************************

	sprite.c

	MVS Sprite Manager - PSP Platform

******************************************************************************/

#include "mvs.h"
#include "sprite_common.h"


/******************************************************************************
	Platform-specific constants/macros
******************************************************************************/

#define PSP_UNCACHE_PTR(p)			(((uint32_t)(p)) | 0x40000000)


/******************************************************************************
	Platform-specific variables
******************************************************************************/

static RECT mvs_src_clip = { 24, 16, 24 + 304, 16 + 224 };

static RECT mvs_clip[6] =
{
	{ 88, 24, 88 + 304, 24 + 224 },	// option_stretch = 0  (304x224 19:14)
	{ 80, 16, 80 + 320, 16 + 240 },	// option_stretch = 1  (320x240  4:3)
	{ 60,  1, 60 + 360,  1 + 270 },	// option_stretch = 2  (360x270  4:3)
	{ 57,  1, 57 + 360,  1 + 270 },	// option_stretch = 3  (366x270 19:14)
	{ 30,  1, 30 + 420,  1 + 270 },	// option_stretch = 4  (420x270 14:9)
	{  0,  1,  0 + 480,  1 + 270 }	    // option_stretch = 5  (480x270 16:9)
};

static struct Vertex ALIGN_DATA vertices_fix[FIX_MAX_SPRITES * 2];
static struct Vertex ALIGN_DATA vertices_spr[SPR_MAX_SPRITES * 2];
static uint16_t ALIGN_DATA spr_flags[SPR_MAX_SPRITES];

static uint8_t clut_index;


/*------------------------------------------------------------------------
	'swizzle' texture address calculation table
------------------------------------------------------------------------*/

static const int ALIGN_DATA swizzle_table_8bit[16] =
{
	   0, 16, 16, 16, 16, 16, 16, 16,
	3984, 16, 16, 16, 16, 16, 16, 16
};


/******************************************************************************
	Sprite drawing interface functions
******************************************************************************/

/*------------------------------------------------------------------------
	Clear all sprites immediately
------------------------------------------------------------------------*/

void blit_clear_all_sprite(void)
{
	blit_clear_spr_sprite();
	blit_clear_fix_sprite();
}


/*------------------------------------------------------------------------
	Set FIX sprite clear flag
------------------------------------------------------------------------*/

void blit_set_fix_clear_flag(void)
{
	clear_fix_texture = 1;
}


/*------------------------------------------------------------------------
	Set SPR sprite clear flag
------------------------------------------------------------------------*/

void blit_set_spr_clear_flag(void)
{
	clear_spr_texture = 1;
}


/*------------------------------------------------------------------------
	Sprite processing reset
------------------------------------------------------------------------*/

void blit_reset(void)
{
	int i;

	scrbitmap  = (uint16_t *)video_driver->workFrame(video_data, SCRBITMAP);
	tex_spr[0] = video_driver->workFrame(video_data, TEX_SPR0);
	tex_spr[1] = video_driver->workFrame(video_data, TEX_SPR1);
	tex_spr[2] = video_driver->workFrame(video_data, TEX_SPR2);
	tex_fix    = video_driver->workFrame(video_data, TEX_FIX);

	for (i = 0; i < FIX_TEXTURE_SIZE; i++) fix_data[i].index = i;
	for (i = 0; i < SPR_TEXTURE_SIZE; i++) spr_data[i].index = i;

	clip_min_y = FIRST_VISIBLE_LINE;
	clip_max_y = LAST_VISIBLE_LINE;

	video_driver->setClutBaseAddr(video_data, (uint16_t *)&video_palettebank);
	clut = (uint16_t *)PSP_UNCACHE_PTR(&video_palettebank[palette_bank]);
	clut_index = palette_bank;

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
		clut = (uint16_t *)PSP_UNCACHE_PTR(&video_palettebank[palette_bank]);

		fix_num = 0;
		spr_disable = 0;

		if (clear_spr_texture) blit_clear_spr_sprite();
		if (clear_fix_texture) blit_clear_fix_sprite();

		video_driver->startWorkFrame(video_data, CNVCOL15TO32(video_palette[4095]));
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

void blit_draw_fix(int x, int y, uint32_t code, uint16_t attr)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_FIX_KEY(code, attr);

	if ((idx = fix_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
		uint8_t *src, *dst, lines = 8;

		if (fix_texture_num == FIX_TEXTURE_SIZE - 1)
			fix_delete_sprite();

		idx = fix_insert_sprite(key);
		dst = SWIZZLED8_8x8(tex_fix, idx);
		src = &fix_memory[code << 5];
		col = color_table[attr];

		while (lines--)
		{
			tile = *(uint32_t *)(src + 0);
			*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;
			src += 4;
			dst += 16;
		}
	}

	vertices = &vertices_fix[fix_num];
	fix_num += 2;

	vertices[0].x = vertices[1].x = x;
	vertices[0].y = vertices[1].y = y;
	vertices[0].u = vertices[1].u = (idx & 0x003f) << 3;
	vertices[0].v = vertices[1].v = (idx & 0x0fc0) >> 3;

	vertices[1].x += 8;
	vertices[1].y += 8;
	vertices[1].u += 8;
	vertices[1].v += 8;
}


/*------------------------------------------------------------------------
	End FIX drawing
------------------------------------------------------------------------*/

void blit_finish_fix(void)
{
	struct Vertex *vertices;

	if (!fix_num) return;

	vertices = (struct Vertex *)sceGuGetMemory(fix_num * sizeof(struct Vertex));
	memcpy(vertices, vertices_fix, fix_num * sizeof(struct Vertex));
	video_driver->blitTexture(video_data, TEX_FIX, clut, clut_index, fix_num, vertices);
}


/*------------------------------------------------------------------------
	Add SPR to draw list
------------------------------------------------------------------------*/

void blit_draw_spr(int x, int y, int w, int h, uint32_t code, uint16_t attr)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key;

	if (spr_disable) return;

	key = MAKE_SPR_KEY(code, attr);

	if ((idx = spr_get_sprite(key)) < 0)
	{
		uint32_t col, tile, gfx3_offset;
		uint8_t *src, *dst, lines = 16;

		if (spr_texture_num == SPR_TEXTURE_SIZE - 1)
		{
			spr_delete_sprite();

			if (spr_texture_num == SPR_TEXTURE_SIZE - 1)
			{
				spr_disable = 1;
				return;
			}
		}

		idx = spr_insert_sprite(key);
		dst = SWIZZLED8_16x16(tex_spr[0], idx);
		gfx3_offset = read_cache ? read_cache(code << 7) : code << 7;
		src = &memory_region_gfx3[gfx3_offset];
		col = color_table[(attr >> 8) & 0x0f];

		while (lines--)
		{
			tile = *(uint32_t *)(src + 0);
			*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;
			tile = *(uint32_t *)(src + 4);
			*(uint32_t *)(dst +  8) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst + 12) = ((tile >> 4) & 0x0f0f0f0f) | col;
			src += 8;
			dst += swizzle_table_8bit[lines];
		}
	}

	vertices = &vertices_spr[spr_num];
	spr_num += 2;

	spr_flags[spr_index] = (idx >> 10) | ((attr & 0xf000) >> 4);
	spr_index++;

	vertices[0].x = vertices[1].x = x;
	vertices[0].y = vertices[1].y = y;
	vertices[0].u = vertices[1].u = (idx & 0x001f) << 4;
	vertices[0].v = vertices[1].v = (idx & 0x03e0) >> 1;

	attr ^= 0x03;
	vertices[(attr & 0x01) >> 0].u += 16;
	vertices[(attr & 0x02) >> 1].v += 16;

	vertices[1].x += w;
	vertices[1].y += h;
}


/*------------------------------------------------------------------------
	End SPR drawing
------------------------------------------------------------------------*/

void blit_finish_spr(void)
{
	int i, total_sprites = 0;
	uint16_t flags, *pflags = spr_flags;
	struct Vertex *vertices, *vertices_tmp;

	if (!spr_index) return;

	flags = *pflags;

	sceGuStart(GU_DIRECT, gulist);
	sceGuDrawBufferList(GU_PSM_5551, work_frame, BUF_WIDTH);
	sceGuScissor(24, clip_min_y, 336, clip_max_y);
	sceGuTexImage(0, 512, 512, BUF_WIDTH, tex_spr[flags & 3]);
	sceGuClutLoad(256/8, &clut[flags & 0xf00]);

	vertices_tmp = vertices = (struct Vertex *)sceGuGetMemory(spr_num * sizeof(struct Vertex));

	for (i = 0; i < spr_num; i += 2)
	{
		if (flags != *pflags)
		{
			if (total_sprites)
			{
				sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, total_sprites, NULL, vertices);
				total_sprites = 0;
				vertices = vertices_tmp;
			}

			flags = *pflags;
			sceGuTexImage(0, 512, 512, BUF_WIDTH, tex_spr[flags & 3]);
			sceGuClutLoad(256/8, &clut[flags & 0xf00]);
		}

		vertices_tmp[0] = vertices_spr[i + 0];
		vertices_tmp[1] = vertices_spr[i + 1];

		vertices_tmp += 2;
		total_sprites += 2;
		pflags++;
	}

	if (total_sprites)
		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, total_sprites, NULL, vertices);

	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);
}


/*------------------------------------------------------------------------
	Draw sprite line (software rendering)
------------------------------------------------------------------------*/

void blit_draw_spr_line(int x, int y, int zoom_x, int sprite_y, uint32_t code, uint16_t attr, uint8_t opaque)
{
	uint32_t gfx3_offset = read_cache ? read_cache(code << 7): code << 7;
	uint32_t dst = (y << 9) + x;
	uint8_t flag = (attr & 1) | (opaque & SPRITE_OPAQUE) | ((zoom_x & 0x10) >> 2);

	if (attr & 0x0002) sprite_y ^= 0x0f;
	gfx3_offset += sprite_y << 3;

	(*drawgfxline[flag])((uint32_t *)&memory_region_gfx3[gfx3_offset], &scrbitmap[dst], &video_palette[(attr >> 8) << 4], zoom_x);
}
