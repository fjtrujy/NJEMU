/******************************************************************************

	psvita_video_common.h

	Helpers shared by the PS Vita video backends (vitaGL and vita2d/GXM):
	render statistics logging.  The geometry lives in common/hw_render.h.

******************************************************************************/

#ifndef PSVITA_VIDEO_COMMON_H
#define PSVITA_VIDEO_COMMON_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>

#include "common/hw_render.h"

/* The Vita display: the canvas the frames are presented on. */
#define PSVITA_DISPLAY_WIDTH	960
#define PSVITA_DISPLAY_HEIGHT	544

/*
 * Copies a rectangle of texels into a linear texture whose rows are `tex_w`
 * texels apart (writeIndexedTextureRect/writeDirectTextureRect). `src_pitch` is
 * in texels. The rectangle is clipped to the texture.
 */
static inline void psvita_write_texture_rect(uint8_t *tex, int tex_w, int tex_h, int bpp,
	int x, int y, int width, int height, const void *pixels, int src_pitch)
{
	const uint8_t *src = pixels;

	if (tex == NULL || pixels == NULL || x < 0 || y < 0 || width <= 0 || height <= 0)
		return;
	if (x + width > tex_w)
		width = tex_w - x;
	if (y + height > tex_h)
		height = tex_h - y;
	for (int row = 0; row < height; row++)
		memcpy(tex + ((size_t)(y + row) * tex_w + x) * bpp,
			src + (size_t)row * src_pitch * bpp, (size_t)width * bpp);
}

/*
 * Render statistics (scenes, draws, quads, CLUT rows per frame) are appended
 * to PSVITA_VIDEO_LOG_PATH every PSVITA_VIDEO_STATS_FRAMES presented frames
 * when built with PSVITA_VIDEO_STATS=1 (CMake option of the same name).
 */
#ifndef PSVITA_VIDEO_STATS
#define PSVITA_VIDEO_STATS			0
#endif
#define PSVITA_VIDEO_STATS_FRAMES	300
#define PSVITA_VIDEO_LOG_PATH		"ux0:data/njemu_video.log"

__attribute__((unused, format(printf, 1, 2)))
static void psvita_video_log(const char *fmt, ...)
{
	char line[256];
	va_list args;

	va_start(args, fmt);
	int len = vsnprintf(line, sizeof(line), fmt, args);
	va_end(args);
	if (len <= 0)
		return;
	if (len >= (int)sizeof(line))
		len = sizeof(line) - 1;

	sceClibPrintf("psvita_video: %s", line);
	SceUID fd = sceIoOpen(PSVITA_VIDEO_LOG_PATH, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
	if (fd >= 0) {
		sceIoWrite(fd, line, len);
		sceIoClose(fd);
	}
}

/*
 * Frame dumps for comparing the Vita output with the Desktop test bench
 * (tools/compare_frames.py): list presented frame numbers, comma separated,
 * in PSVITA_DUMP_LIST.  The displayed frame is sampled back at the work frame
 * resolution through the present transform and written as a PPM.
 */
#define PSVITA_DUMP_LIST	"ux0:data/njemu_dump_frames.txt"
#define PSVITA_DUMP_DIR		"ux0:data/njemu_dumps"

__attribute__((unused))
static bool psvita_dump_wanted(uint32_t frame)
{
	static bool parsed;
	static uint32_t frames[64];
	static int count;

	if (!parsed) {
		char list[512];
		parsed = true;
		SceUID fd = sceIoOpen(PSVITA_DUMP_LIST, SCE_O_RDONLY, 0);
		if (fd < 0)
			return false;
		int len = sceIoRead(fd, list, sizeof(list) - 1);
		sceIoClose(fd);
		list[len > 0 ? len : 0] = '\0';
		for (char *p = list; *p != '\0' && count < 64;) {
			char *end;
			unsigned long f = strtoul(p, &end, 10);
			if (end == p) {
				p++;
				continue;
			}
			if (f != 0)
				frames[count++] = (uint32_t)f;
			p = end;
		}
	}
	for (int i = 0; i < count; i++) {
		if (frames[i] == frame)
			return true;
	}
	return false;
}

/* `display` is RGBA8 with `stride` pixels per row; `bottom_up` for GL reads. */
__attribute__((unused))
static void psvita_dump_frame(const char *backend, uint32_t frame, const uint8_t *display,
							  int stride, bool bottom_up, const RECT *src, const hw_xform_t *m)
{
	const int w = src->right - src->left, h = src->bottom - src->top;
	uint8_t *rgb = malloc((size_t)w * h * 3);
	char path[128];

	if (rgb == NULL)
		return;
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			float X, Y;
			hw_map_point(m, src->left + x + 0.5f, src->top + y + 0.5f, &X, &Y);
			int px = (int)X, py = (int)Y;
			if (px < 0) px = 0;
			if (py < 0) py = 0;
			if (px >= PSVITA_DISPLAY_WIDTH) px = PSVITA_DISPLAY_WIDTH - 1;
			if (py >= PSVITA_DISPLAY_HEIGHT) py = PSVITA_DISPLAY_HEIGHT - 1;
			if (bottom_up)
				py = PSVITA_DISPLAY_HEIGHT - 1 - py;
			memcpy(rgb + ((size_t)y * w + x) * 3, display + ((size_t)py * stride + px) * 4, 3);
		}
	}

	sceIoMkdir(PSVITA_DUMP_DIR, 0777);
	snprintf(path, sizeof(path), "%s/%s_%05u.ppm", PSVITA_DUMP_DIR, backend, (unsigned)frame);
	SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd >= 0) {
		char header[32];
		int len = snprintf(header, sizeof(header), "P6\n%d %d\n255\n", w, h);
		sceIoWrite(fd, header, len);
		sceIoWrite(fd, rgb, (SceSize)w * h * 3);
		sceIoClose(fd);
		psvita_video_log("dumped %s\n", path);
	}
	free(rgb);
}

#endif /* PSVITA_VIDEO_COMMON_H */
