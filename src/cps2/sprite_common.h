/******************************************************************************

	sprite_common.h

	CPS2 Common sprite management declarations

******************************************************************************/

#ifndef CPS2_SPRITE_COMMON_H
#define CPS2_SPRITE_COMMON_H

#include "cps2.h"

/******************************************************************************
	Constants/Macros
******************************************************************************/

#define MAKE_KEY(code, attr)	(code | ((attr & 0x0f) << 28))

/* OBJECT: 16x16 sprites for characters, projectiles, etc. */
#define OBJECT_HASH_SIZE		0x200
#define OBJECT_HASH_MASK		0x1ff
#define OBJECT_TEXTURE_SIZE		((BUF_WIDTH/16)*(TEXTURE_HEIGHT/16))
#define OBJECT_MAX_SPRITES		0x1400

/* SCROLL1: 8x8 tiles */
#define SCROLL1_HASH_SIZE		0x200
#define SCROLL1_HASH_MASK		0x1ff
#define SCROLL1_TEXTURE_SIZE	((BUF_WIDTH/8)*(TEXTURE_HEIGHT/8))
#define SCROLL1_MAX_SPRITES		((384/8 + 2) * (224/8 + 2))

/* SCROLL2: 16x16 tiles */
#define SCROLL2_HASH_SIZE		0x100
#define SCROLL2_HASH_MASK		0xff
#define SCROLL2_TEXTURE_SIZE	((BUF_WIDTH/16)*(TEXTURE_HEIGHT/16))
#define SCROLL2_MAX_SPRITES		((384/16 + 2) * (224/16 + 2))

/* SCROLL3: 32x32 tiles */
#define SCROLL3_HASH_SIZE		0x40
#define SCROLL3_HASH_MASK		0x3f
#define SCROLL3_TEXTURE_SIZE	((BUF_WIDTH/32)*(TEXTURE_HEIGHT/32))
#define SCROLL3_MAX_SPRITES		((384/32 + 2) * (224/32 + 2))

/******************************************************************************
	Structures
******************************************************************************/

typedef struct sprite_t SPRITE;

struct sprite_t
{
	uint32_t key;
	uint32_t used;
	int32_t index;
	SPRITE *next;
};

/******************************************************************************
	Shared variables (extern declarations)
******************************************************************************/

/* OBJECT */
extern SPRITE ALIGN16_DATA *object_head[OBJECT_HASH_SIZE];
extern SPRITE ALIGN16_DATA object_data[OBJECT_TEXTURE_SIZE];
extern SPRITE ALIGN16_DATA *object_free_head;
extern uint8_t *tex_object;
extern uint16_t object_texture_num;

/* SCROLL1 */
extern SPRITE ALIGN16_DATA *scroll1_head[SCROLL1_HASH_SIZE];
extern SPRITE ALIGN16_DATA scroll1_data[SCROLL1_TEXTURE_SIZE];
extern SPRITE ALIGN16_DATA *scroll1_free_head;
extern uint8_t *tex_scroll1;
extern uint16_t scroll1_texture_num;

/* SCROLL2 */
extern SPRITE ALIGN16_DATA *scroll2_head[SCROLL2_HASH_SIZE];
extern SPRITE ALIGN16_DATA scroll2_data[SCROLL2_TEXTURE_SIZE];
extern SPRITE ALIGN16_DATA *scroll2_free_head;
extern uint8_t *tex_scroll2;
extern uint16_t scroll2_texture_num;

/* SCROLL3 */
extern SPRITE ALIGN16_DATA *scroll3_head[SCROLL3_HASH_SIZE];
extern SPRITE ALIGN16_DATA scroll3_data[SCROLL3_TEXTURE_SIZE];
extern SPRITE ALIGN16_DATA *scroll3_free_head;
extern uint8_t *tex_scroll3;
extern uint16_t scroll3_texture_num;

/* Scroll2 clipping */
extern int16_t scroll2_min_y;
extern int16_t scroll2_max_y;
extern int16_t scroll2_sy;
extern int16_t scroll2_ey;

/* General clipping */
extern int16_t clip_min_y;
extern int16_t clip_max_y;
extern int16_t object_min_y;

/* Pen usage */
extern uint8_t *pen_usage;

/* Screen bitmap */
extern uint16_t *scrbitmap;

/* Color table for palette index encoding */
extern const uint32_t ALIGN16_DATA color_table[16];

/* Swizzle table (8-bit color) */
extern const int ALIGN16_DATA swizzle_table_8bit[32];

/* Frame counter (from vidhrdw.c) */
extern uint32_t frames_displayed;

/* Software rendering functions for SCROLL2 */
void drawgfx16_16x16(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines);
void drawgfx16_16x16_flipx(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines);
void drawgfx16_16x16_flipy(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines);
void drawgfx16_16x16_flipxy(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines);

void drawgfx16_16x16_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines);
void drawgfx16_16x16_flipx_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines);
void drawgfx16_16x16_flipy_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines);
void drawgfx16_16x16_flipxy_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines);

/* Function pointer array for software rendering */
extern void ALIGN16_DATA (*drawgfx16[8])(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines);

/******************************************************************************
	Common function declarations
******************************************************************************/

/* Sprite cache management */
int32_t object_get_sprite(uint32_t key);
int32_t object_insert_sprite(uint32_t key);
void object_delete_sprite(void);

int32_t scroll1_get_sprite(uint32_t key);
int32_t scroll1_insert_sprite(uint32_t key);
void scroll1_delete_sprite(void);

int32_t scroll2_get_sprite(uint32_t key);
int32_t scroll2_insert_sprite(uint32_t key);
void scroll2_delete_sprite(void);

int32_t scroll3_get_sprite(uint32_t key);
int32_t scroll3_insert_sprite(uint32_t key);
void scroll3_delete_sprite(void);

/* Platform-agnostic blit functions */
void blit_clear_all_sprite(void);
void blit_update_object(int16_t x, int16_t y, uint32_t code, uint16_t attr);
void blit_update_scroll1(int16_t x, int16_t y, uint32_t code, uint16_t attr);
void blit_update_scroll2(int16_t x, int16_t y, uint32_t code, uint16_t attr);
void blit_update_scroll3(int16_t x, int16_t y, uint32_t code, uint16_t attr);
void blit_set_clip_scroll2(int16_t min_y, int16_t max_y);
int blit_check_clip_scroll2(int16_t sy);

#endif /* CPS2_SPRITE_COMMON_H */
