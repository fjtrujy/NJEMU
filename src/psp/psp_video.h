/******************************************************************************

	video.c

	PSP�r�f�I����֐�

******************************************************************************/

#ifndef PSP_VIDEO_H
#define PSP_VIDEO_H

#include <stddef.h>
#include <stdint.h>
#include <pspgu.h>
#include "emucfg.h"
#include "common/video_driver.h"
#include "common/video_geometry.h"

#define	FRAMESIZE			(BUF_WIDTH * SCR_HEIGHT * sizeof(uint16_t))
#define	FRAMESIZE32			(BUF_WIDTH * SCR_HEIGHT * sizeof(uint32_t))

#define SLICE_SIZE			64 // change this to experiment with different page-cache sizes
#define TEXTURE_FLAGS		(GU_TEXTURE_16BIT | GU_COLOR_5551 | GU_VERTEX_16BIT | GU_TRANSFORM_2D)
#define PRIMITIVE_FLAGS		(GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D)

/* The compact static UI atlas occupies the final 16 EDRAM rows.  Game video
 * allocations must stop before this boundary. */
#define PSP_UI_STATIC_EDRAM_ROW	2032
#define PSP_UI_STATIC_EDRAM_OFFSET \
	((size_t)BUF_WIDTH * PSP_UI_STATIC_EDRAM_ROW * sizeof(uint16_t))

extern uint8_t gulist[GULIST_SIZE];

/* The proportional UI font uses one mutable scratch texture.  Flush the
 * current GU commands before common/ui_draw.c rewrites that scratch for the
 * next glyph, while keeping ownership of the surrounding logical frame. */
void psp_video_sync_ui_scratch(void *video_data);

#endif /* PSP_VIDE_H */
