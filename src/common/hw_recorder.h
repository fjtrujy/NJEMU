/******************************************************************************

	hw_recorder.h

	Platform-independent recording of a work frame for GPU video backends.

	While the emulation core draws, the recorder turns video_driver calls into
	a list of draw commands whose vertices and palette snapshots already live
	in backend-provided memory:

	- sprites are expanded to quads and clipped against the scissor on the CPU
	  (no GPU state per scissor change);
	- CLUT windows are snapshotted as raw 555 rows, copied only when a
	  256-entry window changed, and selected per vertex, so palette changes
	  do not split draws;
	- clears are recorded as quads, so a frame is a single ordered list.

	The backend replays the list once it knows where and how the frame is
	presented (see hw_render.h), typically straight into its back buffer.

******************************************************************************/

#ifndef HW_RECORDER_H
#define HW_RECORDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/video_driver.h"
#include "common/hw_render.h"

#define HW_CHUNK_QUADS		4096		/* quads per vertex allocation */
#define HW_CLUT_ROWS		256			/* rows per CLUT chunk (one texture) */
#define HW_CLUT_ROW_ENTRIES	256
#define HW_CLUT_CHUNK_BYTES	(HW_CLUT_ROWS * HW_CLUT_ROW_ENTRIES * sizeof(uint16_t))
#define HW_NO_ROW			0xffff

enum {
	HW_PROG_INDEXED,			/* U8 atlas + CLUT, inverted-alpha blend */
	HW_PROG_INDEXED_DEPTH,		/* U8 atlas + CLUT, discard + GEQUAL depth */
	HW_PROG_DIRECT,				/* 555 texture, inverted-alpha blend */
	HW_PROG_COUNT
};

enum {
	HW_DEPTH_OFF,				/* no test, no write */
	HW_DEPTH_TEST,				/* GEQUAL test and write (CPS2 masks) */
	HW_DEPTH_WRITE,				/* write only (depth clear) */
};

/* Backend handles are opaque; NULL means "none". */
typedef struct hw_cmd {
	uint8_t prog;
	uint8_t depth;
	uint16_t tex_w;
	uint16_t tex_h;
	const void *texture;		/* NULL: sample the CLUT chunk itself */
	const void *vertices;		/* vertex chunk holding first_quad.. */
	const void *clut;			/* CLUT chunk bound as the palette */
	uint32_t first_quad;
	uint32_t quads;
} hw_cmd_t;

/* A texture layer as seen by the recorder. */
typedef struct hw_layer {
	const void *texture;
	uint16_t tex_width;
	uint16_t tex_height;
	uint16_t row_offset;		/* first row of the layer inside the texture */
	bool indexed;
} hw_layer_t;

typedef struct hw_recorder_ops {
	/* Storage for HW_CHUNK_QUADS * 4 vertices, valid until hw_rec_reset(). */
	hw_vertex_t *(*alloc_vertices)(void *user, const void **handle);
	/* Storage for one HW_CLUT_ROWS x 256 chunk of 555 texels. */
	uint16_t *(*alloc_clut)(void *user, const void **handle);
} hw_recorder_ops_t;

typedef struct hw_recorder {
	const hw_recorder_ops_t *ops;
	void *user;
	RECT work;

	/* Palette windows */
	const uint16_t *clut_base;
	uint32_t clut_entries;
	uint16_t clut_windows;
	uint16_t *window_row;		/* snapshot row per 256-entry window */
	uint16_t *clut;				/* chunk receiving rows */
	const void *clut_handle;
	uint16_t clut_rows_used;

	/* Vertices */
	hw_vertex_t *vtx;
	const void *vtx_handle;
	uint32_t vtx_used;

	/* Recording */
	hw_cmd_t *cmds;
	uint32_t cmd_count;
	uint32_t cmd_capacity;
	hw_cmd_t batch;
	RECT clip;
	bool depth_test;

	/* Statistics */
	uint32_t stat_clut_rows;
	uint32_t stat_dropped;
} hw_recorder_t;

bool hw_rec_init(hw_recorder_t *rec, const hw_recorder_ops_t *ops, void *user,
				 int work_width, int work_height, const clut_info_t *clut_info);
void hw_rec_free(hw_recorder_t *rec);

/* Forgets every allocation: call once the backend recycles its frame memory. */
void hw_rec_reset(hw_recorder_t *rec);

/* Starts a work frame: drops recorded commands and fills it with `bg555`. */
void hw_rec_begin_work(hw_recorder_t *rec, uint16_t bg555);

void hw_rec_set_clip(hw_recorder_t *rec, int left, int top, int right, int bottom);
void hw_rec_set_depth_test(hw_recorder_t *rec, bool enable);

void hw_rec_blit(hw_recorder_t *rec, const hw_layer_t *layer, const uint16_t *clut,
				 const video_sprite_vertex_t *vertices, uint32_t count);
void hw_rec_points(hw_recorder_t *rec, const video_point_vertex_t *points, uint32_t count);

/* Fills a rectangle (work frame pixels) with a 555 colour; 0x8000 keeps the
 * colour untouched, which together with HW_DEPTH_WRITE clears depth only. */
void hw_rec_fill(hw_recorder_t *rec, const RECT *rect, uint16_t color555, uint8_t depth);

/* Closes the draw being accumulated; call before replaying rec->cmds. */
void hw_rec_flush(hw_recorder_t *rec);

static inline uint16_t hw_rgba_to_555(uint32_t c)
{
	return (uint16_t)(((c >> 3) & 0x1f) | (((c >> 11) & 0x1f) << 5) | (((c >> 19) & 0x1f) << 10));
}

#endif /* HW_RECORDER_H */
