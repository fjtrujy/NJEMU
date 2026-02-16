/******************************************************************************

	sprite_common.c

	NCDZ Common sprite management - shared across all platforms

******************************************************************************/

#include "sprite_common.h"

/******************************************************************************
	Shared variable definitions
******************************************************************************/

SPRITE ALIGN16_DATA *fix_head[FIX_HASH_SIZE];
SPRITE ALIGN16_DATA fix_data[FIX_TEXTURE_SIZE];
SPRITE *fix_free_head;
uint16_t fix_num;
uint16_t fix_texture_num;

SPRITE ALIGN16_DATA *spr_head[SPR_HASH_SIZE];
SPRITE ALIGN16_DATA spr_data[SPR_TEXTURE_SIZE];
SPRITE *spr_free_head;
uint16_t spr_num;
uint16_t spr_texture_num;
uint16_t spr_index;

int clip_min_y;
int clip_max_y;
int clear_spr_texture;
int clear_fix_texture;

uint8_t *tex_fix;
uint8_t *tex_spr[3];
uint16_t *clut;

/*
 * Color table for palette index encoding
 * Each entry broadcasts a 4-bit palette offset to all bytes of a 32-bit word
 * Used when rendering tiles to texture cache
 */
const uint32_t ALIGN16_DATA color_table[16] =
{
	0x00000000, 0x10101010, 0x20202020, 0x30303030,
	0x40404040, 0x50505050, 0x60606060, 0x70707070,
	0x80808080, 0x90909090, 0xa0a0a0a0, 0xb0b0b0b0,
	0xc0c0c0c0, 0xd0d0d0d0, 0xe0e0e0e0, 0xf0f0f0f0
};

/******************************************************************************
	FIX Sprite Management
******************************************************************************/

/*------------------------------------------------------------------------
	Get sprite number from FIX texture
------------------------------------------------------------------------*/

int fix_get_sprite(uint32_t key)
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
	Register sprite in FIX texture
------------------------------------------------------------------------*/

int fix_insert_sprite(uint32_t key)
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
	Delete expired sprites from FIX texture
------------------------------------------------------------------------*/

void fix_delete_sprite(void)
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
	SPR Sprite Management
******************************************************************************/

/*------------------------------------------------------------------------
	Get sprite number from SPR texture
------------------------------------------------------------------------*/

int spr_get_sprite(uint32_t key)
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
	Register sprite in SPR texture
------------------------------------------------------------------------*/

int spr_insert_sprite(uint32_t key)
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
	Delete expired sprites from SPR texture
------------------------------------------------------------------------*/

void spr_delete_sprite(void)
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
	Sprite clear functions
******************************************************************************/

/*------------------------------------------------------------------------
	Clear FIX sprites immediately
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
	Clear SPR sprites immediately
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


/******************************************************************************
	Common sprite interface functions
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
