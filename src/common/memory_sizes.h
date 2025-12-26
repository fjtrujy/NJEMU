/******************************************************************************

	memory_sizes.h

	Memory size definitions for NJEMU emulators.
	Provides meaningful names for all memory allocation sizes.

******************************************************************************/

#ifndef MEMORY_SIZES_H
#define MEMORY_SIZES_H

/******************************************************************************
	Size unit helpers
******************************************************************************/

#define KB(x)	((x) * 1024)
#define MB(x)	((x) * 1024 * 1024)

/******************************************************************************
	Common CPU RAM sizes
******************************************************************************/

#define M68K_RAM_SIZE			0x10000		/* 64 KB - Main 68000 work RAM */
#define Z80_RAM_SIZE			0x10000		/* 64 KB - Z80 sound CPU RAM */

/******************************************************************************
	CPS1/CPS2 memory sizes
******************************************************************************/

/* Main RAM */
#define CPS1_RAM_SIZE			0x10000		/* 64 KB - CPS1 main RAM */
#define CPS2_RAM_SIZE			0x4000		/* 16 KB - CPS2 additional RAM */
#define CPS1_USER2_SIZE			0x8000		/* 32 KB - CPS1 user region 2 */

/* Graphics RAM */
#define CPS1_GFXRAM_SIZE		0x30000		/* 192 KB - CPS graphics RAM */
#define CPS1_OUTPUT_SIZE		0x100		/* 256 bytes - CPS1 output register */
#define CPS2_OBJRAM_SIZE		0x2000		/* 8 KB - CPS2 object RAM (per bank) */
#define CPS2_OUTPUT_SIZE		0x10		/* 16 bytes - CPS2 output register */

/* Scroll/sprite pen usage tables */
#define CPS1_PEN_USAGE_SIZE		0x10000		/* 64K entries - Pen usage table */

/* QSound shared RAM offsets */
#define QSOUND_SHAREDRAM1_OFFSET	0xc000
#define QSOUND_SHAREDRAM2_OFFSET	0xf000
#define QSOUND_SHAREDRAM_SIZE		0x1800	/* 6 KB - QSound shared RAM */

/******************************************************************************
	Neo Geo (MVS/NCDZ) memory sizes
******************************************************************************/

/* Main RAM */
#define NEOGEO_RAM_SIZE			0x10000		/* 64 KB - Neo Geo main RAM */
#define NEOGEO_SRAM_SIZE		0x8000		/* 32 KB - Battery-backed SRAM (16-bit) */
#define NEOGEO_MEMCARD_SIZE		0x800		/* 2 KB - Memory card */
#define NEOGEO_VECTORS_SIZE		0x80		/* 128 bytes - Game vectors */

/* Video RAM */
#define NEOGEO_VRAM_SIZE		0x20000		/* 128 KB - Video RAM */
#define NEOGEO_PALETTE_SIZE		0x2000		/* 8 KB - Palette RAM (per bank) */
#define NEOGEO_CLUT_SIZE		0x8000		/* 32 KB - Color lookup table */

/*
 * Neo Geo VRAM Layout (word addresses)
 * Reference: https://wiki.neogeodev.org/index.php?title=VRAM
 *
 * Lower VRAM (64KB):
 *   $0000-$6FFF: SCB1 - Sprite tilemaps and attributes (28KB)
 *   $7000-$74FF: Fix layer tilemap (40x32 tiles)
 *   $7500-$7FFF: Extension/bankswitching area
 *
 * Upper VRAM (4KB):
 *   $8000-$81FF: SCB2 - Sprite shrinking coefficients
 *   $8200-$83FF: SCB3 - Sprite Y position and size
 *   $8400-$85FF: SCB4 - Sprite X position
 *   $8600-$86FF: Sprite render list (active sprite evaluation)
 *   $8700-$87FF: Unused
 */
#define NEOGEO_VRAM_SCB1		0x0000		/* Sprite Control Block 1: tilemaps */
#define NEOGEO_VRAM_SCB1_END	0x6FFF
#define NEOGEO_VRAM_FIX			0x7000		/* Fix layer tilemap */
#define NEOGEO_VRAM_FIX_END		0x74FF
#define NEOGEO_VRAM_EXT			0x7500		/* Extension/bankswitching */
#define NEOGEO_VRAM_SCB2		0x8000		/* Sprite Control Block 2: shrink */
#define NEOGEO_VRAM_SCB3		0x8200		/* Sprite Control Block 3: Y pos/size */
#define NEOGEO_VRAM_SCB4		0x8400		/* Sprite Control Block 4: X position */
#define NEOGEO_VRAM_SPRLIST		0x8600		/* Sprite render list */

/* Sprite limits (hardware enforced) */
#define NEOGEO_SPR_MAX			0x3000		/* Maximum sprites */
#define NEOGEO_SPR_PER_FRAME	381			/* Max sprites rendered per frame */
#define NEOGEO_SPR_PER_LINE		96			/* Max sprites rendered per scanline */
#define CPS1_OBJ_MAX			0x1000		/* Maximum CPS1 objects */

/* Neo Geo palette system */
#define NEOGEO_PALETTE_BANKS	2			/* Two palette banks (switchable) */
#define NEOGEO_PALETTES_PER_BANK 256		/* 256 palettes per bank */
#define NEOGEO_COLORS_PER_PAL	16			/* 16 colors per palette (index 0 = transparent) */
#define NEOGEO_FIX_PALETTES		16			/* Fix layer can only use first 16 palettes */

/******************************************************************************
	Decryption buffer sizes (neocrypt.c)
******************************************************************************/

#define DECRYPT_BUFFER_128B		0x80		/* 128 bytes */
#define DECRYPT_BUFFER_128KB	0x20000		/* 128 KB */
#define DECRYPT_BUFFER_1MB		0x100000	/* 1 MB */
#define DECRYPT_BUFFER_2MB		0x200000	/* 2 MB */
#define DECRYPT_BUFFER_4MB		0x400000	/* 4 MB */
#define DECRYPT_BUFFER_5MB		0x500000	/* 5 MB */
#define DECRYPT_BUFFER_6MB		0x600000	/* 6 MB */
#define DECRYPT_BUFFER_8MB		0x800000	/* 8 MB */
#define DECRYPT_BUFFER_9MB		0x900000	/* 9 MB */

/******************************************************************************
	File I/O buffer sizes
******************************************************************************/

#define FILE_TEMP_BUFFER_SIZE	0x80000		/* 512 KB - Temporary file buffer */

/******************************************************************************
	Cache system sizes (defined in cache.c, duplicated here for reference)
******************************************************************************/

#define CACHE_BLOCK_SIZE		0x10000		/* 64 KB - Single cache block */
#define CACHE_SAFETY_MARGIN		0x20000		/* 128 KB - Minimum free after cache */

/******************************************************************************
	Address masks
******************************************************************************/

#define M68K_ADDR_MASK			0x00ffffff	/* 24-bit M68K address space */
#define Z80_ADDR_MASK			0x0000ffff	/* 16-bit Z80 address space */

#endif /* MEMORY_SIZES_H */
