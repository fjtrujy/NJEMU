/******************************************************************************

	sprite_common.h

	CPS1 Common sprite management declarations

	Hardware Reference:
	- https://fabiensanglard.net/cps1_gfx/index.html
	- https://arcadehacker.blogspot.com/2015/04/capcom-cps1-part-1.html

	CPS-1 Graphics System:
	- 6 compositable layers: SCROLL1 (8x8), SCROLL2 (16x16), SCROLL3 (32x32),
	  OBJ (16x16 sprites), STAR1, STAR2
	- 256 sprites per scanline maximum
	- 192 palettes × 16 colors = 4,096 on-screen colors
	- Layer priorities freely configurable via hardware registers

******************************************************************************/

#ifndef CPS1_SPRITE_COMMON_H
#define CPS1_SPRITE_COMMON_H

#include "cps1.h"

/******************************************************************************
	Constants/Macros
******************************************************************************/

#define SCROLLH_MAX_HEIGHT	192

/* Tiles per line for linear (non-swizzled) texture layout */
#define TILE_8x8_PER_LINE	(BUF_WIDTH/8)
#define TILE_16x16_PER_LINE	(BUF_WIDTH/16)
#define TILE_32x32_PER_LINE	(BUF_WIDTH/32)

#define MAKE_KEY(code, attr)		(code | ((attr & 0x0f) << 28))
#define MAKE_HIGH_KEY(code, attr)	(code | ((attr & 0x19f) << 16))

/* OBJECT: 16x16 sprites for characters, projectiles, etc.
   Hardware limit: 256 sprites per scanline */
#define OBJECT_HASH_SIZE		0x200
#define OBJECT_HASH_MASK		0x1ff
#define OBJECT_TEXTURE_SIZE		((BUF_WIDTH/16)*(TEXTURE_HEIGHT/16))
#define OBJECT_MAX_SPRITES		0x1000

/* SCROLL1: 8x8 tiles, 512x512 tilemap
   Finest granularity - typically used for GUI/text elements */
#define SCROLL1_HASH_SIZE		0x200
#define SCROLL1_HASH_MASK		0x1ff
#define SCROLL1_TEXTURE_SIZE	((BUF_WIDTH/8)*(TEXTURE_HEIGHT/8))
#define SCROLL1_MAX_SPRITES		((384/8 + 2) * (224/8 + 2))

/* SCROLL2: 16x16 tiles, 1024x1024 tilemap
   Main background layer, supports per-line horizontal scrolling for parallax */
#define SCROLL2_HASH_SIZE		0x100
#define SCROLL2_HASH_MASK		0xff
#define SCROLL2_TEXTURE_SIZE	((BUF_WIDTH/16)*(TEXTURE_HEIGHT/16))
#define SCROLL2_MAX_SPRITES		((384/16 + 2) * (224/16 + 2))

/* SCROLL3: 32x32 tiles, 2048x2048 tilemap
   Large background tiles for distant/static scenery */
#define SCROLL3_HASH_SIZE		0x40
#define SCROLL3_HASH_MASK		0x3f
#define SCROLL3_TEXTURE_SIZE	((BUF_WIDTH/32)*(TEXTURE_HEIGHT/32))
#define SCROLL3_MAX_SPRITES		((384/32 + 2) * (224/32 + 2))

/* SCROLLH: High priority scroll layer */
#define SCROLLH_HASH_SIZE		0x200
#define SCROLLH_HASH_MASK		0x1ff
#define SCROLLH_TEXTURE_SIZE	((BUF_WIDTH/8)*(SCROLLH_MAX_HEIGHT/8))
#define SCROLLH_MAX_SPRITES		SCROLL1_MAX_SPRITES

#define SCROLL1H_TEXTURE_SIZE	SCROLLH_TEXTURE_SIZE
#define SCROLL1H_MAX_SPRITES	SCROLL1_MAX_SPRITES
#define SCROLL2H_TEXTURE_SIZE	((BUF_WIDTH/16)*(SCROLLH_MAX_HEIGHT/16))
#define SCROLL2H_MAX_SPRITES	SCROLL2_MAX_SPRITES
#define SCROLL3H_TEXTURE_SIZE	((BUF_WIDTH/32)*(SCROLLH_MAX_HEIGHT/32))
#define SCROLL3H_MAX_SPRITES	SCROLL3_MAX_SPRITES

/******************************************************************************
	Structures
******************************************************************************/

typedef struct sprite_t SPRITE;

struct sprite_t
{
	uint32_t key;
	uint32_t used;
	int16_t index;
	uint16_t pal;
	SPRITE *next;
};

/******************************************************************************
	Shared variables (extern declarations)
******************************************************************************/

/* Palette */
extern uint8_t ALIGN16_DATA palette_dirty_marks[256];

/* OBJECT */
extern SPRITE ALIGN16_DATA *object_head[OBJECT_HASH_SIZE];
extern SPRITE ALIGN16_DATA object_data[OBJECT_TEXTURE_SIZE];
extern SPRITE *object_free_head;
extern uint8_t *gfx_object;
extern uint8_t *tex_object;
extern uint16_t object_texture_num;

/* SCROLL1 */
extern SPRITE ALIGN16_DATA *scroll1_head[SCROLL1_HASH_SIZE];
extern SPRITE ALIGN16_DATA scroll1_data[SCROLL1_TEXTURE_SIZE];
extern SPRITE *scroll1_free_head;
extern uint8_t *gfx_scroll1;
extern uint8_t *tex_scroll1;
extern uint16_t scroll1_texture_num;

/* SCROLL2 */
extern SPRITE ALIGN16_DATA *scroll2_head[SCROLL2_HASH_SIZE];
extern SPRITE ALIGN16_DATA scroll2_data[SCROLL2_TEXTURE_SIZE];
extern SPRITE *scroll2_free_head;
extern uint8_t *gfx_scroll2;
extern uint8_t *tex_scroll2;
extern uint16_t scroll2_texture_num;

/* SCROLL3 */
extern SPRITE ALIGN16_DATA *scroll3_head[SCROLL3_HASH_SIZE];
extern SPRITE ALIGN16_DATA scroll3_data[SCROLL3_TEXTURE_SIZE];
extern SPRITE *scroll3_free_head;
extern uint8_t *gfx_scroll3;
extern uint8_t *tex_scroll3;
extern uint16_t scroll3_texture_num;

/* SCROLLH */
extern SPRITE ALIGN16_DATA *scrollh_head[SCROLLH_HASH_SIZE];
extern SPRITE ALIGN16_DATA scrollh_data[SCROLLH_TEXTURE_SIZE];
extern SPRITE *scrollh_free_head;
extern uint16_t *tex_scrollh;
extern uint16_t scrollh_num;
extern uint16_t scrollh_texture_num;
extern uint8_t scrollh_texture_clear;
extern uint8_t scrollh_layer_number;
extern uint8_t scroll1_palette_is_dirty;
extern uint8_t scroll2_palette_is_dirty;
extern uint8_t scroll3_palette_is_dirty;

/* Scroll2 clipping */
extern int16_t scroll2_min_y;
extern int16_t scroll2_max_y;
extern int16_t scroll2_sy;
extern int16_t scroll2_ey;

/* Pen usage */
extern uint8_t *pen_usage;

/* Color table */
extern const uint32_t ALIGN16_DATA color_table[16];

/* Frame counter (from vidhrdw.c) */
extern uint32_t frames_displayed;

/******************************************************************************
	Common function declarations
******************************************************************************/

/* OBJECT sprite management */
int16_t object_get_sprite(uint32_t key);
int16_t object_insert_sprite(uint32_t key);
void object_delete_sprite(void);

/* SCROLL1 sprite management */
int16_t scroll1_get_sprite(uint32_t key);
int16_t scroll1_insert_sprite(uint32_t key);
void scroll1_delete_sprite(void);

/* SCROLL2 sprite management */
int16_t scroll2_get_sprite(uint32_t key);
int16_t scroll2_insert_sprite(uint32_t key);
void scroll2_delete_sprite(void);

/* SCROLL3 sprite management */
int16_t scroll3_get_sprite(uint32_t key);
int16_t scroll3_insert_sprite(uint32_t key);
void scroll3_delete_sprite(void);

/* SCROLLH sprite management */
void scrollh_reset_sprite(void);
int16_t scrollh_get_sprite(uint32_t key);
int16_t scrollh_insert_sprite(uint32_t key);
void scrollh_delete_sprite(void);
void scrollh_delete_sprite_tpens(uint16_t tpens);
void scrollh_delete_dirty_palette(void);

/* Platform-agnostic blit functions */
void blit_clear_all_sprite(void);
void blit_scrollh_clear_sprite(uint16_t tpens);
void blit_palette_mark_dirty(int palno);
void blit_update_object(int16_t x, int16_t y, uint32_t code, uint16_t attr);
void blit_update_scroll1(int16_t x, int16_t y, uint32_t code, uint16_t attr);
void blit_update_scroll2(int16_t x, int16_t y, uint32_t code, uint16_t attr);
void blit_update_scroll3(int16_t x, int16_t y, uint32_t code, uint16_t attr);
void blit_update_scroll2h(int16_t x, int16_t y, uint32_t code, uint16_t attr);
void blit_update_scrollh(int16_t x, int16_t y, uint32_t code, uint16_t attr);
int blit_check_clip_scroll2(int16_t sy);

#endif /* CPS1_SPRITE_COMMON_H */

