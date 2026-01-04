/******************************************************************************

	sprite_common.h

	NCDZ Common sprite management declarations

******************************************************************************/

#ifndef NCDZ_SPRITE_COMMON_H
#define NCDZ_SPRITE_COMMON_H

#include "ncdz.h"

/******************************************************************************
	Constants/Macros
******************************************************************************/

#define SPRITE_BLANK		0x00
#define SPRITE_TRANSPARENT	0x01
#define SPRITE_OPAQUE		0x02

#define MAKE_FIX_KEY(code, attr)	(uint32_t)(code | (((uint32_t)attr) << 28))
#define MAKE_SPR_KEY(code, attr)	(uint32_t)(code | (((uint32_t)(attr & 0x0f00)) << 20))

#define FIX_TEXTURE_SIZE	((BUF_WIDTH/8)*(TEXTURE_HEIGHT/8))
#define FIX_HASH_MASK		0x1ff
#define FIX_HASH_SIZE		0x200
#define FIX_MAX_SPRITES		((320/8) * (240/8))
#define TILE_8x8_PER_LINE	(BUF_WIDTH/8)
#define TILE_16x16_PER_LINE	(BUF_WIDTH/16)

#define SPR_TEXTURE_SIZE	((BUF_WIDTH/16)*((TEXTURE_HEIGHT*3)/16))
#define SPR_HASH_MASK		0x1ff
#define SPR_HASH_SIZE		0x200
#define SPR_MAX_SPRITES		0x3000

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

extern SPRITE ALIGN_DATA *fix_head[FIX_HASH_SIZE];
extern SPRITE ALIGN_DATA fix_data[FIX_TEXTURE_SIZE];
extern SPRITE *fix_free_head;
extern uint16_t fix_num;
extern uint16_t fix_texture_num;

extern SPRITE ALIGN_DATA *spr_head[SPR_HASH_SIZE];
extern SPRITE ALIGN_DATA spr_data[SPR_TEXTURE_SIZE];
extern SPRITE *spr_free_head;
extern uint16_t spr_num;
extern uint16_t spr_texture_num;
extern uint16_t spr_index;

extern int clip_min_y;
extern int clip_max_y;
extern int clear_spr_texture;
extern int clear_fix_texture;

extern uint16_t *scrbitmap;
extern uint8_t *tex_fix;
extern uint8_t *tex_spr[3];
extern uint16_t *clut;

extern const uint32_t ALIGN_DATA color_table[16];
extern const uint8_t zoom_x_tables[][16];

/******************************************************************************
	Common function declarations
******************************************************************************/

/* Sprite cache management */
int fix_get_sprite(uint32_t key);
int fix_insert_sprite(uint32_t key);
void fix_delete_sprite(void);

int spr_get_sprite(uint32_t key);
int spr_insert_sprite(uint32_t key);
void spr_delete_sprite(void);

void blit_clear_fix_sprite(void);
void blit_clear_spr_sprite(void);

/* Software rendering functions */
void drawgfxline_fixed(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);
void drawgfxline_fixed_flip(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);
void drawgfxline_zoom(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);
void drawgfxline_zoom_flip(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);
void drawgfxline_fixed_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);
void drawgfxline_fixed_flip_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);
void drawgfxline_zoom_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);
void drawgfxline_zoom_flip_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);

extern void ALIGN_DATA (*drawgfxline[8])(uint32_t *src, uint16_t *dst, uint16_t *pal, int zoom);

/* Common sprite interface functions */
void blit_clear_all_sprite(void);
void blit_set_fix_clear_flag(void);
void blit_set_spr_clear_flag(void);

#endif /* NCDZ_SPRITE_COMMON_H */
