/******************************************************************************

	hw_render.h

	Geometry shared by the GPU video backends (PS Vita vitaGL and GXM, Desktop
	OpenGL): the vertex layout of the shared shaders, CPU scissor clipping and
	the work frame -> canvas transform used when presenting.

******************************************************************************/

#ifndef HW_RENDER_H
#define HW_RENDER_H

#include <stdbool.h>
#include <stdint.h>
#include "common/video_driver.h"

/* Vertex layout consumed by the hardware renderers' shared vertex shader. */
typedef struct hw_vertex {
	float u, v;					/* texel coordinates */
	int16_t x, y;				/* work frame (or display) pixel coordinates */
	uint16_t z;					/* CPS2 priority depth (0..65535) */
	uint16_t pal;				/* CLUT row within the bound chunk */
} hw_vertex_t;

enum {
	HW_ORIENT_NORMAL,
	HW_ORIENT_FLIP,			/* 180 degrees (cocktail flip) */
	HW_ORIENT_ROTATE,		/* CPS vertical raster -> upright presentation */
	HW_ORIENT_ROTATE_FLIP,
};

/* Work frame pixel -> display pixel: X = ax*x + bx*y + cx, Y = ay*x + by*y + cy. */
typedef struct hw_xform {
	float ax, bx, cx;
	float ay, by, cy;
} hw_xform_t;

/*
 * Clips an axis-aligned textured quad against `c`, keeping texels attached
 * to pixels (flipped sprites simply carry reversed texture coordinates).
 * Returns false when nothing is left.
 */
static inline bool hw_clip_quad(const RECT *c, int *x0, int *y0, int *x1, int *y1,
									float *u0, float *v0, float *u1, float *v1)
{
	if (*x1 <= *x0 || *y1 <= *y0)
		return false;
	if (*x1 <= c->left || *x0 >= c->right || *y1 <= c->top || *y0 >= c->bottom)
		return false;

	if (*x0 < c->left) {
		*u0 += (*u1 - *u0) * (float)(c->left - *x0) / (float)(*x1 - *x0);
		*x0 = c->left;
	}
	if (*x1 > c->right) {
		*u1 -= (*u1 - *u0) * (float)(*x1 - c->right) / (float)(*x1 - *x0);
		*x1 = c->right;
	}
	if (*y0 < c->top) {
		*v0 += (*v1 - *v0) * (float)(c->top - *y0) / (float)(*y1 - *y0);
		*y0 = c->top;
	}
	if (*y1 > c->bottom) {
		*v1 -= (*v1 - *v0) * (float)(*y1 - c->bottom) / (float)(*y1 - *y0);
		*y1 = c->bottom;
	}
	return true;
}

static inline void hw_write_quad(hw_vertex_t *q,
									 int16_t x0, int16_t y0, int16_t x1, int16_t y1,
									 float u0, float v0, float u1, float v1,
									 uint16_t z, uint16_t pal)
{
	q[0] = (hw_vertex_t){ u0, v0, x0, y0, z, pal };
	q[1] = (hw_vertex_t){ u1, v0, x1, y0, z, pal };
	q[2] = (hw_vertex_t){ u1, v1, x1, y1, z, pal };
	q[3] = (hw_vertex_t){ u0, v1, x0, y1, z, pal };
}

/*
 * Transform mapping the work frame rectangle `src` onto `dst`, a rectangle of
 * the canvas (the physical output reported by getOutputSize(), where the target
 * renderers place the frame), and the destination rectangle itself in `out`.
 * Returns false for empty rectangles.
 */
static inline bool hw_present_geometry(const RECT *src, const RECT *dst, int orient,
									   int canvas_w, int canvas_h,
									   RECT *out, hw_xform_t *m)
{
	const float sl = src->left, sr = src->right, st = src->top, sb = src->bottom;
	const int src_w = src->right - src->left;
	const int src_h = src->bottom - src->top;
	const int dx = dst->left, dy = dst->top;
	const int dw = dst->right - dst->left;
	const int dh = dst->bottom - dst->top;
	(void)canvas_w;
	(void)canvas_h;

	if (src_w <= 0 || src_h <= 0 || dw <= 0 || dh <= 0)
		return false;

	switch (orient) {
	case HW_ORIENT_FLIP:
		*m = (hw_xform_t){ -dw / (float)src_w, 0.0f, dx + sr * dw / src_w,
		                       0.0f, -dh / (float)src_h, dy + sb * dh / src_h };
		break;
	case HW_ORIENT_ROTATE:
		/* Dest TL <- src TR, TR <- BR, BL <- TL, BR <- BL. */
		*m = (hw_xform_t){ 0.0f, dw / (float)src_h, dx - st * dw / src_h,
		                       -dh / (float)src_w, 0.0f, dy + sr * dh / src_w };
		break;
	case HW_ORIENT_ROTATE_FLIP:
		*m = (hw_xform_t){ 0.0f, -dw / (float)src_h, dx + sb * dw / src_h,
		                       dh / (float)src_w, 0.0f, dy - sl * dh / src_w };
		break;
	default:
		*m = (hw_xform_t){ dw / (float)src_w, 0.0f, dx - sl * dw / src_w,
		                       0.0f, dh / (float)src_h, dy - st * dh / src_h };
		break;
	}

	*out = (RECT){ dx, dy, dx + dw, dy + dh };
	return true;
}

/*
 * Sharp bilinear presentation.  Replaying the work frame straight onto a
 * destination that is not a whole multiple of it puts pixel centres next to
 * texel edges of the nearest-sampled atlas, and low-precision GPUs (the
 * Vita's) round them onto the neighbouring texel: vertical seams between
 * sprite strips.  Instead the frame is replayed at the largest whole scale
 * that fits into an off-screen target, which is then filtered bilinearly onto
 * the destination; the fractional rest of the scale is spread evenly rather
 * than alternating column widths.
 *
 * `out_w` x `out_h` is the destination in physical pixels.  Returns false
 * when it already is a whole multiple of `src`, where the direct replay is
 * exact, unless `force` (a presentation filter samples the target anyway);
 * otherwise the target size, at most `max_dim` on each side.
 */
static inline bool hw_sharp_target(const RECT *src, int out_w, int out_h, int orient,
								   bool force, int max_dim, int *target_w, int *target_h)
{
	const int sw = src->right - src->left;
	const int sh = src->bottom - src->top;
	const bool rotated = orient == HW_ORIENT_ROTATE || orient == HW_ORIENT_ROTATE_FLIP;
	const int dw = rotated ? out_h : out_w;
	const int dh = rotated ? out_w : out_h;
	int scale;

	if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
		return false;
	scale = dw / sw < dh / sh ? dw / sw : dh / sh;
	if (scale < 1)
		scale = 1;
	if (!force && dw == sw * scale && dh == sh * scale)
		return false;
	while (scale > 1 && (sw * scale > max_dim || sh * scale > max_dim))
		scale--;
	*target_w = sw * scale;
	*target_h = sh * scale;
	return true;
}

static inline void hw_map_point(const hw_xform_t *m, float x, float y,
									float *X, float *Y)
{
	*X = m->ax * x + m->bx * y + m->cx;
	*Y = m->ay * x + m->by * y + m->cy;
}

/* Folds canvas pixels -> clip space into the vertex shader rows uRowX/uRowY. */
static inline void hw_clip_rows(const hw_xform_t *m, int canvas_w, int canvas_h,
								float row_x[3], float row_y[3])
{
	const float kx = 2.0f / canvas_w, ky = -2.0f / canvas_h;
	row_x[0] = m->ax * kx;
	row_x[1] = m->bx * kx;
	row_x[2] = m->cx * kx - 1.0f;
	row_y[0] = m->ay * ky;
	row_y[1] = m->by * ky;
	row_y[2] = m->cy * ky + 1.0f;
}

#endif /* HW_RENDER_H */
