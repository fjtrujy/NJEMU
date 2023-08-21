/******************************************************************************

	sprite.c

	MVS スプライトマネージャ

******************************************************************************/

#include "mvs.h"

#include <gsKit.h>
#include <gsInline.h>

/******************************************************************************
	定数/マクロ等
******************************************************************************/

#define MAKE_FIX_KEY(code, attr)	(code | (attr << 28))
#define MAKE_SPR_KEY(code, attr)	(code | ((attr & 0x0f00) << 20))
#define PSP_UNCACHE_PTR(p)			(((uint32_t)(p)) | 0x40000000)


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

static RECT mvs_clip[6] =
{
	{ 88, 24, 88 + 304, 24 + 224 },	// option_stretch = 0  (304x224 19:14)
	{ 80, 16, 80 + 320, 16 + 240 },	// option_stretch = 1  (320x240  4:3)
	{ 60,  1, 60 + 360,  1 + 270 },	// option_stretch = 2  (360x270  4:3)
	{ 57,  1, 57 + 360,  1 + 270 },	// option_stretch = 3  (366x270 19:14)
	{ 30,  1, 30 + 420,  1 + 270 },	// option_stretch = 4  (420x270 14:9)
	{  0,  1,  0 + 480,  1 + 270 }	    // option_stretch = 5  (480x270 16:9)
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

static SPRITE ALIGN_DATA *fix_head[FIX_HASH_SIZE];
static SPRITE ALIGN_DATA fix_data[FIX_TEXTURE_SIZE];
static SPRITE *fix_free_head;

static uint8_t *tex_fix;
static GSPRIMUVPOINT ALIGN_DATA vertices_fix[FIX_MAX_SPRITES * 2];
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
static GSPRIMUVPOINT ALIGN_DATA vertices_spr[SPR_MAX_SPRITES * 2];
static uint16_t ALIGN_DATA spr_flags[SPR_MAX_SPRITES];
static uint16_t spr_num;
static uint16_t spr_texture_num;
static uint16_t spr_index;
static uint8_t spr_disable;


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


/*------------------------------------------------------------------------
	'swizzle'テクスチャアドレス計算テーブル
------------------------------------------------------------------------*/

static const int ALIGN_DATA swizzle_table_8bit[16] =
{
	   0, 16, 16, 16, 16, 16, 16, 16,
	3984, 16, 16, 16, 16, 16, 16, 16
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
			if (p->used != frames_displayed)
			{
				update_cache(key << 7);
				p->used = frames_displayed;
			}
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

static void blit_clear_fix_sprite(void)
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
	SPRスプライトを即座にクリアする
------------------------------------------------------------------------*/

static void blit_clear_spr_sprite(void)
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
	全てのスプライトを即座にクリアする
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

	clut = (uint16_t *)&video_palettebank[palette_bank];

	blit_clear_all_sprite();
}


/*------------------------------------------------------------------------
	画面の更新開始
------------------------------------------------------------------------*/

void blit_start(int start, int end)
{
	printf("blit_start(%d, %d)\n", start, end);
	clip_min_y = start;
	clip_max_y = end + 1;

	spr_num   = 0;
	spr_index = 0;

	if (start == FIRST_VISIBLE_LINE)
	{
		clut = (uint16_t *)&video_palettebank[palette_bank];

		fix_num = 0;
		spr_disable = 0;

		if (clear_spr_texture) blit_clear_spr_sprite();
		if (clear_fix_texture) blit_clear_fix_sprite();

		video_driver->clearScreen(video_data);

		// sceGuStart(GU_DIRECT, gulist);
	// 	sceGuDrawBufferList(GU_PSM_5551, draw_frame, BUF_WIDTH);
	// 	sceGuScissor(0, 0, BUF_WIDTH, SCR_WIDTH);
	// 	sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);

	// 	sceGuDrawBufferList(GU_PSM_5551, work_frame, BUF_WIDTH);
	// 	sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);

	// 	sceGuScissor(24, 16, 336, 240);
	// 	sceGuClearColor(CNVCOL15TO32(video_palette[4095]));
	// 	sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);

	// 	sceGuClearColor(0);
	// 	sceGuEnable(GU_ALPHA_TEST);
	// 	sceGuTexMode(GU_PSM_T8, 0, 0, GU_TRUE);
	// 	sceGuTexFilter(GU_NEAREST, GU_NEAREST);

	// 	sceGuFinish();
	// 	sceGuSync(0, GU_SYNC_FINISH);
	}
}


/*------------------------------------------------------------------------
	画面の更新終了
------------------------------------------------------------------------*/

void blit_finish(void)
{
	printf("blit_finish\n");
	video_driver->transferWorkFrame(video_data, &mvs_src_clip, &mvs_clip[option_stretch]);
}


/*------------------------------------------------------------------------
	FIXを描画リストに登録
------------------------------------------------------------------------*/

void blit_draw_fix(int x, int y, uint32_t code, uint16_t attr)
{
	int16_t idx;
	GSPRIMUVPOINT *vertices;
	uint32_t key = MAKE_FIX_KEY(code, attr);
	gs_rgbaq color = color_to_RGBAQ(0x80, 0x80, 0x80, 0x80, 0);

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

	GSGLOBAL *gsGlobal = video_driver->getNativeObjects(video_data, 0);
	GSTEXTURE *atlas = video_driver->getNativeObjects(video_data, 5);
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


	vertices[0].xyz2 = vertex_to_XYZ2(gsGlobal, x0, y0, 0);
	vertices[0].uv = vertex_to_UV(atlas, u0, v0);
	vertices[0].rgbaq = color;

	vertices[1].xyz2 = vertex_to_XYZ2(gsGlobal, x1, y1, 0);
	vertices[1].uv = vertex_to_UV(atlas, u1, v1);
	vertices[1].rgbaq = color;
}


/*------------------------------------------------------------------------
	FIX描画終了
------------------------------------------------------------------------*/

void blit_finish_fix(void)
{
	printf("blit_finish_fix %i\n", fix_num);

	if (!fix_num) return;

	video_driver->blitFinishFix(video_data, TEX_FIX, clut, fix_num, vertices_fix);
}


/*------------------------------------------------------------------------
	SPRを描画リストに登録
------------------------------------------------------------------------*/

void blit_draw_spr(int x, int y, int w, int h, uint32_t code, uint16_t attr)
{
	int16_t idx;
	GSPRIMUVPOINT *vertices;
	gs_rgbaq color = color_to_RGBAQ(0x80, 0x80, 0x80, 0x80, 0);
	uint32_t key;

	if (spr_disable) return;

	key = MAKE_SPR_KEY(code, attr);

	if ((idx = spr_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
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
		src = &memory_region_gfx3[(*read_cache)(code << 7)];
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
	u1 = (index_u ? u1 : u0) + 16;
	v1 = (index_v ? v1 : v0) + 16;

	GSGLOBAL *gsGlobal = video_driver->getNativeObjects(video_data, 0);
	GSTEXTURE *atlas = video_driver->getNativeObjects(video_data, 2);

	vertices[0].xyz2 = vertex_to_XYZ2(gsGlobal, x0, y0, 0);
	vertices[0].uv = vertex_to_UV(atlas, u0, v0);
	vertices[0].rgbaq = color;

	vertices[1].xyz2 = vertex_to_XYZ2(gsGlobal, x1, y1, 0);
	vertices[1].uv = vertex_to_UV(atlas, u1, v1);
	vertices[1].rgbaq = color;
}


/*------------------------------------------------------------------------
	SPR描画終了
------------------------------------------------------------------------*/

void blit_finish_spr(void)
{
	printf("blit_finish_spr\n");
	int i, total_sprites = 0;
	uint16_t flags, *pflags = spr_flags;
	GSPRIMUVPOINT *vertices, *vertices_tmp;

	if (!spr_index) return;

	// flags = *pflags;

	// sceGuStart(GU_DIRECT, gulist);
	// sceGuDrawBufferList(GU_PSM_5551, work_frame, BUF_WIDTH);
	// sceGuScissor(24, clip_min_y, 336, clip_max_y);
	// sceGuTexImage(0, 512, 512, BUF_WIDTH, tex_spr[flags & 3]);
	// sceGuClutLoad(256/8, &clut[flags & 0xf00]);

	// vertices_tmp = vertices = (struct Vertex *)sceGuGetMemory(spr_num * sizeof(struct Vertex));

	// for (i = 0; i < spr_num; i += 2)
	// {
	// 	if (flags != *pflags)
	// 	{
	// 		if (total_sprites)
	// 		{
	// 			sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, total_sprites, NULL, vertices);
	// 			total_sprites = 0;
	// 			vertices = vertices_tmp;
	// 		}

	// 		flags = *pflags;
	// 		sceGuTexImage(0, 512, 512, BUF_WIDTH, tex_spr[flags & 3]);
	// 		sceGuClutLoad(256/8, &clut[flags & 0xf00]);
	// 	}

	// 	vertices_tmp[0] = vertices_spr[i + 0];
	// 	vertices_tmp[1] = vertices_spr[i + 1];

	// 	vertices_tmp += 2;
	// 	total_sprites += 2;
	// 	pflags++;
	// }

	// if (total_sprites)
	// 	sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, total_sprites, NULL, vertices);

	// sceGuFinish();
	// sceGuSync(0, GU_SYNC_FINISH);
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
	uint32_t src = (*read_cache)(code << 7);
	uint32_t dst = (y << 9) + x;
	uint8_t flag = (attr & 1) | (opaque & SPRITE_OPAQUE) | ((zoom_x & 0x10) >> 2);

	if (attr & 0x0002) sprite_y ^= 0x0f;
	src += sprite_y << 3;

	(*drawgfxline[flag])((uint32_t *)&memory_region_gfx3[src], &scrbitmap[dst], &video_palette[(attr >> 8) << 4], zoom_x);
}
