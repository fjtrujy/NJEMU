/******************************************************************************

    psvita_video_gl.c

    PS Vita video backend built on vitaGL.

    Design goals:

    - One GXM scene per frame.  The emulated frame is never rendered into an
      intermediate framebuffer: while the core draws, the driver only records
      commands (draws whose vertices are already in GPU memory, and clears).
      transferWorkFrame() knows the destination rectangle and orientation, so
      it replays the recording straight into the display back buffer with an
      affine transform in the vertex shader (scale, 180 degree flip or
      rotation).  No FBO scene, no presentation pass.

    - Texture data in place.  Every indexed (1 byte per pixel) layer lives
      inside a vitaGL U8 texture, and writeIndexedTextureRect() copies the
      tiles missed by the sprite cache straight into that GPU-visible memory
      (no upload pass).  Layers of the same width
      are stacked into a single "page" texture, so sprites taken from different
      layers can still share a draw call.  Direct-colour layers (CPS1 SCROLLH)
      use a native U1U5U5U5 texture that matches NJEMU's 555 pixel layout.

    - Native palette format.  CLUT windows are snapshotted as raw 16-bit 555
      rows into vitaGL's scratch pool and sampled as a U1U5U5U5 texture from
      the fragment shader.  No 32-bit palette expansion; a row is only copied
      when its 256-entry window actually changed.  The palette row is a vertex
      attribute, so draws with different CLUT windows are merged.

    - One draw per state change, not per sprite.  Vertices are expanded and
      clipped against the scissor rectangle on the CPU, straight into a mapped
      stream VBO backed by vitaGL's scratch pool.

    NJEMU uses bit 15 of a 555 colour as the "transparent" marker, i.e. the
    opposite of conventional alpha.  Rather than rewriting colours, the
    blending equation is inverted (dst = src * (1 - a) + dst * a), and the
    depth-tested CPS2 path discards instead of blending.

    The GUI draws straight into the same display scene with the direct
    program and conventional blending: sprites from the shared UI atlas (see
    psvita_video_common.h), colour fills from 2x2 textures in the scratch pool
    whose bilinear filtering reproduces the per-corner gradient colours.

******************************************************************************/

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/display.h>
#include <psp2/gxm.h>
#include <vitaGL.h>

#include "common/hw_recorder.h"
#include "common/ui_draw_driver.h"
#include "common/video_driver.h"
#include "common/video_geometry.h"
#include "psvita_shaders.h"
#include "psvita_video_common.h"

#ifndef PSVITA_GL_SHADER_SOURCE
#define PSVITA_GL_SHADER_SOURCE	0
#endif

/* Mid-frame scene splits counted by the vitaGL fork; absent upstream. */
extern uint32_t vgl_debug_scene_splits __attribute__((weak));

/* vitaGL's display buffers (globals of its gxm.c), for SHOW_FRAME_BUFFER. */
extern void *gxm_color_surfaces_addr[] __attribute__((weak));
extern unsigned int gxm_front_buffer_index __attribute__((weak));
extern int DISPLAY_STRIDE __attribute__((weak));

#define GL_WORK_WIDTH		SCR_WIDTH
#define GL_WORK_HEIGHT		SCR_HEIGHT

#define GL_MAX_TEXTURE_DIM	4096
#define GL_MAX_PAGES		4

/* vitaGL expands GL_QUADS through a static index buffer of 0xC000 indices. */
#define GL_DRAW_MAX_QUADS	8192

/*
 * Transient GPU data (vertices and CLUT snapshots) lives in vitaGL's
 * per-frame circular "scratch" pool, which vitaGL recycles in step with its
 * own frame pacing.  Vertices go through GL_STREAM_DRAW VBOs: with
 * USE_SCRATCH_MEMORY, glBufferData() is a bump allocation in that pool and
 * glMapBuffer() returns it for direct writes; without it, vitaGL allocates
 * and garbage-collects the storage, which is equally safe.  Each chunk keeps
 * its own VBO because the recording is replayed after all chunks are filled.
 */
#define GL_MAX_CHUNKS		32
#define GL_SCRATCH_ALIGN	256			/* GXM texture data alignment, rounded up */

/* GUI */
#define GL_UI_CHUNK_QUADS	1024		/* UI quads per stream VBO allocation */
#define GL_UI_FILL_BYTES	64			/* one 2x2 RGBA texture: 2 rows of 8 texels... */
#define GL_UI_FILL_CHUNK	16384		/* ...allocated from the scratch pool in chunks */
#define GL_UI_ATLAS_WIDTH	1024
#define GL_UI_ATLAS_HEIGHT	512
#define GL_SCRATCH_WIDTH	BUF_WIDTH	/* INITIAL_TEXTURE_LAYER */
#define GL_SCRATCH_HEIGHT	SCR_HEIGHT

typedef struct gl_page {
	GLuint tex;
	uint8_t *data;
	uint16_t width;
	uint16_t height;
} gl_page_t;

typedef struct gl_layer {
	uint8_t *cpu;				/* pointer handed to the core */
	GLuint tex;
	uint16_t width;
	uint16_t height;
	hw_layer_t hw;				/* texture handle: the GL name */
	uint8_t bytes_per_pixel;
} gl_layer_t;

typedef struct gl_program {
	GLuint id;
	GLint u_row_x;
	GLint u_row_y;
	GLint u_tex_scale;
	GLint a_uv;
	GLint a_pos;
	GLint a_zp;
} gl_program_t;

typedef struct psvita_gl_video {
	gl_layer_t *layers;
	uint8_t layer_count;
	gl_page_t pages[GL_MAX_PAGES];
	uint8_t page_count;

	/* Transient memory (vitaGL scratch pool) */
	GLuint clut_tex;
	GLuint vbos[GL_MAX_CHUNKS];
	uint8_t chunks_used;
	uint32_t frame;
	bool frame_valid;
	bool alloc_failure_logged;

	hw_recorder_t rec;
	gl_program_t progs[HW_PROG_COUNT];
	bool pending_flip;
	uint32_t presented;			/* for PSVITA_DUMP_LIST */
	hw_xform_t last_present_xform;
	bool last_present_valid;

	/*
	 * The display scene: cleared to black by the first draw of a frame, and
	 * only swapped by flipScreen() when something was drawn.
	 */
	bool display_open;
	bool ui_blend;				/* conventional blending set (UI), not NJEMU's */
	RECT ui_clip;				/* setUIScissor(), display pixels */

	/* DRAW / SCREEN_BITMAP as solid fills (menu backgrounds, see copyRect). */
	uint32_t draw_fill;
	uint32_t screen_fill;
	bool screen_fill_valid;

	GLuint ui_vbo;
	hw_vertex_t *ui_vtx;		/* current chunk, NULL at frame start */
	uint32_t ui_quads;
	uint8_t *fill_chunk;		/* current chunk, NULL at frame start */
	uint32_t fill_used;
	GLuint fill_tex;			/* 2x2 RGBA, data set per draw */
	GLuint atlas_tex;
	psvita_ui_atlas_t *atlas;
	GLuint scratch_tex;
	uint16_t *scratch;			/* INITIAL_TEXTURE_LAYER texels (PSP 5551) */
	GLuint frame_tex;			/* points at the displayed frame when drawn */

	/* Statistics */
	uint32_t stat_frames;
	uint32_t stat_draws;
	uint32_t stat_quads;
	uint32_t stat_splits_base;
} psvita_gl_video_t;

#define gl_log psvita_video_log


/******************************************************************************
	Shaders (see psvita_shaders.h)
******************************************************************************/

/*
 * Loads a precompiled GXP (no libshacccg needed).  Building with
 * PSVITA_GL_SHADER_SOURCE=1 compiles the Cg sources instead, which is how
 * the embedded binaries are regenerated through vitaGL's shader cache.
 */
static GLuint gl_compile(GLenum type, const char *source,
						 const uint8_t *gxp, size_t gxp_size)
{
	GLuint shader = glCreateShader(type);

#if PSVITA_GL_SHADER_SOURCE
	GLint length = (GLint)strlen(source);
	GLint ok = GL_FALSE;
	(void)gxp;
	(void)gxp_size;

	glShaderSource(shader, 1, &source, &length);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		gl_log("shader compilation failed: %s\n", log);
		glDeleteShader(shader);
		return 0;
	}
#else
	/* vitaGL's binary format: matrix uniform count (0), then the GXP. */
	uint8_t *blob = malloc(sizeof(uint32_t) + gxp_size);
	(void)source;
	if (blob == NULL) {
		glDeleteShader(shader);
		return 0;
	}
	memset(blob, 0, sizeof(uint32_t));
	memcpy(blob + sizeof(uint32_t), gxp, gxp_size);
	glShaderBinary(1, &shader, 0, blob, (GLsizei)(sizeof(uint32_t) + gxp_size));
	free(blob);
#endif
	return shader;
}

static bool gl_link(gl_program_t *prog, GLuint vs, const char *fragment_src,
					const uint8_t *gxp, size_t gxp_size)
{
	GLuint fs = gl_compile(GL_CG_FRAGMENT_SHADER_EXT, fragment_src, gxp, gxp_size);
	GLint ok = GL_FALSE;

	if (fs == 0)
		return false;

	prog->id = glCreateProgram();
	glAttachShader(prog->id, vs);
	glAttachShader(prog->id, fs);
	glLinkProgram(prog->id);
	glGetProgramiv(prog->id, GL_LINK_STATUS, &ok);
	if (!ok) {
		gl_log("program link failed\n");
		return false;
	}

	prog->u_row_x = glGetUniformLocation(prog->id, "uRowX");
	prog->u_row_y = glGetUniformLocation(prog->id, "uRowY");
	prog->u_tex_scale = glGetUniformLocation(prog->id, "uTexScale");
	prog->a_uv = glGetAttribLocation(prog->id, "aUV");
	prog->a_pos = glGetAttribLocation(prog->id, "aPos");
	prog->a_zp = glGetAttribLocation(prog->id, "aZP");

	glUseProgram(prog->id);
	glUniform1i(glGetUniformLocation(prog->id, "uTex"), 0);
	GLint u_clut = glGetUniformLocation(prog->id, "uClut");
	if (u_clut >= 0)
		glUniform1i(u_clut, 1);
	return true;
}

static bool gl_create_programs(psvita_gl_video_t *gl)
{
	static const struct {
		const char *cg;
		const uint8_t *gxp;
		size_t gxp_size;
	} fragments[HW_PROG_COUNT] = {
		{ psvita_shader_indexed_cg, psvita_shader_indexed_gxp, sizeof(psvita_shader_indexed_gxp) },
		{ psvita_shader_indexed_depth_cg, psvita_shader_indexed_depth_gxp, sizeof(psvita_shader_indexed_depth_gxp) },
		{ psvita_shader_direct_cg, psvita_shader_direct_gxp, sizeof(psvita_shader_direct_gxp) },
	};
	GLuint vs = gl_compile(GL_CG_VERTEX_SHADER_EXT, psvita_shader_vertex_cg,
		psvita_shader_vertex_gxp, sizeof(psvita_shader_vertex_gxp));

	if (vs == 0)
		return false;

	for (int i = 0; i < HW_PROG_COUNT; i++) {
		if (!gl_link(&gl->progs[i], vs, fragments[i].cg, fragments[i].gxp, fragments[i].gxp_size))
			return false;
	}
	return true;
}


/******************************************************************************
	Texture helpers
******************************************************************************/

/*
 * Creates a texture whose storage is owned by vitaGL and returns a CPU
 * pointer to it.  vitaGL's default (uncached) pools make plain CPU stores
 * visible to the GPU without explicit cache maintenance.
 */
static uint8_t *gl_create_texture(GLuint *tex, int width, int height,
								  bool indexed)
{
	glGenTextures(1, tex);
	glBindTexture(GL_TEXTURE_2D, *tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	if (indexed) {
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0,
			GL_RED, GL_UNSIGNED_BYTE, NULL);
	} else {
		/*
		 * Allocate as a 16-bit format vitaGL stores verbatim, then retag the
		 * GXM texture as U1U5U5U5_ABGR: exactly NJEMU's 555 + bit 15 layout.
		 */
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
			GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, NULL);
		sceGxmTextureSetFormat(vglGetGxmTexture(GL_TEXTURE_2D),
			SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ABGR);
	}

	uint8_t *data = vglGetTexDataPointer(GL_TEXTURE_2D);
	if (data == NULL)
		return NULL;

	/*
	 * The core addresses layers as [row * width + x].  That only matches a
	 * linear GXM texture whose implicit stride (width aligned to 8) is width.
	 */
	SceGxmTexture *gxm = vglGetGxmTexture(GL_TEXTURE_2D);
	if (sceGxmTextureGetType(gxm) != SCE_GXM_TEXTURE_LINEAR || (width & 7) != 0) {
		gl_log("texture %dx%d is not a tightly packed linear texture\n",
			width, height);
		return NULL;
	}
	const size_t stride = (size_t)width * (indexed ? 1 : 2);

	memset(data, 0, stride * height);
	return data;
}

/*
 * GUI texture: RGBA storage of `type` owned by vitaGL, retagged as the GXM
 * format `format` (drawing may retag it again).  Returns its texels.
 */
static void *gl_create_ui_texture(GLuint *tex, int width, int height, GLenum type,
								  SceGxmTextureFormat format, GLint filter)
{
	const size_t bpp = type == GL_UNSIGNED_BYTE ? 4 : 2;

	glGenTextures(1, tex);
	glBindTexture(GL_TEXTURE_2D, *tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, type, NULL);
	sceGxmTextureSetFormat(vglGetGxmTexture(GL_TEXTURE_2D), format);

	void *data = vglGetTexDataPointer(GL_TEXTURE_2D);
	if (data != NULL)
		memset(data, 0, (size_t)((width + 7) & ~7) * height * bpp);
	return data;
}

static bool gl_create_layers(psvita_gl_video_t *gl,
							 const layer_texture_info_t *info, uint8_t count)
{
	gl->layers = calloc(count, sizeof(*gl->layers));
	if (gl->layers == NULL)
		return false;
	gl->layer_count = count;

	/* Group indexed layers into pages: same width, stacked vertically. */
	uint16_t page_height[GL_MAX_PAGES] = { 0 };
	uint16_t page_width[GL_MAX_PAGES] = { 0 };
	uint8_t layer_page[256];

	for (uint8_t i = 0; i < count; i++) {
		gl_layer_t *layer = &gl->layers[i];
		layer->width = (uint16_t)info[i].width;
		layer->height = (uint16_t)info[i].height;
		layer->bytes_per_pixel = info[i].bytes_per_pixel;

		if (layer->bytes_per_pixel != 1)
			continue;

		uint8_t p;
		for (p = 0; p < gl->page_count; p++) {
			if (page_width[p] == layer->width &&
				page_height[p] + layer->height <= GL_MAX_TEXTURE_DIM)
				break;
		}
		if (p == gl->page_count) {
			if (gl->page_count == GL_MAX_PAGES)
				return false;
			page_width[p] = layer->width;
			gl->page_count++;
		}
		layer_page[i] = p;
		layer->hw.row_offset = page_height[p];
		page_height[p] += layer->height;
	}

	for (uint8_t p = 0; p < gl->page_count; p++) {
		gl_page_t *page = &gl->pages[p];
		page->width = page_width[p];
		page->height = page_height[p];
		page->data = gl_create_texture(&page->tex, page->width, page->height, true);
		if (page->data == NULL)
			return false;
	}

	for (uint8_t i = 0; i < count; i++) {
		gl_layer_t *layer = &gl->layers[i];

		if (layer->bytes_per_pixel == 1) {
			gl_page_t *page = &gl->pages[layer_page[i]];
			layer->tex = page->tex;
			layer->hw.tex_width = page->width;
			layer->hw.tex_height = page->height;
			layer->hw.indexed = true;
			layer->cpu = page->data + (size_t)layer->hw.row_offset * page->width;
		} else if (layer->bytes_per_pixel == 2) {
			layer->hw.tex_width = layer->width;
			layer->hw.tex_height = layer->height;
			layer->hw.indexed = false;
			layer->cpu = gl_create_texture(&layer->tex, layer->width,
				layer->height, false);
			if (layer->cpu == NULL)
				return false;
		} else {
			return false;
		}
		/* The recorder only compares handles: use the GL name. */
		layer->hw.texture = (const void *)(uintptr_t)layer->tex;
	}
	return true;
}


/******************************************************************************
	Transient GPU memory
******************************************************************************/

static void gl_log_alloc_failure(psvita_gl_video_t *gl, const char *what)
{
	if (!gl->alloc_failure_logged) {
		gl_log("out of scratch memory for %s\n", what);
		gl->alloc_failure_logged = true;
	}
}

static void *gl_scratch_alloc(size_t size)
{
	uintptr_t p = (uintptr_t)vglAllocFromScratch(size + GL_SCRATCH_ALIGN);
	if (p == 0)
		return NULL;
	return (void *)((p + GL_SCRATCH_ALIGN - 1) & ~(uintptr_t)(GL_SCRATCH_ALIGN - 1));
}

/* Recorder memory: one stream VBO per vertex chunk (handle = its GL name). */
static hw_vertex_t *gl_alloc_vertices(void *user, const void **handle)
{
	psvita_gl_video_t *gl = user;
	if (gl->chunks_used == GL_MAX_CHUNKS) {
		gl_log_alloc_failure(gl, "vertices");
		return NULL;
	}

	GLuint vbo = gl->vbos[gl->chunks_used];
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, HW_CHUNK_QUADS * 4 * sizeof(hw_vertex_t), NULL, GL_STREAM_DRAW);
	hw_vertex_t *vtx = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	glUnmapBuffer(GL_ARRAY_BUFFER);
	if (vtx == NULL) {
		gl_log_alloc_failure(gl, "vertices");
		return NULL;
	}
	gl->chunks_used++;
	*handle = (const void *)(uintptr_t)vbo;
	return vtx;
}

/* Recorder memory: CLUT chunks in the scratch pool (handle = data pointer). */
static uint16_t *gl_alloc_clut(void *user, const void **handle)
{
	uint16_t *clut = gl_scratch_alloc(HW_CLUT_CHUNK_BYTES);
	if (clut == NULL) {
		gl_log_alloc_failure(user, "CLUT snapshots");
		return NULL;
	}
	*handle = clut;
	return clut;
}

static const hw_recorder_ops_t gl_ops = { gl_alloc_vertices, gl_alloc_clut };

/* Drops last frame's scratch allocations (and the recording using them). */
static void gl_frame_sync(psvita_gl_video_t *gl)
{
	uint32_t frame = vglGetFrameNumber();

	if (gl->frame_valid && frame == gl->frame)
		return;

	gl->frame_valid = true;
	gl->frame = frame;
	gl->chunks_used = 0;
	gl->ui_vtx = NULL;
	gl->fill_chunk = NULL;
	hw_rec_reset(&gl->rec);
}

/* Selects NJEMU's inverted blending (replay) or the conventional one (UI). */
static void gl_set_blend(psvita_gl_video_t *gl, bool ui)
{
	if (gl->ui_blend == ui)
		return;
	gl->ui_blend = ui;
	if (ui)
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	else
		glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA);
}

/*
 * Makes the display the render target.  The first display draw of a frame
 * clears it (to black, or to `color` when the caller clears the colour
 * anyway); `mask` adds buffers to clear now.
 */
static void gl_open_display(psvita_gl_video_t *gl, GLbitfield mask, uint32_t color)
{
	gl_frame_sync(gl);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT);
	glDisable(GL_SCISSOR_TEST);
	if (!gl->display_open) {
		gl->display_open = true;
		gl->ui_clip = (RECT){ 0, 0, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT };
		mask |= GL_COLOR_BUFFER_BIT;
	}
	if (mask) {
		glClearColor((color & 0xff) / 255.0f, ((color >> 8) & 0xff) / 255.0f,
			((color >> 16) & 0xff) / 255.0f, 1.0f);
		glClearStencil(0);
		glStencilMask(0xff);
		glClear(mask);
	}
}

/* Vertex attributes of hw_vertex_t in `vbo` for `prog`. */
static void gl_bind_vertices(const gl_program_t *prog, GLuint vbo)
{
	const GLsizei stride = sizeof(hw_vertex_t);

	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray(prog->a_uv);
	glVertexAttribPointer(prog->a_uv, 2, GL_FLOAT, GL_FALSE, stride,
		(const void *)offsetof(hw_vertex_t, u));
	glEnableVertexAttribArray(prog->a_pos);
	glVertexAttribPointer(prog->a_pos, 2, GL_SHORT, GL_FALSE, stride,
		(const void *)offsetof(hw_vertex_t, x));
	glEnableVertexAttribArray(prog->a_zp);
	glVertexAttribPointer(prog->a_zp, 2, GL_UNSIGNED_SHORT, GL_FALSE, stride,
		(const void *)offsetof(hw_vertex_t, z));
}


/******************************************************************************
	Replay
******************************************************************************/

static void gl_replay_draw(psvita_gl_video_t *gl, const hw_cmd_t *cmd,
						   const float row_x[3], const float row_y[3])
{
	gl_program_t *prog = &gl->progs[cmd->prog];

	/* The depth-tested program discards instead of blending. */
	if (cmd->prog == HW_PROG_INDEXED_DEPTH)
		glDisable(GL_BLEND);
	else
		glEnable(GL_BLEND);
	if (cmd->depth == HW_DEPTH_OFF) {
		glDisable(GL_DEPTH_TEST);
	} else {
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(cmd->depth == HW_DEPTH_TEST ? GL_GEQUAL : GL_ALWAYS);
		glDepthMask(GL_TRUE);
	}

	if (cmd->clut != NULL) {
		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, gl->clut_tex);
		sceGxmTextureSetData(vglGetGxmTexture(GL_TEXTURE_2D), cmd->clut);
	}
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, cmd->texture ? (GLuint)(uintptr_t)cmd->texture : gl->clut_tex);

	glUseProgram(prog->id);
	glUniform4f(prog->u_row_x, row_x[0], row_x[1], row_x[2], 0.0f);
	glUniform4f(prog->u_row_y, row_y[0], row_y[1], row_y[2], 0.0f);
	glUniform4f(prog->u_tex_scale, 1.0f / cmd->tex_w, 1.0f / cmd->tex_h,
		1.0f / HW_CLUT_ROWS, 0.0f);

	gl_bind_vertices(prog, (GLuint)(uintptr_t)cmd->vertices);

	uint32_t first = cmd->first_quad, quads = cmd->quads;
	while (quads) {
		uint32_t n = quads > GL_DRAW_MAX_QUADS ? GL_DRAW_MAX_QUADS : quads;
		glDrawArrays(GL_QUADS, first * 4, n * 4);
		gl->stat_draws++;
		gl->stat_quads += n;
		first += n;
		quads -= n;
	}
}

/*
 * Replays the recorded work frame into the display back buffer, mapping the
 * work frame rectangle `src_rect` onto `dst_rect` with the given orientation.
 * Everything lands in the display scene: no intermediate render target.
 */
static void gl_present(psvita_gl_video_t *gl, const RECT *src_rect,
					   const RECT *dst_rect, int orient)
{
	hw_xform_t m;
	RECT d;
	float row_x[3], row_y[3];

	hw_rec_flush(&gl->rec);
	gl_frame_sync(gl);

	if (!hw_present_geometry(src_rect, dst_rect, orient, PSVITA_DISPLAY_WIDTH,
			PSVITA_DISPLAY_HEIGHT, &d, &m))
		return;
	gl->last_present_xform = m;
	gl->last_present_valid = true;
	hw_clip_rows(&m, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT, row_x, row_y);

	/*
	 * Only the destination rectangle shows the work frame, at pixel precision.
	 * vitaGL builds glScissor from a tile-granular region clip plus two
	 * mask-update draws; a stencil mask drawn with one display-space quad
	 * gives the same result.
	 */
	hw_recorder_t *rec = &gl->rec;
	const uint32_t cmd_first = rec->cmd_count;
	hw_rec_fill(rec, &d, 0x8000, HW_DEPTH_OFF);
	hw_rec_flush(rec);
	if (rec->cmd_count != cmd_first + 1 || rec->cmds[cmd_first].quads != 1)
		return;
	const hw_cmd_t mask = rec->cmds[--rec->cmd_count];
	const float display_x[3] = { 2.0f / PSVITA_DISPLAY_WIDTH, 0.0f, -1.0f };
	const float display_y[3] = { 0.0f, -2.0f / PSVITA_DISPLAY_HEIGHT, 1.0f };

	gl_open_display(gl, GL_STENCIL_BUFFER_BIT, 0);
	gl_set_blend(gl, false);
	glDisable(GL_DEPTH_TEST);

	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_ALWAYS, 1, 0xff);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
	gl_replay_draw(gl, &mask, display_x, display_y);
	glStencilFunc(GL_EQUAL, 1, 0xff);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

	for (uint32_t i = 0; i < rec->cmd_count; i++)
		gl_replay_draw(gl, &rec->cmds[i], row_x, row_y);

	glDisable(GL_STENCIL_TEST);
	glDisable(GL_DEPTH_TEST);

	if (psvita_dump_wanted(++gl->presented)) {
		uint8_t *display = malloc(PSVITA_DISPLAY_WIDTH * PSVITA_DISPLAY_HEIGHT * 4);
		if (display != NULL) {
			glReadPixels(0, 0, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT, GL_RGBA,
				GL_UNSIGNED_BYTE, display);
			psvita_dump_frame("vitagl", gl->presented, display, PSVITA_DISPLAY_WIDTH,
				true, src_rect, &m);
			free(display);
		}
	}
}


/******************************************************************************
	Driver: lifetime
******************************************************************************/

static void psvita_gl_free(void *data);

static void *psvita_gl_init(layer_texture_info_t *layer_textures,
							uint8_t layer_textures_count,
							clut_info_t *clut_info)
{
	psvita_gl_video_t *gl = calloc(1, sizeof(*gl));
	if (gl == NULL)
		return NULL;

	/*
	 * 2D pixel art: no MSAA.  Leave 16 MB of user RAM for the rest of NJEMU.
	 * vitaGL returns GL_TRUE only when it had to fall back to a smaller display.
	 */
	if (vglInitExtended(0x80000, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT,
			0x1000000, SCE_GXM_MULTISAMPLE_NONE))
		gl_log("vitaGL fell back to a smaller display resolution\n");
	vglWaitVblankStart(GL_TRUE);
	/* Only our per-frame stream buffers may live in the scratch pool. */
	vglSetupScratchMemory(GL_FALSE, GL_TRUE);

	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);
	glDepthFunc(GL_GEQUAL);
	glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA);

	if (!gl_create_programs(gl)) {
		gl_log("shader setup failed\n");
		goto fail;
	}

	if (!gl_create_layers(gl, layer_textures, layer_textures_count)) {
		gl_log("texture layer setup failed\n");
		goto fail;
	}

	/* CLUT: raw 555 rows sampled through a U1U5U5U5 texture. */
	if (!hw_rec_init(&gl->rec, &gl_ops, gl, GL_WORK_WIDTH, GL_WORK_HEIGHT, clut_info))
		goto fail;
	if (gl_create_texture(&gl->clut_tex, HW_CLUT_ROW_ENTRIES, HW_CLUT_ROWS, false) == NULL) {
		gl_log("CLUT texture setup failed\n");
		goto fail;
	}

	/* Streamed vertex chunks, re-specified every frame (see GL_CHUNK_QUADS). */
	glGenBuffers(GL_MAX_CHUNKS, gl->vbos);

	/* GUI: failures only disable the corresponding UI drawing. */
	glGenBuffers(1, &gl->ui_vbo);
	gl_create_ui_texture(&gl->fill_tex, 2, 2, GL_UNSIGNED_BYTE,
		SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, GL_LINEAR);
	gl_create_ui_texture(&gl->frame_tex, 8, 8, GL_UNSIGNED_BYTE,
		SCE_GXM_TEXTURE_FORMAT_X8U8U8U8_1BGR, GL_NEAREST);
	gl->scratch = gl_create_ui_texture(&gl->scratch_tex, GL_SCRATCH_WIDTH, GL_SCRATCH_HEIGHT,
		GL_UNSIGNED_SHORT_5_5_5_1, SCE_GXM_TEXTURE_FORMAT_X1U5U5U5_1BGR, GL_NEAREST);
	gl->atlas = calloc(1, sizeof(*gl->atlas));
	if (gl->atlas != NULL) {
		gl->atlas->texels = gl_create_ui_texture(&gl->atlas_tex, GL_UI_ATLAS_WIDTH,
			GL_UI_ATLAS_HEIGHT, GL_UNSIGNED_SHORT_4_4_4_4, SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_ABGR,
			GL_NEAREST);
		gl->atlas->width = GL_UI_ATLAS_WIDTH;
		gl->atlas->height = GL_UI_ATLAS_HEIGHT;
	}
	gl->draw_fill = 0xff000000u;

	gl_log("vitaGL init ok: %u layers in %u page(s), %u CLUT windows\n",
		(unsigned)gl->layer_count, (unsigned)gl->page_count, (unsigned)gl->rec.clut_windows);
	return gl;

fail:
	psvita_gl_free(gl);
	return NULL;
}

static void psvita_gl_free(void *data)
{
	psvita_gl_video_t *gl = data;
	if (gl == NULL)
		return;

	glFinish();

	if (gl->vbos[0])
		glDeleteBuffers(GL_MAX_CHUNKS, gl->vbos);
	if (gl->ui_vbo)
		glDeleteBuffers(1, &gl->ui_vbo);
	{
		const GLuint ui_textures[] = { gl->fill_tex, gl->frame_tex, gl->scratch_tex, gl->atlas_tex };
		for (size_t i = 0; i < sizeof(ui_textures) / sizeof(ui_textures[0]); i++) {
			if (ui_textures[i])
				glDeleteTextures(1, &ui_textures[i]);
		}
	}
	free(gl->atlas);
	if (gl->clut_tex)
		glDeleteTextures(1, &gl->clut_tex);
	for (uint8_t i = 0; i < gl->layer_count; i++) {
		if (gl->layers[i].bytes_per_pixel != 1 && gl->layers[i].tex)
			glDeleteTextures(1, &gl->layers[i].tex);
	}
	for (uint8_t p = 0; p < gl->page_count; p++)
		glDeleteTextures(1, &gl->pages[p].tex);
	for (int i = 0; i < HW_PROG_COUNT; i++) {
		if (gl->progs[i].id)
			glDeleteProgram(gl->progs[i].id);
	}

	/* vitaGL has no public shutdown; the GXM context lives until exit. */
	hw_rec_free(&gl->rec);
	free(gl->layers);
	free(gl);
}


/******************************************************************************
	Driver: frame control
******************************************************************************/

static void psvita_gl_waitVsync(void *data)
{
	(void)data;
	sceDisplayWaitVblankStart();
}

static void psvita_gl_flipScreen(void *data, bool vsync)
{
	psvita_gl_video_t *gl = data;

	/* Nothing drawn since the last flip (e.g. a message box waiting for a
	 * key): keep showing the current frame instead of a stale back buffer. */
	if (!gl->display_open) {
		if (vsync)
			sceDisplayWaitVblankStart();
		return;
	}

	vglWaitVblankStart(vsync ? GL_TRUE : GL_FALSE);
	vglSwapBuffers(GL_FALSE);
	gl->display_open = false;

#if PSVITA_VIDEO_STATS
	if (++gl->stat_frames == PSVITA_VIDEO_STATS_FRAMES) {
		/* Every frame ends with one scene; splits are the extra ones. */
		uint32_t splits = &vgl_debug_scene_splits ? vgl_debug_scene_splits : 0;
		const float n = (float)gl->stat_frames;
		gl_log("vitaGL frames=%u scenes/frame=%.2f draws/frame=%.1f quads/frame=%.0f clut_rows/frame=%.1f%s\n",
			(unsigned)gl->stat_frames,
			1.0f + (splits - gl->stat_splits_base) / n,
			gl->stat_draws / n, gl->stat_quads / n, gl->rec.stat_clut_rows / n,
			&vgl_debug_scene_splits ? "" : " (scene count unavailable)");
		gl->stat_splits_base = splits;
		gl->stat_frames = gl->stat_draws = gl->stat_quads = gl->rec.stat_clut_rows = 0;
	}
#else
	(void)gl;
#endif
}

static void psvita_gl_beginFrame(void *data)
{
	gl_frame_sync(data);
}

static void psvita_gl_endFrame(void *data)
{
	hw_rec_flush(&((psvita_gl_video_t *)data)->rec);
}

static void *psvita_gl_frameAddr(void *data, int frameIndex, int x, int y)
{
	psvita_gl_video_t *gl = data;

	/* Only the UI scratch surface is CPU-addressable (PSP 5551 texels). */
	if (frameIndex != COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER || gl->scratch == NULL
		|| x < 0 || y < 0 || x >= GL_SCRATCH_WIDTH || y >= GL_SCRATCH_HEIGHT)
		return NULL;
	return gl->scratch + y * GL_SCRATCH_WIDTH + x;
}

/* The last presented frame (vitaGL's front display buffer), or NULL. */
static const uint32_t *gl_front_buffer(int *stride)
{
	if (&gxm_front_buffer_index == NULL || (void *)gxm_color_surfaces_addr == NULL)
		return NULL;
	*stride = &DISPLAY_STRIDE != NULL ? DISPLAY_STRIDE : PSVITA_DISPLAY_WIDTH;
	return gxm_color_surfaces_addr[gxm_front_buffer_index];
}

/*
 * CPU copy of a frame as PSP 555 texels. SHOW_FRAME_BUFFER is read from the
 * displayed frame in the 480x272 logical space (sampled from the 960x544
 * display); INITIAL_TEXTURE_LAYER from the scratch surface.
 */
static int psvita_gl_readFrame(void *data, int frameIndex, int x, int y, int width, int height,
							   uint16_t *dst, int dstPitch)
{
	psvita_gl_video_t *gl = data;
	const uint32_t *fb;
	int stride;

	if (dst == NULL || width <= 0 || height <= 0 || x < 0 || y < 0)
		return 0;

	if (frameIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER) {
		if (gl->scratch == NULL || x + width > GL_SCRATCH_WIDTH || y + height > GL_SCRATCH_HEIGHT)
			return 0;
		for (int row = 0; row < height; row++)
			memcpy(dst + (size_t)row * dstPitch, gl->scratch + (size_t)(y + row) * GL_SCRATCH_WIDTH + x,
				(size_t)width * sizeof(uint16_t));
		return 1;
	}

	if (frameIndex != COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER
		|| x + width > SCR_WIDTH || y + height > SCR_HEIGHT
		|| (fb = gl_front_buffer(&stride)) == NULL)
		return 0;

	glFinish();
	for (int row = 0; row < height; row++) {
		const uint32_t *src = fb + (size_t)((y + row) * PSVITA_DISPLAY_HEIGHT / SCR_HEIGHT) * stride;
		for (int col = 0; col < width; col++) {
			const uint32_t c = src[(x + col) * PSVITA_DISPLAY_WIDTH / SCR_WIDTH];
			dst[(size_t)row * dstPitch + col] = hw_rgba_to_555(c);
		}
	}
	return 1;
}

static void psvita_gl_getOutputSize(void *data, int *width, int *height)
{
	(void)data;
	*width = PSVITA_DISPLAY_WIDTH;
	*height = PSVITA_DISPLAY_HEIGHT;
}

static void psvita_gl_scissor(void *data, uint16_t left, uint16_t top,
							  uint16_t right, uint16_t bottom)
{
	hw_rec_set_clip(&((psvita_gl_video_t *)data)->rec, left, top, right, bottom);
}

/* DRAW_FRAME_BUFFER / SHOW_FRAME_BUFFER are the display being built. */
static void gl_clear_display(psvita_gl_video_t *gl, uint32_t color)
{
	gl_open_display(gl, GL_COLOR_BUFFER_BIT, color);
	gl->draw_fill = color | 0xff000000u;
}

static void psvita_gl_clearScreen(void *data)
{
	gl_clear_display(data, 0);
}

static void psvita_gl_clearFrame(void *data, int index)
{
	/* SCREEN_BITMAP is the recorded work frame; the others are the display. */
	psvita_gl_video_t *gl = data;

	if (index == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP) {
		gl_frame_sync(gl);
		hw_rec_fill(&gl->rec, &gl->rec.work, 0, HW_DEPTH_OFF);
	} else if (index == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER) {
		if (gl->scratch != NULL)
			memset(gl->scratch, 0, (size_t)GL_SCRATCH_WIDTH * GL_SCRATCH_HEIGHT * sizeof(uint16_t));
	} else {
		gl_clear_display(gl, 0);
	}
}

static void psvita_gl_fillFrame(void *data, int frameIndex, uint32_t color)
{
	psvita_gl_video_t *gl = data;

	if (frameIndex == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP) {
		gl_frame_sync(gl);
		hw_rec_fill(&gl->rec, &gl->rec.work, hw_rgba_to_555(color), HW_DEPTH_OFF);
	} else
		gl_clear_display(gl, color);
}

static void psvita_gl_startWorkFrame(void *data, uint32_t color)
{
	psvita_gl_video_t *gl = data;

	gl_frame_sync(gl);
	gl->pending_flip = false;
	gl->screen_fill_valid = false;
	hw_rec_begin_work(&gl->rec, hw_rgba_to_555(color));
}

static void gl_ui_fill(psvita_gl_video_t *gl, int x, int y, int w, int h, uint32_t c0,
					   uint32_t c1, uint32_t c2, uint32_t c3, bool clip);
static void gl_draw_scratch(psvita_gl_video_t *gl, const RECT *src, const RECT *dst);
static void gl_draw_front(psvita_gl_video_t *gl);

static bool gl_capture_front_to_scratch(psvita_gl_video_t *gl, const RECT *src,
	const RECT *dst, bool rotate)
{
	const uint32_t *fb;
	int stride;
	const int sw = src->right - src->left;
	const int sh = src->bottom - src->top;
	const int dw = dst->right - dst->left;
	const int dh = dst->bottom - dst->top;

	if (gl->scratch == NULL || !gl->last_present_valid || sw <= 0 || sh <= 0 ||
		dw <= 0 || dh <= 0 || dst->left < 0 || dst->top < 0 ||
		dst->right > GL_SCRATCH_WIDTH || dst->bottom > GL_SCRATCH_HEIGHT ||
		(fb = gl_front_buffer(&stride)) == NULL)
		return false;

	glFinish();
	for (int y = 0; y < dh; y++) {
		uint16_t *out = gl->scratch + (size_t)(dst->top + y) * GL_SCRATCH_WIDTH + dst->left;
		for (int x = 0; x < dw; x++) {
			float sx, sy, px, py;
			if (rotate) {
				/* Inverse of HW_ORIENT_ROTATE: dest TL <- src TR. */
				sx = src->right - ((y + 0.5f) * sw / dh);
				sy = src->top + ((x + 0.5f) * sh / dw);
			} else {
				sx = src->left + ((x + 0.5f) * sw / dw);
				sy = src->top + ((y + 0.5f) * sh / dh);
			}
			hw_map_point(&gl->last_present_xform, sx, sy, &px, &py);
			int ix = (int)px;
			int iy = (int)py;
			if (ix < 0) ix = 0;
			if (iy < 0) iy = 0;
			if (ix >= PSVITA_DISPLAY_WIDTH) ix = PSVITA_DISPLAY_WIDTH - 1;
			if (iy >= PSVITA_DISPLAY_HEIGHT) iy = PSVITA_DISPLAY_HEIGHT - 1;
			out[x] = hw_rgba_to_555(fb[(size_t)iy * stride + ix]);
		}
	}
	return true;
}

static void psvita_gl_transferWorkFrame(void *data, RECT *src_rect, RECT *dst_rect)
{
	psvita_gl_video_t *gl = data;

	/* The UI background cached in SCREEN_BITMAP is a solid fill (copyRect). */
	if (gl->screen_fill_valid) {
		gl_ui_fill(gl, dst_rect->left, dst_rect->top, dst_rect->right - dst_rect->left,
			dst_rect->bottom - dst_rect->top, gl->screen_fill, gl->screen_fill,
			gl->screen_fill, gl->screen_fill, false);
		return;
	}
	gl_present(gl, src_rect, dst_rect, HW_ORIENT_NORMAL);
}

static void psvita_gl_copyRect(void *data, int srcIndex, int dstIndex,
							   RECT *src_rect, RECT *dst_rect)
{
	psvita_gl_video_t *gl = data;
	const bool dst_display = dstIndex == COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER
		|| dstIndex == COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER;

	if (srcIndex == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP) {
		if (dstIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER)
			gl_capture_front_to_scratch(gl, src_rect, dst_rect, false);
		else if (dst_display)
			gl_present(gl, src_rect, dst_rect, HW_ORIENT_NORMAL);
	} else if (srcIndex == COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER
			   && dstIndex == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP) {
		/*
		 * Either the CPS rotate+flip sequence, whose flip is folded into the
		 * final rotated present, or the UI caching its background, which is
		 * the solid fill of the display (no chrome without UI_DRAW_CAP_CACHE_CHROME).
		 */
		if (!gl->pending_flip) {
			gl->screen_fill = gl->draw_fill;
			gl->screen_fill_valid = true;
		}
	} else if (srcIndex == COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER
			   && dstIndex == COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER) {
		/* Dialogs start from the displayed frame: draw it back, whole. */
		gl_draw_front(gl);
	} else if (srcIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER && dst_display) {
		gl_draw_scratch(gl, src_rect, dst_rect);
	}
}

static void psvita_gl_copyRectFlip(void *data, int srcIndex, int dstIndex,
								   RECT *src_rect, RECT *dst_rect)
{
	psvita_gl_video_t *gl = data;
	(void)dstIndex;

	if (srcIndex != COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP)
		return;

	/*
	 * CPS rotate+flip first flips the work frame in place (src == dst) and
	 * copies it back before rotating.  Fold that flip into the rotated
	 * present instead of bouncing the frame through another surface.
	 */
	if (src_rect->left == dst_rect->left && src_rect->top == dst_rect->top &&
		src_rect->right == dst_rect->right && src_rect->bottom == dst_rect->bottom) {
		gl->pending_flip = true;
		return;
	}

	gl_present(gl, src_rect, dst_rect, HW_ORIENT_FLIP);
}

static void psvita_gl_copyRectRotate(void *data, int srcIndex, int dstIndex,
									 RECT *src_rect, RECT *dst_rect)
{
	psvita_gl_video_t *gl = data;
	(void)dstIndex;

	if (srcIndex != COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP)
		return;
	if (dstIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER) {
		gl_capture_front_to_scratch(gl, src_rect, dst_rect, true);
		return;
	}

	gl_present(gl, src_rect, dst_rect,
		gl->pending_flip ? HW_ORIENT_ROTATE_FLIP : HW_ORIENT_ROTATE);
	gl->pending_flip = false;
}

static void psvita_gl_drawTexture(void *data, int srcIndex, int dstIndex,
								  RECT *src_rect, RECT *dst_rect)
{
	if (srcIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER
		&& (dstIndex == COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER
			|| dstIndex == COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER))
		gl_draw_scratch(data, src_rect, dst_rect);
}



/******************************************************************************
	Driver: drawing
******************************************************************************/

static void psvita_gl_commitTextureUpdates(void *data, uint8_t textureIndex)
{
	/* The texture rects were written straight into vitaGL's texture storage. */
	(void)data;
	(void)textureIndex;
}

static void psvita_gl_writeIndexedTextureRect(void *data, uint8_t textureIndex,
											  int x, int y, int width, int height,
											  const uint8_t *pixels, int srcPitch)
{
	psvita_gl_video_t *gl = data;
	const gl_layer_t *layer;

	if (textureIndex >= gl->layer_count || gl->layers[textureIndex].bytes_per_pixel != 1)
		return;
	layer = &gl->layers[textureIndex];
	psvita_write_texture_rect(layer->cpu, layer->width, layer->height, 1,
		x, y, width, height, pixels, srcPitch);
}

static void psvita_gl_writeDirectTextureRect(void *data, uint8_t textureIndex,
											 int x, int y, int width, int height,
											 const uint16_t *pixels, int srcPitch)
{
	psvita_gl_video_t *gl = data;
	const gl_layer_t *layer;

	if (textureIndex >= gl->layer_count || gl->layers[textureIndex].bytes_per_pixel != 2)
		return;
	layer = &gl->layers[textureIndex];
	psvita_write_texture_rect(layer->cpu, layer->width, layer->height, 2,
		x, y, width, height, pixels, srcPitch);
}

static void psvita_gl_uploadClut(void *data, uint16_t *bank, uint8_t bank_index)
{
	/* CLUT windows are captured lazily per batch in blitSpriteVertices(). */
	(void)data;
	(void)bank;
	(void)bank_index;
}

static void psvita_gl_blitSpriteVertices(void *data, uint8_t textureIndex,
										 const uint16_t *clut, uint8_t bank_index,
										 uint32_t vertices_count,
										 const video_sprite_vertex_t *vertices)
{
	psvita_gl_video_t *gl = data;
	(void)bank_index;

	if (textureIndex >= gl->layer_count)
		return;
	gl_frame_sync(gl);
	hw_rec_blit(&gl->rec, &gl->layers[textureIndex].hw, clut, vertices, vertices_count);
}

static void psvita_gl_blitPointVertices(void *data, uint32_t points_count,
										const video_point_vertex_t *vertices)
{
	psvita_gl_video_t *gl = data;

	gl_frame_sync(gl);
	hw_rec_points(&gl->rec, vertices, points_count);
}


static void psvita_gl_enableDepthTest(void *data)
{
	hw_rec_set_depth_test(&((psvita_gl_video_t *)data)->rec, true);
}

static void psvita_gl_disableDepthTest(void *data)
{
	hw_rec_set_depth_test(&((psvita_gl_video_t *)data)->rec, false);
}

static void psvita_gl_clearDepthBuffer(void *data)
{
	psvita_gl_video_t *gl = data;

	/* PSP semantics: depth cleared to 0, GEQUAL test.  Colour untouched. */
	gl_frame_sync(gl);
	hw_rec_fill(&gl->rec, &gl->rec.work, 0x8000, HW_DEPTH_WRITE);
}

static void psvita_gl_clearColorBuffer(void *data)
{
	psvita_gl_video_t *gl = data;

	/* Clears the colour inside the current scissor, keeping depth. */
	gl_frame_sync(gl);
	hw_rec_fill(&gl->rec, &gl->rec.clip, 0, HW_DEPTH_OFF);
}

/******************************************************************************
	Driver: UI primitives

	Common UI code passes display pixels.  Everything goes into the display
	scene with the direct program and conventional alpha blending; vertices
	come from a stream VBO, colours and copies from the scratch pool.  The UI
	scissor is applied on the CPU, like the work frame's.
******************************************************************************/

static hw_vertex_t *gl_ui_alloc(psvita_gl_video_t *gl, GLint *first)
{
	gl_frame_sync(gl);
	if (gl->ui_vtx == NULL || gl->ui_quads == GL_UI_CHUNK_QUADS) {
		glBindBuffer(GL_ARRAY_BUFFER, gl->ui_vbo);
		glBufferData(GL_ARRAY_BUFFER, GL_UI_CHUNK_QUADS * 4 * sizeof(hw_vertex_t), NULL,
			GL_STREAM_DRAW);
		gl->ui_vtx = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
		glUnmapBuffer(GL_ARRAY_BUFFER);
		gl->ui_quads = 0;
		if (gl->ui_vtx == NULL) {
			gl_log_alloc_failure(gl, "UI vertices");
			return NULL;
		}
	}
	*first = (GLint)gl->ui_quads * 4;
	return gl->ui_vtx + gl->ui_quads++ * 4;
}

/* Colours of one fill, as a 2x2 texture (rows of 8 texels). */
static uint32_t *gl_ui_fill_texels(psvita_gl_video_t *gl)
{
	if (gl->fill_chunk == NULL || gl->fill_used + GL_UI_FILL_BYTES > GL_UI_FILL_CHUNK) {
		gl->fill_chunk = gl_scratch_alloc(GL_UI_FILL_CHUNK);
		gl->fill_used = 0;
		if (gl->fill_chunk == NULL) {
			gl_log_alloc_failure(gl, "UI colours");
			return NULL;
		}
	}
	uint32_t *texels = (uint32_t *)(gl->fill_chunk + gl->fill_used);
	gl->fill_used += GL_UI_FILL_BYTES;
	return texels;
}

/* Draws `count` vertices from the UI chunk with the texture bound to unit 0. */
static void gl_ui_emit(psvita_gl_video_t *gl, GLenum mode, GLint first, GLsizei count,
					   int tex_w, int tex_h)
{
	const gl_program_t *prog = &gl->progs[HW_PROG_DIRECT];

	gl_set_blend(gl, true);
	glEnable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);
	glUseProgram(prog->id);
	glUniform4f(prog->u_row_x, 2.0f / PSVITA_DISPLAY_WIDTH, 0.0f, -1.0f, 0.0f);
	glUniform4f(prog->u_row_y, 0.0f, -2.0f / PSVITA_DISPLAY_HEIGHT, 1.0f, 0.0f);
	glUniform4f(prog->u_tex_scale, 1.0f / tex_w, 1.0f / tex_h, 0.0f, 0.0f);
	gl_bind_vertices(prog, gl->ui_vbo);
	glDrawArrays(mode, first, count);
	gl->stat_draws++;
}

/*
 * Clips a textured display quad to `clip` and queues it; returns its first
 * vertex, or -1 when nothing is left.  The caller binds the texture and emits.
 */
static GLint gl_ui_quad(psvita_gl_video_t *gl, const RECT *clip, int x0, int y0, int x1, int y1,
						float u0, float v0, float u1, float v1)
{
	GLint first;
	hw_vertex_t *q;

	if (!hw_clip_quad(clip, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1))
		return -1;
	gl_open_display(gl, 0, 0);
	if ((q = gl_ui_alloc(gl, &first)) == NULL)
		return -1;
	hw_write_quad(q, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1, u0, v0, u1, v1, 0, 0);
	return first;
}

/* Binds the fill texture to `texels` (see gl_ui_fill_texels). */
static void gl_ui_bind_fill(psvita_gl_video_t *gl, const uint32_t *texels)
{
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl->fill_tex);
	sceGxmTextureSetData(vglGetGxmTexture(GL_TEXTURE_2D), texels);
}

/*
 * Rectangle with one colour per corner: top-left, top-right, bottom-left,
 * bottom-right.  Sampling a 2x2 texture from texel centre to texel centre
 * with bilinear filtering interpolates them across the rectangle.
 */
static void gl_ui_fill(psvita_gl_video_t *gl, int x, int y, int w, int h, uint32_t c0,
					   uint32_t c1, uint32_t c2, uint32_t c3, bool clip)
{
	static const RECT display = { 0, 0, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT };
	uint32_t *texels;
	GLint first;

	if (w <= 0 || h <= 0)
		return;
	first = gl_ui_quad(gl, clip ? &gl->ui_clip : &display, x, y, x + w, y + h,
		0.5f, 0.5f, 1.5f, 1.5f);
	if (first < 0 || (texels = gl_ui_fill_texels(gl)) == NULL)
		return;
	texels[0] = c0;
	texels[1] = c1;
	texels[8] = c2;
	texels[9] = c3;
	gl_ui_bind_fill(gl, texels);
	gl_ui_emit(gl, GL_QUADS, first, 4, 2, 2);
}

/* Draws the scratch surface (INITIAL_TEXTURE_LAYER) opaque onto the display. */
static void gl_draw_scratch(psvita_gl_video_t *gl, const RECT *src, const RECT *dst)
{
	static const RECT display = { 0, 0, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT };
	GLint first;

	if (gl->scratch == NULL)
		return;
	first = gl_ui_quad(gl, &display, dst->left, dst->top, dst->right, dst->bottom,
		src->left, src->top, src->right, src->bottom);
	if (first < 0)
		return;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl->scratch_tex);
	gl_ui_emit(gl, GL_QUADS, first, 4, GL_SCRATCH_WIDTH, GL_SCRATCH_HEIGHT);
}

/*
 * Draws the last presented frame back onto the display.  The GPU renders
 * scenes in order, so this scene samples it complete.
 */
static void gl_draw_front(psvita_gl_video_t *gl)
{
	static const RECT display = { 0, 0, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT };
	const uint32_t *fb;
	int stride;
	GLint first;

	if ((fb = gl_front_buffer(&stride)) == NULL)
		return;
	first = gl_ui_quad(gl, &display, 0, 0, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT,
		0.0f, 0.0f, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT);
	if (first < 0)
		return;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl->frame_tex);
	SceGxmTexture *t = vglGetGxmTexture(GL_TEXTURE_2D);
	if (sceGxmTextureInitLinearStrided(t, fb, SCE_GXM_TEXTURE_FORMAT_X8U8U8U8_1BGR,
			PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT, stride * 4) < 0)
		return;
	gl_ui_emit(gl, GL_QUADS, first, 4, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT);
}

/*
 * UI textures come from psvita_ui_draw.c as linear CPU buffers of PSP-layout
 * texels (4444 or 5551, alpha in the top bits): the ABGR GXM formats. Without
 * blending the alpha is forced to 1.  The texels are drawn from the UI atlas.
 */
static void psvita_gl_drawUISprite(void *data, void *tex, int tex_format, int tex_swizzled,
								   int tex_width, int tex_height, int tex_stride,
								   int su, int sv, int sw, int sh,
								   int dx, int dy, int dw, int dh, int blend)
{
	psvita_gl_video_t *gl = data;
	SceGxmTextureFormat format;
	const uint16_t *texels;
	int x0 = dx, y0 = dy, x1 = dx + dw, y1 = dy + dh;
	int ax, ay;
	(void)tex_swizzled;
	(void)tex_width;

	if (tex == NULL || gl->atlas == NULL || gl->atlas->texels == NULL || sw <= 0 || sh <= 0
		|| su < 0 || sv < 0 || sv + sh > tex_height || su + sw > tex_stride)
		return;
	/* Nothing visible: skip the atlas. */
	if (x1 <= gl->ui_clip.left || x0 >= gl->ui_clip.right
		|| y1 <= gl->ui_clip.top || y0 >= gl->ui_clip.bottom || x1 <= x0 || y1 <= y0)
		return;
	if (tex_format == UI_PIXFMT_4444)
		format = blend ? SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_ABGR : SCE_GXM_TEXTURE_FORMAT_X4U4U4U4_1BGR;
	else
		format = blend ? SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ABGR : SCE_GXM_TEXTURE_FORMAT_X1U5U5U5_1BGR;

	texels = (const uint16_t *)tex + (size_t)sv * tex_stride + su;
	if (!psvita_ui_atlas_get(gl->atlas, texels, tex_stride, sw, sh, &ax, &ay)) {
		if (!gl->atlas->full)
			return;
		/* Wait for the GPU to stop reading the atlas (this ends the scene). */
		glFinish();
		psvita_ui_atlas_reset(gl->atlas);
		if (!psvita_ui_atlas_get(gl->atlas, texels, tex_stride, sw, sh, &ax, &ay))
			return;
	}

	const GLint first = gl_ui_quad(gl, &gl->ui_clip, x0, y0, x1, y1,
		ax, ay, ax + sw, ay + sh);
	if (first < 0)
		return;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, gl->atlas_tex);
	sceGxmTextureSetFormat(vglGetGxmTexture(GL_TEXTURE_2D), format);
	gl_ui_emit(gl, GL_QUADS, first, 4, gl->atlas->width, gl->atlas->height);
}

static void gl_ui_line(psvita_gl_video_t *gl, int x1, int y1, int x2, int y2,
					   uint32_t color1, uint32_t color2)
{
	/* hline()/vline() pass an exclusive end point: draw 1-pixel rectangles. */
	if (y1 == y2) {
		if (x2 < x1) {
			int t = x1; x1 = x2; x2 = t;
			uint32_t c = color1; color1 = color2; color2 = c;
		}
		gl_ui_fill(gl, x1, y1, x2 - x1, 1, color1, color2, color1, color2, true);
	} else if (x1 == x2) {
		if (y2 < y1) {
			int t = y1; y1 = y2; y2 = t;
			uint32_t c = color1; color1 = color2; color2 = c;
		}
		gl_ui_fill(gl, x1, y1, 1, y2 - y1, color1, color1, color2, color2, true);
	} else {
		/* Diagonal lines are not clipped to the UI scissor. */
		uint32_t *texels;
		hw_vertex_t *q;
		GLint first;

		gl_open_display(gl, 0, 0);
		if ((q = gl_ui_alloc(gl, &first)) == NULL || (texels = gl_ui_fill_texels(gl)) == NULL)
			return;
		q[0] = (hw_vertex_t){ 0.5f, 0.5f, (int16_t)x1, (int16_t)y1, 0, 0 };
		q[1] = (hw_vertex_t){ 1.5f, 0.5f, (int16_t)x2, (int16_t)y2, 0, 0 };
		texels[0] = texels[8] = color1;
		texels[1] = texels[9] = color2;
		gl_ui_bind_fill(gl, texels);
		gl_ui_emit(gl, GL_LINES, first, 2, 2, 2);
	}
}

static void psvita_gl_drawUILine(void *data, int x1, int y1, int x2, int y2, uint32_t color)
{
	gl_ui_line(data, x1, y1, x2, y2, color, color);
}

static void psvita_gl_drawUILineGradient(void *data, int x1, int y1, int x2, int y2,
										 uint32_t color1, uint32_t color2)
{
	gl_ui_line(data, x1, y1, x2, y2, color1, color2);
}

static void psvita_gl_drawUIRect(void *data, int x, int y, int w, int h, uint32_t color)
{
	psvita_gl_video_t *gl = data;

	gl_ui_fill(gl, x, y, w, 1, color, color, color, color, true);
	gl_ui_fill(gl, x, y + h - 1, w, 1, color, color, color, color, true);
	gl_ui_fill(gl, x, y, 1, h, color, color, color, color, true);
	gl_ui_fill(gl, x + w - 1, y, 1, h, color, color, color, color, true);
}

static void psvita_gl_fillUIRect(void *data, int x, int y, int w, int h, uint32_t color)
{
	gl_ui_fill(data, x, y, w, h, color, color, color, color, true);
}

static void psvita_gl_fillUIRectGradient(void *data, int x, int y, int w, int h,
										 uint32_t color1, uint32_t color2, int direction)
{
	if (direction == UI_GRADIENT_HORIZONTAL)
		gl_ui_fill(data, x, y, w, h, color1, color2, color1, color2, true);
	else
		gl_ui_fill(data, x, y, w, h, color1, color1, color2, color2, true);
}

static void psvita_gl_setUIScissor(void *data, int x, int y, int w, int h)
{
	psvita_gl_video_t *gl = data;
	int x1 = x + w, y1 = y + h;

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x1 > PSVITA_DISPLAY_WIDTH) x1 = PSVITA_DISPLAY_WIDTH;
	if (y1 > PSVITA_DISPLAY_HEIGHT) y1 = PSVITA_DISPLAY_HEIGHT;
	gl->ui_clip = (RECT){ x, y, x1, y1 };
}

video_driver_t video_psvita_gl = {
	.ident = "psvita_gl",
	.init = psvita_gl_init,
	.free = psvita_gl_free,
	.waitVsync = psvita_gl_waitVsync,
	.flipScreen = psvita_gl_flipScreen,
	.beginFrame = psvita_gl_beginFrame,
	.endFrame = psvita_gl_endFrame,
	.frameAddr = psvita_gl_frameAddr,
	.readFrame = psvita_gl_readFrame,
	.getOutputSize = psvita_gl_getOutputSize,
	.scissor = psvita_gl_scissor,
	.clearScreen = psvita_gl_clearScreen,
	.clearFrame = psvita_gl_clearFrame,
	.fillFrame = psvita_gl_fillFrame,
	.startWorkFrame = psvita_gl_startWorkFrame,
	.transferWorkFrame = psvita_gl_transferWorkFrame,
	.copyRect = psvita_gl_copyRect,
	.copyRectFlip = psvita_gl_copyRectFlip,
	.copyRectRotate = psvita_gl_copyRectRotate,
	.drawTexture = psvita_gl_drawTexture,
	.commitTextureUpdates = psvita_gl_commitTextureUpdates,
	.uploadClut = psvita_gl_uploadClut,
	.writeIndexedTextureRect = psvita_gl_writeIndexedTextureRect,
	.writeDirectTextureRect = psvita_gl_writeDirectTextureRect,
	.blitSpriteVertices = psvita_gl_blitSpriteVertices,
	.blitPointVertices = psvita_gl_blitPointVertices,
	.enableDepthTest = psvita_gl_enableDepthTest,
	.disableDepthTest = psvita_gl_disableDepthTest,
	.clearDepthBuffer = psvita_gl_clearDepthBuffer,
	.clearColorBuffer = psvita_gl_clearColorBuffer,
	.drawUISprite = psvita_gl_drawUISprite,
	.drawUILine = psvita_gl_drawUILine,
	.drawUILineGradient = psvita_gl_drawUILineGradient,
	.drawUIRect = psvita_gl_drawUIRect,
	.fillUIRect = psvita_gl_fillUIRect,
	.fillUIRectGradient = psvita_gl_fillUIRectGradient,
	.setUIScissor = psvita_gl_setUIScissor,
	.flushAndWait = NULL,
	.getNativeContext = NULL,
};
