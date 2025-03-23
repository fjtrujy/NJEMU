/******************************************************************************

	sprite.c

	NEOGEO CDZ スプライトマネージャ

******************************************************************************/

#include "ncdz.h"


/******************************************************************************
	定数/マクロ等
******************************************************************************/

#define MAKE_FIX_KEY(code, attr)	(uint32_t)(code | (((uint32_t)attr) << 28))
#define MAKE_SPR_KEY(code, attr)	(uint32_t)(code | (((uint32_t)(attr & 0x0f00)) << 20))


/******************************************************************************
	ローカル変数/構造体
******************************************************************************/

typedef struct sprite_t SPRITE;

struct sprite_t
{
	uint32_t key;
	uint32_t used;
	int32_t index;
	SPRITE *next;
};

static RECT mvs_src_clip = { 24, 16, 24 + 304, 16 + 224 };

static RECT mvs_clip[7] =
{
	{  0,  0,  0 + 640,  0 + 448 },	// option_stretch = 0  (640 x 448)
	{ 88, 24, 88 + 304, 24 + 224 },	// option_stretch = 1  (304x224 19:14)
	{ 80, 16, 80 + 320, 16 + 240 },	// option_stretch = 2  (320x240  4:3)
	{ 60,  1, 60 + 360,  1 + 270 },	// option_stretch = 3  (360x270  4:3)
	{ 57,  1, 57 + 360,  1 + 270 },	// option_stretch = 4  (366x270 19:14)
	{ 30,  1, 30 + 420,  1 + 270 },	// option_stretch = 5  (420x270 14:9)
	{  0,  1,  0 + 480,  1 + 270 }	    // option_stretch = 8  (480x270 16:9)
};

static int clip_min_y;
static int clip_max_y;
static int clear_spr_texture;
static int clear_fix_texture;

static uint16_t *scrbitmap;


/*------------------------------------------------------------------------
	FIX: サイズ/位置8pixel固定のスプライト
------------------------------------------------------------------------*/

#define FIX_TEXTURE_SIZE	((BUF_WIDTH/8)*(TEXTURE_HEIGHT/8))
#define FIX_HASH_MASK		0x1ff
#define FIX_HASH_SIZE		0x200
#define FIX_MAX_SPRITES		((320/8) * (240/8))
#define TILE_8x8_PER_LINE	(BUF_WIDTH/8)
#define TILE_16x16_PER_LINE	(BUF_WIDTH/16)

static SPRITE ALIGN_DATA *fix_head[FIX_HASH_SIZE];
static SPRITE ALIGN_DATA fix_data[FIX_TEXTURE_SIZE];
static SPRITE *fix_free_head;

static uint8_t *tex_fix;
static struct Vertex ALIGN_DATA vertices_fix[FIX_MAX_SPRITES * 2];
static uint16_t fix_num;
static uint16_t fix_texture_num;


/*------------------------------------------------------------------------
	SPR: 可変サイズのスプライト
------------------------------------------------------------------------*/

#define SPR_TEXTURE_SIZE	((BUF_WIDTH/16)*((TEXTURE_HEIGHT*3)/16))
#define SPR_HASH_MASK		0x1ff
#define SPR_HASH_SIZE		0x200
#define SPR_MAX_SPRITES		0x3000

static SPRITE ALIGN_DATA *spr_head[SPR_HASH_SIZE];
static SPRITE ALIGN_DATA spr_data[SPR_TEXTURE_SIZE];
static SPRITE *spr_free_head;

static uint8_t *tex_spr[3];
static struct Vertex ALIGN_DATA vertices_spr[SPR_MAX_SPRITES * 2];
static uint16_t ALIGN_DATA spr_flags[SPR_MAX_SPRITES];
static uint16_t spr_num;
static uint16_t spr_texture_num;
static uint16_t spr_index;


/*------------------------------------------------------------------------
	カラールックアップテーブル
------------------------------------------------------------------------*/

static uint16_t *clut;

static const uint32_t ALIGN_DATA color_table[16] =
{
	0x00000000, 0x10101010, 0x20202020, 0x30303030,
	0x40404040, 0x50505050, 0x60606060, 0x70707070,
	0x80808080, 0x90909090, 0xa0a0a0a0, 0xb0b0b0b0,
	0xc0c0c0c0, 0xd0d0d0d0, 0xe0e0e0e0, 0xf0f0f0f0
};


/******************************************************************************
	FIXスプライト管理
******************************************************************************/

/*------------------------------------------------------------------------
	FIXテクスチャからスプライト番号を取得
------------------------------------------------------------------------*/

static int fix_get_sprite(uint32_t key)
{
	SPRITE *p = fix_head[key & FIX_HASH_MASK];

	while (p)
	{
		if (p->key == key)
		{
			p->used = frames_displayed;
			return p->index;
	 	}
		p = p->next;
	}
	return -1;
}


/*------------------------------------------------------------------------
	FIXテクスチャにスプライトを登録
------------------------------------------------------------------------*/

static int fix_insert_sprite(uint32_t key)
{
	uint16_t hash = key & FIX_HASH_MASK;
	SPRITE *p = fix_head[hash];
	SPRITE *q = fix_free_head;

	if (!q) return -1;

	fix_free_head = fix_free_head->next;

	q->next = NULL;
	q->key  = key;
	q->used = frames_displayed;

	if (!p)
	{
		fix_head[hash] = q;
	}
	else
	{
		while (p->next) p = p->next;
		p->next = q;
	}

	fix_texture_num++;

	return q->index;
}


/*------------------------------------------------------------------------
	FIXテクスチャから一定時間を経過したスプライトを削除
------------------------------------------------------------------------*/

static void fix_delete_sprite(void)
{
	int i;
	SPRITE *p, *prev_p;

	for (i = 0; i < FIX_HASH_SIZE; i++)
	{
		prev_p = NULL;
		p = fix_head[i];

		while (p)
		{
			if (frames_displayed != p->used)
			{
				fix_texture_num--;

				if (!prev_p)
				{
					fix_head[i] = p->next;
					p->next = fix_free_head;
					fix_free_head = p;
					p = fix_head[i];
				}
				else
				{
					prev_p->next = p->next;
					p->next = fix_free_head;
					fix_free_head = p;
					p = prev_p->next;
				}
			}
			else
			{
				prev_p = p;
				p = p->next;
			}
		}
	}
}


/******************************************************************************
	SPRスプライト管理
******************************************************************************/

/*------------------------------------------------------------------------
	SPRテクスチャからスプライト番号を取得
------------------------------------------------------------------------*/

static int spr_get_sprite(uint32_t key)
{
	SPRITE *p = spr_head[key & SPR_HASH_MASK];

	while (p)
	{
		if (p->key == key)
		{
			p->used = frames_displayed;
			return p->index;
		}
		p = p->next;
	}
	return -1;
}


/*------------------------------------------------------------------------
	SPRテクスチャにスプライトを登録
------------------------------------------------------------------------*/

static int spr_insert_sprite(uint32_t key)
{
	uint16_t hash = key & SPR_HASH_MASK;
	SPRITE *p = spr_head[hash];
	SPRITE *q = spr_free_head;

	if (!q) return -1;

	spr_free_head = spr_free_head->next;

	q->next = NULL;
	q->key  = key;
	q->used = frames_displayed;

	if (!p)
	{
		spr_head[hash] = q;
	}
	else
	{
		while (p->next) p = p->next;
		p->next = q;
	}

	spr_texture_num++;

	return q->index;
}


/*------------------------------------------------------------------------
	SPRテクスチャから一定時間を経過したスプライトを削除
------------------------------------------------------------------------*/

static void spr_delete_sprite(void)
{
	int i;
	SPRITE *p, *prev_p;

	for (i = 0; i < SPR_HASH_SIZE; i++)
	{
		prev_p = NULL;
		p = spr_head[i];

		while (p)
		{
			if (frames_displayed != p->used)
			{
				spr_texture_num--;

				if (!prev_p)
				{
					spr_head[i] = p->next;
					p->next = spr_free_head;
					spr_free_head = p;
					p = spr_head[i];
				}
				else
				{
					prev_p->next = p->next;
					p->next = spr_free_head;
					spr_free_head = p;
					p = prev_p->next;
				}
			}
			else
			{
				prev_p = p;
				p = p->next;
			}
		}
	}
}


/******************************************************************************
	スプライト描画インタフェース関数
******************************************************************************/

/*------------------------------------------------------------------------
	FIXスプライトを即座にクリアする
------------------------------------------------------------------------*/

void blit_clear_fix_sprite(void)
{
	int i;

	for (i = 0; i < FIX_TEXTURE_SIZE - 1; i++)
		fix_data[i].next = &fix_data[i + 1];

	fix_data[i].next = NULL;
	fix_free_head = &fix_data[0];

	memset(fix_head, 0, sizeof(SPRITE *) * FIX_HASH_SIZE);

	fix_texture_num = 0;
	clear_fix_texture = 0;
}


/*------------------------------------------------------------------------
	全てのスプライトを即座にクリアする
------------------------------------------------------------------------*/

void blit_clear_spr_sprite(void)
{
	int i;

	for (i = 0; i < SPR_TEXTURE_SIZE - 1; i++)
		spr_data[i].next = &spr_data[i + 1];

	spr_data[i].next = NULL;
	spr_free_head = &spr_data[0];

	memset(spr_head, 0, sizeof(SPRITE *) * SPR_HASH_SIZE);

	spr_texture_num = 0;
	clear_spr_texture = 0;
}


/*------------------------------------------------------------------------
	FIXスプライトのクリアフラグを立てる
------------------------------------------------------------------------*/

void blit_clear_all_sprite(void)
{
	blit_clear_spr_sprite();
	blit_clear_fix_sprite();
}


/*------------------------------------------------------------------------
	FIXスプライトのクリアフラグを立てる
------------------------------------------------------------------------*/

void blit_set_fix_clear_flag(void)
{
	clear_fix_texture = 1;
}


/*------------------------------------------------------------------------
	SPRスプライトのクリアフラグを立てる
------------------------------------------------------------------------*/

void blit_set_spr_clear_flag(void)
{
	clear_spr_texture = 1;
}


/*------------------------------------------------------------------------
	スプライト処理のリセット
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
	clut = (uint16_t *)&video_palettebank[palette_bank];

	blit_clear_all_sprite();
}


/*------------------------------------------------------------------------
	画面の更新開始
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

		fix_num = 0;

		if (clear_spr_texture) blit_clear_spr_sprite();
		if (clear_fix_texture) blit_clear_fix_sprite();

		video_driver->startWorkFrame(video_data, CNVCOL15TO32(video_palette[4095]));
	}
}


/*------------------------------------------------------------------------
	画面の更新終了
------------------------------------------------------------------------*/

void blit_finish(void)
{
	video_driver->transferWorkFrame(video_data, &mvs_src_clip, &mvs_clip[option_stretch]);
}


/*------------------------------------------------------------------------
	FIXを描画リストに登録
------------------------------------------------------------------------*/

void blit_draw_fix(int x, int y, uint32_t code, uint32_t attr)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_FIX_KEY(code, attr);

	if ((idx = fix_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
		uint8_t *src, *dst, lines, row, column;
		uint32_t datal, datah;

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
	FIX描画終了
------------------------------------------------------------------------*/

void blit_finish_fix(void)
{
	if (!fix_num) return;
	video_driver->blitTexture(video_data, TEX_FIX, clut, 0, fix_num, vertices_fix);
}


/*------------------------------------------------------------------------
	SPRを描画リストに登録
------------------------------------------------------------------------*/

void blit_draw_spr(int x, int y, int w, int h, uint32_t code, uint32_t attr)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key;

	key = MAKE_SPR_KEY(code, attr);

	if ((idx = spr_get_sprite(key)) < 0)
	{
		uint32_t col, tile, offset, gfx3_offset;
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
	SPR描画終了
------------------------------------------------------------------------*/

static enum WorkBuffer getWorkBufferForSPR(uint8_t index) {
	// printf("getWorkBufferForSPR(%i)\n", index);
	switch (index) {
		case 0:
			return TEX_SPR0;
		case 1:
			return TEX_SPR1;
		case 2:
			return TEX_SPR2;
		default:
			return TEX_SPR0;
	}
}

void blit_finish_spr(void)
{
	// // printf("blit_finish_spr\n");
	int i, total_sprites = 0;
	uint16_t flags, *pflags = spr_flags;
	struct Vertex *vertices, *vertices_tmp;
	uint16_t *clut_tmp;
	enum WorkBuffer workBuffer;

	if (!spr_index) return;
	// printf("blit_finish_spr has spr_index\n");

	struct Vertex vertex_buffer[spr_num];

	flags = *pflags;
	workBuffer = getWorkBufferForSPR(flags & 3);
	clut_tmp = &clut[flags & 0xf00];

	vertices_tmp = vertices = &vertex_buffer[0];

	for (i = 0; i < spr_num; i += 2)
	{
		if (flags != *pflags)
		{
			if (total_sprites)
			{
				video_driver->blitTexture(video_data, workBuffer, clut_tmp, 0, total_sprites, vertices);
				total_sprites = 0;
				vertices = vertices_tmp;
			}

			flags = *pflags;
			workBuffer = getWorkBufferForSPR(flags & 3);
			clut_tmp = &clut[flags & 0xf00];
		}

		vertices_tmp[0] = vertices_spr[i + 0];
		vertices_tmp[1] = vertices_spr[i + 1];

		vertices_tmp += 2;
		total_sprites += 2;
		pflags++;
	}

	if (total_sprites)
		video_driver->blitTexture(video_data, workBuffer, clut_tmp, 0, total_sprites, vertices);
}


/******************************************************************************
	SPR ソフトウェア描画
******************************************************************************/

static void drawgfxline_fixed(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);
static void drawgfxline_fixed_flip(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);
static void drawgfxline_zoom(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);
static void drawgfxline_zoom_flip(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);

static void drawgfxline_fixed_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);
static void drawgfxline_fixed_flip_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);
static void drawgfxline_zoom_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);
static void drawgfxline_zoom_flip_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);

static void ALIGN_DATA (*drawgfxline[8])(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom) =
{
	drawgfxline_zoom,					// 0000
	drawgfxline_zoom_flip,				// 0001
	drawgfxline_zoom_opaque,			// 0010
	drawgfxline_zoom_flip_opaque,		// 0011
	drawgfxline_fixed,					// 0100
	drawgfxline_fixed_flip,				// 0101
	drawgfxline_fixed_opaque,			// 0110
	drawgfxline_fixed_flip_opaque		// 0111
};


static const uint8_t zoom_x_tables[][16] =
{
	{ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 },
	{ 0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0 },
	{ 0,0,0,0,1,0,0,0,1,0,0,0,0,0,0,0 },
	{ 0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0 },
	{ 0,0,1,0,1,0,0,0,1,0,0,0,1,0,0,0 },
	{ 0,0,1,0,1,0,0,0,1,0,0,0,1,0,1,0 },
	{ 0,0,1,0,1,0,1,0,1,0,0,0,1,0,1,0 },
	{ 0,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0 },
	{ 1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0 },
	{ 1,0,1,0,1,0,1,0,1,1,1,0,1,0,1,0 },
	{ 1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,0 },
	{ 1,0,1,1,1,0,1,0,1,1,1,0,1,0,1,1 },
	{ 1,0,1,1,1,0,1,1,1,1,1,0,1,0,1,1 },
	{ 1,0,1,1,1,0,1,1,1,1,1,0,1,1,1,1 },
	{ 1,1,1,1,1,0,1,1,1,1,1,0,1,1,1,1 },
	{ 1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1 },
//	{ 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1 }
};

static void drawgfxline_zoom(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;
	const uint8_t *zoom_x_table = zoom_x_tables[zoom];

	tile = src[0];
	if (zoom_x_table[ 0]) { if (tile & 0x0000000f) *dst = pal[(tile >>  0) & 0x0f]; dst++; }
	if (zoom_x_table[ 1]) { if (tile & 0x00000f00) *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[ 2]) { if (tile & 0x000f0000) *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[ 3]) { if (tile & 0x0f000000) *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[ 4]) { if (tile & 0x000000f0) *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[ 5]) { if (tile & 0x0000f000) *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[ 6]) { if (tile & 0x00f00000) *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[ 7]) { if (tile & 0xf0000000) *dst = pal[(tile >> 28) & 0x0f]; dst++; }

	tile = src[1];
	if (zoom_x_table[ 8]) { if (tile & 0x0000000f) *dst = pal[(tile >>  0) & 0x0f]; dst++; }
	if (zoom_x_table[ 9]) { if (tile & 0x00000f00) *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[10]) { if (tile & 0x000f0000) *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[11]) { if (tile & 0x0f000000) *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[12]) { if (tile & 0x000000f0) *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[13]) { if (tile & 0x0000f000) *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[14]) { if (tile & 0x00f00000) *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[15]) { if (tile & 0xf0000000) *dst = pal[(tile >> 28) & 0x0f]; }
}

static void drawgfxline_zoom_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;
	const uint8_t *zoom_x_table = zoom_x_tables[zoom];

	tile = src[0];
	if (zoom_x_table[ 0]) { *dst = pal[(tile >>  0) & 0x0f]; dst++; }
	if (zoom_x_table[ 1]) { *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[ 2]) { *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[ 3]) { *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[ 4]) { *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[ 5]) { *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[ 6]) { *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[ 7]) { *dst = pal[(tile >> 28) & 0x0f]; dst++; }

	tile = src[1];
	if (zoom_x_table[ 8]) { *dst = pal[(tile >>  0) & 0x0f]; dst++; }
	if (zoom_x_table[ 9]) { *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[10]) { *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[11]) { *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[12]) { *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[13]) { *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[14]) { *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[15]) { *dst = pal[(tile >> 28) & 0x0f]; }
}

static void drawgfxline_zoom_flip(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;
	const uint8_t *zoom_x_table = zoom_x_tables[zoom];

	tile = src[1];
	if (zoom_x_table[ 0]) { if (tile & 0xf0000000) *dst = pal[(tile >> 28) & 0x0f]; dst++; }
	if (zoom_x_table[ 1]) { if (tile & 0x00f00000) *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[ 2]) { if (tile & 0x0000f000) *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[ 3]) { if (tile & 0x000000f0) *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[ 4]) { if (tile & 0x0f000000) *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[ 5]) { if (tile & 0x000f0000) *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[ 6]) { if (tile & 0x00000f00) *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[ 7]) { if (tile & 0x0000000f) *dst = pal[(tile >>  0) & 0x0f]; dst++; }

	tile = src[0];
	if (zoom_x_table[ 8]) { if (tile & 0xf0000000) *dst = pal[(tile >> 28) & 0x0f]; dst++; }
	if (zoom_x_table[ 9]) { if (tile & 0x00f00000) *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[10]) { if (tile & 0x0000f000) *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[11]) { if (tile & 0x000000f0) *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[12]) { if (tile & 0x0f000000) *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[13]) { if (tile & 0x000f0000) *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[14]) { if (tile & 0x00000f00) *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[15]) { if (tile & 0x0000000f) *dst = pal[(tile >>  0) & 0x0f]; }
}

static void drawgfxline_zoom_flip_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;
	const uint8_t *zoom_x_table = zoom_x_tables[zoom];

	tile = src[1];
	if (zoom_x_table[ 0]) { *dst = pal[(tile >> 28) & 0x0f]; dst++; }
	if (zoom_x_table[ 1]) { *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[ 2]) { *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[ 3]) { *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[ 4]) { *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[ 5]) { *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[ 6]) { *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[ 7]) { *dst = pal[(tile >>  0) & 0x0f]; dst++; }

	tile = src[0];
	if (zoom_x_table[ 8]) { *dst = pal[(tile >> 28) & 0x0f]; dst++; }
	if (zoom_x_table[ 9]) { *dst = pal[(tile >> 20) & 0x0f]; dst++; }
	if (zoom_x_table[10]) { *dst = pal[(tile >> 12) & 0x0f]; dst++; }
	if (zoom_x_table[11]) { *dst = pal[(tile >>  4) & 0x0f]; dst++; }
	if (zoom_x_table[12]) { *dst = pal[(tile >> 24) & 0x0f]; dst++; }
	if (zoom_x_table[13]) { *dst = pal[(tile >> 16) & 0x0f]; dst++; }
	if (zoom_x_table[14]) { *dst = pal[(tile >>  8) & 0x0f]; dst++; }
	if (zoom_x_table[15]) { *dst = pal[(tile >>  0) & 0x0f]; }
}

static void drawgfxline_fixed(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;

	tile = src[0];
	if (tile)
	{
		if (tile & 0x000f) dst[ 0] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[ 4] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[ 1] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[ 5] = pal[(tile >> 12) & 0x0f];
		tile >>= 16;
		if (tile & 0x000f) dst[ 2] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[ 6] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[ 3] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[ 7] = pal[(tile >> 12) & 0x0f];
	}
	tile = src[1];
	if (tile)
	{
		if (tile & 0x000f) dst[ 8] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[12] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[ 9] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[13] = pal[(tile >> 12) & 0x0f];
		tile >>= 16;
		if (tile & 0x000f) dst[10] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[14] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[11] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[15] = pal[(tile >> 12) & 0x0f];
	}
}

static void drawgfxline_fixed_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;

	tile = src[0];
	dst[ 0] = pal[(tile >>  0) & 0x0f];
	dst[ 4] = pal[(tile >>  4) & 0x0f];
	dst[ 1] = pal[(tile >>  8) & 0x0f];
	dst[ 5] = pal[(tile >> 12) & 0x0f];
	dst[ 2] = pal[(tile >> 16) & 0x0f];
	dst[ 6] = pal[(tile >> 20) & 0x0f];
	dst[ 3] = pal[(tile >> 24) & 0x0f];
	dst[ 7] = pal[(tile >> 28) & 0x0f];

	tile = src[1];
	dst[ 8] = pal[(tile >>  0) & 0x0f];
	dst[12] = pal[(tile >>  4) & 0x0f];
	dst[ 9] = pal[(tile >>  8) & 0x0f];
	dst[13] = pal[(tile >> 12) & 0x0f];
	dst[10] = pal[(tile >> 16) & 0x0f];
	dst[14] = pal[(tile >> 20) & 0x0f];
	dst[11] = pal[(tile >> 24) & 0x0f];
	dst[15] = pal[(tile >> 28) & 0x0f];
}

static void drawgfxline_fixed_flip(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;

	tile = src[0];
	if (tile)
	{
		if (tile & 0x000f) dst[15] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[11] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[14] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[10] = pal[(tile >> 12) & 0x0f];
		tile >>= 16;
		if (tile & 0x000f) dst[13] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[ 9] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[12] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[ 8] = pal[(tile >> 12) & 0x0f];
	}
	tile = src[1];
	if (tile)
	{
		if (tile & 0x000f) dst[ 7] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[ 3] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[ 6] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[ 2] = pal[(tile >> 12) & 0x0f];
		tile >>= 16;
		if (tile & 0x000f) dst[ 5] = pal[(tile >>  0) & 0x0f];
		if (tile & 0x00f0) dst[ 1] = pal[(tile >>  4) & 0x0f];
		if (tile & 0x0f00) dst[ 4] = pal[(tile >>  8) & 0x0f];
		if (tile & 0xf000) dst[ 0] = pal[(tile >> 12) & 0x0f];
	}
}

static void drawgfxline_fixed_flip_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom)
{
	uint32_t tile;

	tile = src[1];
	dst[ 0] = pal[(tile >> 28) & 0x0f];
	dst[ 1] = pal[(tile >> 20) & 0x0f];
	dst[ 2] = pal[(tile >> 12) & 0x0f];
	dst[ 3] = pal[(tile >>  4) & 0x0f];
	dst[ 4] = pal[(tile >> 24) & 0x0f];
	dst[ 5] = pal[(tile >> 16) & 0x0f];
	dst[ 6] = pal[(tile >>  8) & 0x0f];
	dst[ 7] = pal[(tile >>  0) & 0x0f];

	tile = src[0];
	dst[ 8] = pal[(tile >> 28) & 0x0f];
	dst[ 9] = pal[(tile >> 20) & 0x0f];
	dst[10] = pal[(tile >> 12) & 0x0f];
	dst[11] = pal[(tile >>  4) & 0x0f];
	dst[12] = pal[(tile >> 24) & 0x0f];
	dst[13] = pal[(tile >> 16) & 0x0f];
	dst[14] = pal[(tile >>  8) & 0x0f];
	dst[15] = pal[(tile >>  0) & 0x0f];
}

void blit_draw_spr_line(int x, int y, int zoom_x, int sprite_y, uint32_t code, uint16_t attr, uint8_t opaque)
{
	uint32_t src = code << 7;
	uint32_t dst = (y << 9) + x;
	uint8_t flag = (attr & 1) | (opaque & SPRITE_OPAQUE) | ((zoom_x & 0x10) >> 2);

	if (attr & 0x0002) sprite_y ^= 0x0f;
	src += sprite_y << 3;

	(*drawgfxline[flag])((uint32_t *)&memory_region_gfx2[src], &scrbitmap[dst], &video_palette[(attr >> 8) << 4], zoom_x);
}
