/******************************************************************************

	hw_recorder.c

	Platform-independent recording of a work frame for GPU video backends.
	See hw_recorder.h.

******************************************************************************/

#include "common/hw_recorder.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

bool hw_rec_init(hw_recorder_t *rec, const hw_recorder_ops_t *ops, void *user,
				 int work_width, int work_height, const clut_info_t *clut_info)
{
	memset(rec, 0, sizeof(*rec));
	rec->ops = ops;
	rec->user = user;
	rec->work = (RECT){ 0, 0, (int16_t)work_width, (int16_t)work_height };
	rec->clip = rec->work;

	rec->clut_base = clut_info->base;
	rec->clut_entries = (uint32_t)clut_info->entries_per_bank * clut_info->bank_count;
	rec->clut_windows = (uint16_t)((rec->clut_entries + 255) / 256);
	if (rec->clut_windows) {
		rec->window_row = malloc(rec->clut_windows * sizeof(*rec->window_row));
		if (rec->window_row == NULL)
			return false;
	}
	hw_rec_reset(rec);
	return true;
}

void hw_rec_free(hw_recorder_t *rec)
{
	free(rec->window_row);
	free(rec->cmds);
	memset(rec, 0, sizeof(*rec));
}

static void hw_reset_clut_cache(hw_recorder_t *rec)
{
	for (uint16_t i = 0; i < rec->clut_windows; i++)
		rec->window_row[i] = HW_NO_ROW;
}

void hw_rec_reset(hw_recorder_t *rec)
{
	rec->vtx = NULL;
	rec->vtx_handle = NULL;
	rec->vtx_used = 0;
	rec->clut = NULL;
	rec->clut_handle = NULL;
	rec->clut_rows_used = 0;
	rec->cmd_count = 0;
	rec->batch.quads = 0;
	hw_reset_clut_cache(rec);
}

static hw_cmd_t *hw_push_cmd(hw_recorder_t *rec)
{
	if (rec->cmd_count == rec->cmd_capacity) {
		uint32_t capacity = rec->cmd_capacity ? rec->cmd_capacity * 2 : 256;
		hw_cmd_t *cmds = realloc(rec->cmds, capacity * sizeof(*cmds));
		if (cmds == NULL)
			return NULL;
		rec->cmds = cmds;
		rec->cmd_capacity = capacity;
	}
	return &rec->cmds[rec->cmd_count++];
}

void hw_rec_flush(hw_recorder_t *rec)
{
	hw_cmd_t *b = &rec->batch;

	if (b->quads == 0)
		return;

	hw_cmd_t *cmd = hw_push_cmd(rec);
	if (cmd != NULL)
		*cmd = *b;
	else
		rec->stat_dropped += b->quads;
	b->first_quad += b->quads;
	b->quads = 0;
}

/* Makes the pending draw compatible with the next quads, closing it if needed. */
static void hw_batch_begin(hw_recorder_t *rec, uint8_t prog, uint8_t depth,
						   const void *texture, uint16_t tex_w, uint16_t tex_h,
						   const void *clut)
{
	hw_cmd_t *b = &rec->batch;

	if (b->quads != 0 &&
		(b->prog != prog || b->depth != depth || b->texture != texture || b->clut != clut))
		hw_rec_flush(rec);

	if (b->quads == 0) {
		b->prog = prog;
		b->depth = depth;
		b->texture = texture;
		b->tex_w = tex_w;
		b->tex_h = tex_h;
		b->clut = clut;
		b->vertices = rec->vtx_handle;
		b->first_quad = rec->vtx_used;
	}
}

/* Ensures room for one more quad, opening a new vertex chunk if needed. */
static bool hw_reserve_quad(hw_recorder_t *rec)
{
	if (rec->vtx != NULL && rec->vtx_used < HW_CHUNK_QUADS)
		return true;

	/* The pending draw lives in the previous chunk: close it. */
	hw_rec_flush(rec);

	const void *handle = NULL;
	hw_vertex_t *vtx = rec->ops->alloc_vertices(rec->user, &handle);
	if (vtx == NULL) {
		rec->stat_dropped++;
		return false;
	}
	rec->vtx = vtx;
	rec->vtx_handle = handle;
	rec->vtx_used = 0;
	rec->batch.vertices = handle;
	rec->batch.first_quad = 0;
	return true;
}

static void hw_emit_quad(hw_recorder_t *rec, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
						 float u0, float v0, float u1, float v1, uint16_t z, uint16_t pal)
{
	hw_write_quad(rec->vtx + (size_t)rec->vtx_used * 4, x0, y0, x1, y1, u0, v0, u1, v1, z, pal);
	rec->vtx_used++;
	rec->batch.quads++;
}

static uint16_t *hw_clut_row_ptr(hw_recorder_t *rec, uint16_t row)
{
	return rec->clut + (size_t)row * HW_CLUT_ROW_ENTRIES;
}

/* Allocates a CLUT row, opening a new chunk (and closing the draw) if needed. */
static uint16_t hw_clut_alloc_row(hw_recorder_t *rec)
{
	if (rec->clut == NULL || rec->clut_rows_used == HW_CLUT_ROWS) {
		const void *handle = NULL;

		/* The pending draw samples the old chunk: close it before switching. */
		hw_rec_flush(rec);
		uint16_t *clut = rec->ops->alloc_clut(rec->user, &handle);
		if (clut == NULL)
			return HW_NO_ROW;
		rec->clut = clut;
		rec->clut_handle = handle;
		rec->clut_rows_used = 0;
		hw_reset_clut_cache(rec);
	}
	rec->stat_clut_rows++;
	return rec->clut_rows_used++;
}

/*
 * Returns the chunk row holding the current contents of the 256-entry CLUT
 * window `clut`.  A row is copied only when that window changed since it was
 * captured in this frame, so mid-frame palette writes stay correct while
 * static palettes cost one 512-byte compare per batch.
 */
static uint16_t hw_clut_row(hw_recorder_t *rec, const uint16_t *clut)
{
	uint16_t window = HW_NO_ROW;
	ptrdiff_t offset = clut - rec->clut_base;

	if (rec->clut_base != NULL && offset >= 0 &&
		(uint32_t)offset + HW_CLUT_ROW_ENTRIES <= rec->clut_entries && (offset & 0xff) == 0)
		window = (uint16_t)(offset >> 8);

	if (rec->clut != NULL && window != HW_NO_ROW) {
		uint16_t row = rec->window_row[window];
		if (row != HW_NO_ROW &&
			memcmp(hw_clut_row_ptr(rec, row), clut, HW_CLUT_ROW_ENTRIES * sizeof(uint16_t)) == 0)
			return row;
	}

	uint16_t row = hw_clut_alloc_row(rec);
	if (row == HW_NO_ROW)
		return HW_NO_ROW;
	memcpy(hw_clut_row_ptr(rec, row), clut, HW_CLUT_ROW_ENTRIES * sizeof(uint16_t));
	if (window != HW_NO_ROW)
		rec->window_row[window] = row;
	return row;
}

void hw_rec_set_clip(hw_recorder_t *rec, int left, int top, int right, int bottom)
{
	/* Quads are clipped on the CPU: no GPU state change, no new draw. */
	rec->clip.left = (int16_t)(left < rec->work.left ? rec->work.left : left);
	rec->clip.top = (int16_t)(top < rec->work.top ? rec->work.top : top);
	rec->clip.right = (int16_t)(right > rec->work.right ? rec->work.right : right);
	rec->clip.bottom = (int16_t)(bottom > rec->work.bottom ? rec->work.bottom : bottom);
}

void hw_rec_set_depth_test(hw_recorder_t *rec, bool enable)
{
	rec->depth_test = enable;
}

void hw_rec_fill(hw_recorder_t *rec, const RECT *rect, uint16_t color555, uint8_t depth)
{
	if (rect->right <= rect->left || rect->bottom <= rect->top)
		return;

	uint16_t row = hw_clut_alloc_row(rec);
	if (row == HW_NO_ROW)
		return;
	hw_clut_row_ptr(rec, row)[0] = color555;

	hw_batch_begin(rec, HW_PROG_DIRECT, depth, NULL, HW_CLUT_ROW_ENTRIES, HW_CLUT_ROWS,
		rec->clut_handle);
	if (!hw_reserve_quad(rec))
		return;
	const float tv = row + 0.5f;
	hw_emit_quad(rec, rect->left, rect->top, rect->right, rect->bottom,
		0.5f, tv, 0.5f, tv, 0, 0);
}

void hw_rec_begin_work(hw_recorder_t *rec, uint16_t bg555)
{
	rec->batch.quads = 0;
	rec->cmd_count = 0;
	rec->clip = rec->work;
	hw_rec_fill(rec, &rec->work, bg555, HW_DEPTH_OFF);
}

void hw_rec_blit(hw_recorder_t *rec, const hw_layer_t *layer, const uint16_t *clut,
				 const video_sprite_vertex_t *vertices, uint32_t count)
{
	const RECT *c = &rec->clip;
	uint16_t pal = 0;

	if (vertices == NULL || count < 2 || c->right <= c->left || c->bottom <= c->top)
		return;

	if (layer->indexed) {
		if (clut == NULL)
			return;
		pal = hw_clut_row(rec, clut);
		if (pal == HW_NO_ROW)
			return;
		if (rec->depth_test)
			hw_batch_begin(rec, HW_PROG_INDEXED_DEPTH, HW_DEPTH_TEST, layer->texture,
				layer->tex_width, layer->tex_height, rec->clut_handle);
		else
			hw_batch_begin(rec, HW_PROG_INDEXED, HW_DEPTH_OFF, layer->texture,
				layer->tex_width, layer->tex_height, rec->clut_handle);
	} else {
		hw_batch_begin(rec, HW_PROG_DIRECT, HW_DEPTH_OFF, layer->texture,
			layer->tex_width, layer->tex_height, NULL);
	}

	const float row_offset = layer->row_offset;
	const video_sprite_vertex_t *v = vertices;

	for (uint32_t i = 0; i + 1 < count; i += 2, v += 2) {
		int x0 = v[0].x, y0 = v[0].y, x1 = v[1].x, y1 = v[1].y;
		float u0 = v[0].u, v0 = v[0].v + row_offset;
		float u1 = v[1].u, v1 = v[1].v + row_offset;

		if (!hw_clip_quad(c, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1))
			continue;
		if (!hw_reserve_quad(rec))
			break;
		hw_emit_quad(rec, x0, y0, x1, y1, u0, v0, u1, v1, v[0].z, pal);
	}
}

void hw_rec_points(hw_recorder_t *rec, const video_point_vertex_t *points, uint32_t count)
{
	const RECT *c = &rec->clip;

	if (points == NULL || count == 0)
		return;

	/*
	 * Points (CPS1 stars) are single 555 pixels: write their colours into CLUT
	 * rows and draw each as a 1x1 quad sampling its own texel.
	 */
	for (uint32_t i = 0; i < count;) {
		uint16_t row = hw_clut_alloc_row(rec);
		if (row == HW_NO_ROW)
			return;
		uint16_t *colors = hw_clut_row_ptr(rec, row);

		hw_batch_begin(rec, HW_PROG_DIRECT, HW_DEPTH_OFF, NULL, HW_CLUT_ROW_ENTRIES,
			HW_CLUT_ROWS, rec->clut_handle);

		for (uint32_t col = 0; col < HW_CLUT_ROW_ENTRIES && i < count; i++) {
			int x = points[i].x, y = points[i].y;
			if (x < c->left || x >= c->right || y < c->top || y >= c->bottom)
				continue;
			if (!hw_reserve_quad(rec))
				return;

			/* Points are always opaque: clear the transparency bit. */
			colors[col] = points[i].color & 0x7fff;
			const float u = col + 0.5f, tv = row + 0.5f;
			hw_emit_quad(rec, x, y, x + 1, y + 1, u, tv, u, tv, 0, 0);
			col++;
		}
	}
}
