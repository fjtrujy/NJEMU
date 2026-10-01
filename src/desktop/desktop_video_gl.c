/******************************************************************************

	desktop_video_gl.c

	Desktop video backend on OpenGL 3.3 core (SDL2 window and context).

	It renders like the PS Vita backends, so it doubles as their test bench:
	the frame is recorded by common/hw_recorder while the core draws (indexed
	atlas + 555 CLUT rows, CPU clipping, quads per batch) and replayed straight
	into the back buffer by transferWorkFrame() with the same shaders and the
	same work frame -> canvas geometry (common/hw_render.h).  The canvas is
	the Vita's 960x544, letterboxed into the window.

	GL entry points are loaded through SDL_GL_GetProcAddress, so no GL loader
	or extra library is needed.  NJEMU_DUMP_FRAMES (desktop_frame_dump.h)
	re-renders the requested frames at 1x into an FBO and dumps them.

******************************************************************************/

#include <stdlib.h>
#include <string.h>

#include <SDL.h>
/* Prototypes are only used to type the pointers loaded from SDL. */
#define GL_GLEXT_PROTOTYPES 1
#include <SDL_opengl.h>

#include "emucfg.h"
#include "common/hw_recorder.h"
#include "common/video_driver.h"
#include "common/ui_draw_driver.h"
#include "common/video_geometry.h"
#include "desktop/desktop_frame_dump.h"

#define GLD_CANVAS_WIDTH	960
#define GLD_CANVAS_HEIGHT	544
#define GLD_WORK_WIDTH		SCR_WIDTH
#define GLD_WORK_HEIGHT		SCR_HEIGHT
#define GLD_UI_SCRATCH_HEIGHT	160
#define GLD_UI_UPLOAD_HEIGHT	64

#define GLD_MAX_TEXTURE_DIM	4096
#define GLD_MAX_PAGES		4
#define GLD_MAX_CHUNKS		32			/* vertex chunks per frame */
#define GLD_MAX_CLUTS		16			/* CLUT chunks per frame */
#define GLD_DRAW_MAX_QUADS	16384		/* 65536 vertices per indexed draw */

/******************************************************************************
	GL entry points
******************************************************************************/

#define GLD_FUNCS(X) \
	X(GetString) \
	X(GetError) \
	X(Viewport) \
	X(ClearColor) \
	X(ClearDepth) \
	X(Clear) \
	X(Enable) \
	X(Disable) \
	X(BlendFunc) \
	X(DepthFunc) \
	X(DepthMask) \
	X(Scissor) \
	X(PixelStorei) \
	X(ReadPixels) \
	X(Finish) \
	X(GenTextures) \
	X(DeleteTextures) \
	X(BindTexture) \
	X(ActiveTexture) \
	X(TexImage2D) \
	X(TexSubImage2D) \
	X(TexParameteri) \
	X(GenBuffers) \
	X(DeleteBuffers) \
	X(BindBuffer) \
	X(BufferData) \
	X(GenVertexArrays) \
	X(DeleteVertexArrays) \
	X(BindVertexArray) \
	X(EnableVertexAttribArray) \
	X(VertexAttribPointer) \
	X(CreateShader) \
	X(DeleteShader) \
	X(ShaderSource) \
	X(CompileShader) \
	X(GetShaderiv) \
	X(GetShaderInfoLog) \
	X(CreateProgram) \
	X(DeleteProgram) \
	X(AttachShader) \
	X(BindAttribLocation) \
	X(LinkProgram) \
	X(GetProgramiv) \
	X(GetProgramInfoLog) \
	X(UseProgram) \
	X(GetUniformLocation) \
	X(Uniform1i) \
	X(Uniform4f) \
	X(DrawElementsBaseVertex) \
	X(DrawArrays) \
	X(GenFramebuffers) \
	X(DeleteFramebuffers) \
	X(BindFramebuffer) \
	X(FramebufferTexture2D) \
	X(GenRenderbuffers) \
	X(DeleteRenderbuffers) \
	X(BindRenderbuffer) \
	X(RenderbufferStorage) \
	X(FramebufferRenderbuffer) \
	X(CheckFramebufferStatus)

static struct {
#define GLD_DECLARE(name) __typeof__(gl##name) *name;
	GLD_FUNCS(GLD_DECLARE)
#undef GLD_DECLARE
} gl;

static bool gld_load_functions(void)
{
#define GLD_LOAD(name) \
	gl.name = (__typeof__(gl.name))SDL_GL_GetProcAddress("gl" #name); \
	if (gl.name == NULL) { \
		printf("desktop_gl: missing gl" #name "\n"); \
		return false; \
	}
	GLD_FUNCS(GLD_LOAD)
#undef GLD_LOAD
	return true;
}


/******************************************************************************
	Shaders (GLSL port of psvita_shaders.h)
******************************************************************************/

static const char gld_vertex_src[] =
	"#version 330 core\n"
	"layout(location = 0) in vec2 aUV;\n"
	"layout(location = 1) in vec2 aPos;\n"
	"layout(location = 2) in vec2 aZP;\n"
	"uniform vec4 uRowX;\n"
	"uniform vec4 uRowY;\n"
	"uniform vec4 uTexScale;\n"
	"out vec2 vUV;\n"
	"out float vPal;\n"
	"void main() {\n"
	"	vec3 p = vec3(aPos, 1.0);\n"
	"	gl_Position = vec4(dot(uRowX.xyz, p), dot(uRowY.xyz, p),\n"
	"	                   aZP.x * (2.0 / 65535.0) - 1.0, 1.0);\n"
	"	vUV = aUV * uTexScale.xy;\n"
	"	vPal = (aZP.y + 0.5) * uTexScale.z;\n"
	"}\n";

/* The R8 atlas returns index/255: remap to the centre of CLUT texel `index`. */
static const char gld_indexed_src[] =
	"#version 330 core\n"
	"in vec2 vUV;\n"
	"in float vPal;\n"
	"uniform sampler2D uTex;\n"
	"uniform sampler2D uClut;\n"
	"out vec4 fragColor;\n"
	"void main() {\n"
	"	float i = texture(uTex, vUV).r;\n"
	"	fragColor = texture(uClut, vec2(i * (255.0 / 256.0) + (0.5 / 256.0), vPal));\n"
	"}\n";

static const char gld_indexed_depth_src[] =
	"#version 330 core\n"
	"in vec2 vUV;\n"
	"in float vPal;\n"
	"uniform sampler2D uTex;\n"
	"uniform sampler2D uClut;\n"
	"out vec4 fragColor;\n"
	"void main() {\n"
	"	float i = texture(uTex, vUV).r;\n"
	"	vec4 c = texture(uClut, vec2(i * (255.0 / 256.0) + (0.5 / 256.0), vPal));\n"
	"	if (c.a > 0.5)\n"
	"		discard;\n"
	"	fragColor = c;\n"
	"}\n";

static const char gld_direct_src[] =
	"#version 330 core\n"
	"in vec2 vUV;\n"
	"uniform sampler2D uTex;\n"
	"out vec4 fragColor;\n"
	"void main() {\n"
	"	fragColor = texture(uTex, vUV);\n"
	"}\n";


/******************************************************************************
	State
******************************************************************************/

typedef struct gld_page {
	GLuint tex;
	uint8_t *data;
	uint16_t width;
	uint16_t height;
} gld_page_t;

typedef struct gld_layer {
	uint8_t *cpu;				/* pointer handed to the core */
	GLuint tex;
	uint16_t width;
	uint16_t height;
	hw_layer_t hw;
	uint8_t bytes_per_pixel;
} gld_layer_t;

typedef struct gld_program {
	GLuint id;
	GLint u_row_x;
	GLint u_row_y;
	GLint u_tex_scale;
} gld_program_t;

typedef struct desktop_gl_video {
	SDL_Window *window;
	SDL_GLContext context;

	gld_layer_t *layers;
	uint8_t layer_count;
	gld_page_t pages[GLD_MAX_PAGES];
	uint8_t page_count;

	gld_program_t progs[HW_PROG_COUNT];
	GLuint vao;
	GLuint vbo;
	GLuint ibo;

	/* Frame memory handed to the recorder (CPU side, uploaded at present). */
	hw_vertex_t *vertices;
	uint8_t chunks_used;
	uint16_t *cluts;
	GLuint clut_tex[GLD_MAX_CLUTS];
	uint8_t cluts_used;

	hw_recorder_t rec;
	bool pending_flip;
	uint32_t draw_fill;
	uint32_t screen_fill;
	bool screen_fill_valid;

	/* 1x offscreen target for NJEMU_DUMP_FRAMES */
	GLuint dump_fbo;
	GLuint dump_color;
	GLuint dump_depth;
	uint32_t presented_frames;

	/* Statistics (NJEMU_VIDEO_STATS=1) */
	bool stats;
	uint32_t stat_frames;
	uint32_t stat_draws;
	uint32_t stat_quads;

	/* Immediate UI path. UI texture storage is CPU-owned by linear_ui_draw.c;
	 * OpenGL uploads each submitted region before drawing it. */
	GLuint ui_tex;
	GLuint ui_fill_tex;
	GLuint ui_vbo;
	RECT ui_clip;
	uint16_t *ui_scratch;
	GLuint ui_scratch_tex;
} desktop_gl_video_t;


/******************************************************************************
	Setup helpers
******************************************************************************/

static GLuint gld_compile(GLenum type, const char *source)
{
	GLuint shader = gl.CreateShader(type);
	GLint ok = GL_FALSE;

	gl.ShaderSource(shader, 1, &source, NULL);
	gl.CompileShader(shader);
	gl.GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		gl.GetShaderInfoLog(shader, sizeof(log), NULL, log);
		printf("desktop_gl: shader compilation failed: %s\n", log);
		gl.DeleteShader(shader);
		return 0;
	}
	return shader;
}

static bool gld_create_programs(desktop_gl_video_t *g)
{
	static const char *const fragments[HW_PROG_COUNT] = {
		gld_indexed_src, gld_indexed_depth_src, gld_direct_src,
	};
	GLuint vs = gld_compile(GL_VERTEX_SHADER, gld_vertex_src);
	if (vs == 0)
		return false;

	for (int i = 0; i < HW_PROG_COUNT; i++) {
		GLuint fs = gld_compile(GL_FRAGMENT_SHADER, fragments[i]);
		GLint ok = GL_FALSE;
		if (fs == 0)
			return false;

		gld_program_t *p = &g->progs[i];
		p->id = gl.CreateProgram();
		gl.AttachShader(p->id, vs);
		gl.AttachShader(p->id, fs);
		gl.LinkProgram(p->id);
		gl.GetProgramiv(p->id, GL_LINK_STATUS, &ok);
		gl.DeleteShader(fs);
		if (!ok) {
			char log[1024];
			gl.GetProgramInfoLog(p->id, sizeof(log), NULL, log);
			printf("desktop_gl: program link failed: %s\n", log);
			return false;
		}

		p->u_row_x = gl.GetUniformLocation(p->id, "uRowX");
		p->u_row_y = gl.GetUniformLocation(p->id, "uRowY");
		p->u_tex_scale = gl.GetUniformLocation(p->id, "uTexScale");
		gl.UseProgram(p->id);
		gl.Uniform1i(gl.GetUniformLocation(p->id, "uTex"), 0);
		GLint u_clut = gl.GetUniformLocation(p->id, "uClut");
		if (u_clut >= 0)
			gl.Uniform1i(u_clut, 1);
	}
	gl.DeleteShader(vs);
	return true;
}

static void gld_texture_params(void)
{
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* 555 with bit 15 as (inverted) alpha: GL_RGBA + UNSIGNED_SHORT_1_5_5_5_REV. */
static GLuint gld_create_texture(int width, int height, bool indexed)
{
	GLuint tex;
	gl.GenTextures(1, &tex);
	gl.BindTexture(GL_TEXTURE_2D, tex);
	gld_texture_params();
	if (indexed)
		gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
	else
		gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, width, height, 0, GL_RGBA,
			GL_UNSIGNED_SHORT_1_5_5_5_REV, NULL);
	return tex;
}

static bool gld_create_layers(desktop_gl_video_t *g, const layer_texture_info_t *info,
							  uint8_t count)
{
	uint16_t page_height[GLD_MAX_PAGES] = { 0 };
	uint16_t page_width[GLD_MAX_PAGES] = { 0 };
	uint8_t layer_page[256];

	g->layers = calloc(count, sizeof(*g->layers));
	if (g->layers == NULL)
		return false;
	g->layer_count = count;

	/* Group indexed layers into pages: same width, stacked vertically. */
	for (uint8_t i = 0; i < count; i++) {
		gld_layer_t *layer = &g->layers[i];
		layer->width = (uint16_t)info[i].width;
		layer->height = (uint16_t)info[i].height;
		layer->bytes_per_pixel = info[i].bytes_per_pixel;
		if (layer->bytes_per_pixel != 1)
			continue;

		uint8_t p;
		for (p = 0; p < g->page_count; p++) {
			if (page_width[p] == layer->width &&
				page_height[p] + layer->height <= GLD_MAX_TEXTURE_DIM)
				break;
		}
		if (p == g->page_count) {
			if (g->page_count == GLD_MAX_PAGES)
				return false;
			page_width[p] = layer->width;
			g->page_count++;
		}
		layer_page[i] = p;
		layer->hw.row_offset = page_height[p];
		page_height[p] += layer->height;
	}

	for (uint8_t p = 0; p < g->page_count; p++) {
		gld_page_t *page = &g->pages[p];
		page->width = page_width[p];
		page->height = page_height[p];
		page->data = calloc((size_t)page->width * page->height, 1);
		if (page->data == NULL)
			return false;
		page->tex = gld_create_texture(page->width, page->height, true);
	}

	for (uint8_t i = 0; i < count; i++) {
		gld_layer_t *layer = &g->layers[i];

		if (layer->bytes_per_pixel == 1) {
			gld_page_t *page = &g->pages[layer_page[i]];
			layer->tex = page->tex;
			layer->cpu = page->data + (size_t)layer->hw.row_offset * page->width;
			layer->hw.tex_width = page->width;
			layer->hw.tex_height = page->height;
			layer->hw.indexed = true;
		} else if (layer->bytes_per_pixel == 2) {
			layer->cpu = calloc((size_t)layer->width * layer->height, 2);
			if (layer->cpu == NULL)
				return false;
			layer->tex = gld_create_texture(layer->width, layer->height, false);
			layer->hw.tex_width = layer->width;
			layer->hw.tex_height = layer->height;
			layer->hw.indexed = false;
		} else {
			return false;
		}
		/* The recorder only compares handles: use the GL name. */
		layer->hw.texture = (const void *)(uintptr_t)layer->tex;
	}
	return true;
}


/******************************************************************************
	Recorder memory
******************************************************************************/

static hw_vertex_t *gld_alloc_vertices(void *user, const void **handle)
{
	desktop_gl_video_t *g = user;
	if (g->chunks_used == GLD_MAX_CHUNKS)
		return NULL;
	uint32_t chunk = g->chunks_used++;
	/* Handle = first quad of the chunk + 1 (never NULL). */
	*handle = (const void *)(uintptr_t)(chunk * HW_CHUNK_QUADS + 1);
	return g->vertices + (size_t)chunk * HW_CHUNK_QUADS * 4;
}

static uint16_t *gld_alloc_clut(void *user, const void **handle)
{
	desktop_gl_video_t *g = user;
	if (g->cluts_used == GLD_MAX_CLUTS)
		return NULL;
	uint32_t chunk = g->cluts_used++;
	*handle = (const void *)(uintptr_t)(chunk + 1);
	return g->cluts + (size_t)chunk * HW_CLUT_ROWS * HW_CLUT_ROW_ENTRIES;
}

static const hw_recorder_ops_t gld_ops = { gld_alloc_vertices, gld_alloc_clut };

/* New frame: the previous recording has been presented, reuse its memory. */
static void gld_reset_frame(desktop_gl_video_t *g)
{
	g->chunks_used = 0;
	g->cluts_used = 0;
	hw_rec_reset(&g->rec);
}


/******************************************************************************
	Replay
******************************************************************************/

/* Uploads everything the recording references (atlases, CLUTs, vertices). */
static void gld_upload(desktop_gl_video_t *g)
{
	gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
	gl.ActiveTexture(GL_TEXTURE0);
	for (uint8_t p = 0; p < g->page_count; p++) {
		gl.BindTexture(GL_TEXTURE_2D, g->pages[p].tex);
		gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g->pages[p].width, g->pages[p].height,
			GL_RED, GL_UNSIGNED_BYTE, g->pages[p].data);
	}
	for (uint8_t i = 0; i < g->layer_count; i++) {
		gld_layer_t *layer = &g->layers[i];
		if (layer->bytes_per_pixel != 2)
			continue;
		gl.BindTexture(GL_TEXTURE_2D, layer->tex);
		gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, layer->width, layer->height,
			GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, layer->cpu);
	}
	for (uint8_t c = 0; c < g->cluts_used; c++) {
		gl.BindTexture(GL_TEXTURE_2D, g->clut_tex[c]);
		gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, HW_CLUT_ROW_ENTRIES, HW_CLUT_ROWS,
			GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV,
			g->cluts + (size_t)c * HW_CLUT_ROWS * HW_CLUT_ROW_ENTRIES);
	}

	gl.BindBuffer(GL_ARRAY_BUFFER, g->vbo);
	gl.BufferData(GL_ARRAY_BUFFER,
		(GLsizeiptr)g->chunks_used * HW_CHUNK_QUADS * 4 * sizeof(hw_vertex_t),
		g->vertices, GL_STREAM_DRAW);
}

static void gld_bind_vertex_buffer(desktop_gl_video_t *g, GLuint buffer)
{
	gl.BindVertexArray(g->vao);
	gl.BindBuffer(GL_ARRAY_BUFFER, buffer);
	gl.EnableVertexAttribArray(0);
	gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(hw_vertex_t),
		(const void *)offsetof(hw_vertex_t, u));
	gl.EnableVertexAttribArray(1);
	gl.VertexAttribPointer(1, 2, GL_SHORT, GL_FALSE, sizeof(hw_vertex_t),
		(const void *)offsetof(hw_vertex_t, x));
	gl.EnableVertexAttribArray(2);
	gl.VertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(hw_vertex_t),
		(const void *)offsetof(hw_vertex_t, z));
}

static void gld_set_depth(uint8_t mode)
{
	if (mode == HW_DEPTH_OFF) {
		gl.Disable(GL_DEPTH_TEST);
		return;
	}
	gl.Enable(GL_DEPTH_TEST);
	gl.DepthFunc(mode == HW_DEPTH_TEST ? GL_GEQUAL : GL_ALWAYS);
	gl.DepthMask(GL_TRUE);
}

static void gld_replay(desktop_gl_video_t *g, const float row_x[3], const float row_y[3])
{
	gld_bind_vertex_buffer(g, g->vbo);
	gl.Enable(GL_BLEND);
	gl.BlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA);

	for (uint32_t i = 0; i < g->rec.cmd_count; i++) {
		const hw_cmd_t *cmd = &g->rec.cmds[i];
		const gld_program_t *p = &g->progs[cmd->prog];
		GLuint clut = cmd->clut ? g->clut_tex[(uintptr_t)cmd->clut - 1] : 0;

		/* The depth-tested program discards instead of blending. */
		if (cmd->prog == HW_PROG_INDEXED_DEPTH)
			gl.Disable(GL_BLEND);
		else
			gl.Enable(GL_BLEND);
		gld_set_depth(cmd->depth);

		gl.ActiveTexture(GL_TEXTURE1);
		gl.BindTexture(GL_TEXTURE_2D, clut);
		gl.ActiveTexture(GL_TEXTURE0);
		gl.BindTexture(GL_TEXTURE_2D, cmd->texture ? (GLuint)(uintptr_t)cmd->texture : clut);

		gl.UseProgram(p->id);
		gl.Uniform4f(p->u_row_x, row_x[0], row_x[1], row_x[2], 0.0f);
		gl.Uniform4f(p->u_row_y, row_y[0], row_y[1], row_y[2], 0.0f);
		gl.Uniform4f(p->u_tex_scale, 1.0f / cmd->tex_w, 1.0f / cmd->tex_h,
			1.0f / HW_CLUT_ROWS, 0.0f);

		uint32_t first = (uint32_t)((uintptr_t)cmd->vertices - 1) + cmd->first_quad;
		uint32_t quads = cmd->quads;
		while (quads) {
			uint32_t n = quads > GLD_DRAW_MAX_QUADS ? GLD_DRAW_MAX_QUADS : quads;
			gl.DrawElementsBaseVertex(GL_TRIANGLES, (GLsizei)n * 6, GL_UNSIGNED_SHORT,
				NULL, (GLint)first * 4);
			g->stat_draws++;
			g->stat_quads += n;
			first += n;
			quads -= n;
		}
	}

	gl.Disable(GL_DEPTH_TEST);
	gl.Disable(GL_BLEND);
}

/* Re-renders the recording at 1x into the dump FBO and writes the src rect. */
static void gld_dump(desktop_gl_video_t *g, const RECT *src)
{
	const int w = src->right - src->left, h = src->bottom - src->top;
	uint8_t *pixels = malloc((size_t)w * h * 4);
	if (pixels == NULL)
		return;

	/* Identity: work frame pixels straight onto a work-frame-sized target. */
	const float row_x[3] = { 2.0f / GLD_WORK_WIDTH, 0.0f, -1.0f };
	const float row_y[3] = { 0.0f, -2.0f / GLD_WORK_HEIGHT, 1.0f };

	gl.BindFramebuffer(GL_FRAMEBUFFER, g->dump_fbo);
	gl.Viewport(0, 0, GLD_WORK_WIDTH, GLD_WORK_HEIGHT);
	gl.Disable(GL_SCISSOR_TEST);
	gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	gl.ClearDepth(1.0);
	gl.DepthMask(GL_TRUE);
	gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	gld_replay(g, row_x, row_y);

	gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
	gl.ReadPixels(src->left, GLD_WORK_HEIGHT - src->bottom, w, h, GL_RGBA,
		GL_UNSIGNED_BYTE, pixels);
	desktop_dump_write("gl", g->presented_frames, pixels, w, h, w * 4, true);
	free(pixels);

	gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void gld_present(desktop_gl_video_t *g, const RECT *src, const RECT *dst, int orient)
{
	hw_xform_t m;
	RECT d;
	float row_x[3], row_y[3];
	int win_w, win_h;

	hw_rec_flush(&g->rec);
	if (!hw_present_geometry(src, dst, orient, GLD_CANVAS_WIDTH, GLD_CANVAS_HEIGHT, &d, &m))
		return;
	hw_clip_rows(&m, GLD_CANVAS_WIDTH, GLD_CANVAS_HEIGHT, row_x, row_y);

	gld_upload(g);
	if (desktop_dump_wanted(++g->presented_frames))
		gld_dump(g, src);

	/* Letterbox the 960x544 canvas into the window. */
	SDL_GL_GetDrawableSize(g->window, &win_w, &win_h);
	float scale = (float)win_w / GLD_CANVAS_WIDTH;
	if ((float)win_h / GLD_CANVAS_HEIGHT < scale)
		scale = (float)win_h / GLD_CANVAS_HEIGHT;
	const int vp_w = (int)(GLD_CANVAS_WIDTH * scale), vp_h = (int)(GLD_CANVAS_HEIGHT * scale);
	const int vp_x = (win_w - vp_w) / 2, vp_y = (win_h - vp_h) / 2;

	gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
	gl.Viewport(0, 0, win_w, win_h);
	gl.Disable(GL_SCISSOR_TEST);
	gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	gl.ClearDepth(1.0);
	gl.DepthMask(GL_TRUE);
	gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/* Only the destination rectangle shows the work frame. */
	gl.Viewport(vp_x, vp_y, vp_w, vp_h);
	gl.Enable(GL_SCISSOR_TEST);
	gl.Scissor(vp_x + (int)(d.left * scale), vp_y + (int)((GLD_CANVAS_HEIGHT - d.bottom) * scale),
		(int)((d.right - d.left) * scale), (int)((d.bottom - d.top) * scale));
	gld_replay(g, row_x, row_y);
	gl.Disable(GL_SCISSOR_TEST);

	/* Also dump what the window shows (scaling, letterbox, orientation). */
	if (desktop_dump_wanted(g->presented_frames)) {
		uint8_t *pixels = malloc((size_t)win_w * win_h * 4);
		if (pixels != NULL) {
			gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
			gl.ReadPixels(0, 0, win_w, win_h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
			desktop_dump_write("glscreen", g->presented_frames, pixels, win_w, win_h,
				win_w * 4, true);
			free(pixels);
		}
	}
}


/******************************************************************************
	Driver: lifetime
******************************************************************************/

static void desktop_gl_free(void *data);

static void *desktop_gl_init(layer_texture_info_t *layer_textures, uint8_t layer_textures_count,
							 clut_info_t *clut_info)
{
	desktop_gl_video_t *g = calloc(1, sizeof(*g));
	char title[256];

	if (g == NULL)
		return NULL;

	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

	snprintf(title, sizeof(title), "%s %s (OpenGL)", APPNAME_STR, VERSION_STR);
	g->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		GLD_CANVAS_WIDTH, GLD_CANVAS_HEIGHT,
		SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (g->window == NULL) {
		printf("desktop_gl: cannot create window: %s\n", SDL_GetError());
		goto fail;
	}
	g->context = SDL_GL_CreateContext(g->window);
	if (g->context == NULL) {
		printf("desktop_gl: cannot create GL 3.3 context: %s\n", SDL_GetError());
		goto fail;
	}
	if (!gld_load_functions())
		goto fail;
	SDL_GL_SetSwapInterval(1);
	printf("desktop_gl: %s / %s\n", gl.GetString(GL_RENDERER), gl.GetString(GL_VERSION));

	if (!gld_create_programs(g) || !gld_create_layers(g, layer_textures, layer_textures_count))
		goto fail;

	g->ui_clip = (RECT){ 0, 0, GLD_CANVAS_WIDTH, GLD_CANVAS_HEIGHT };
	g->draw_fill = 0xff000000u;

	/* UI_TEXTURE_FONT mixes RGBA4444 glyphs/shadows with RGB5551 assets.
	 * Keep the reusable upload target at RGBA8 so 4-bit alpha is preserved;
	 * allocating it as RGB5_A1 would quantize every 4444 alpha value to a
	 * single bit and make antialiased glyphs/glows visibly too bright. */
	gl.GenTextures(1, &g->ui_tex);
	gl.BindTexture(GL_TEXTURE_2D, g->ui_tex);
	gld_texture_params();
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, BUF_WIDTH, GLD_UI_UPLOAD_HEIGHT,
		0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	gl.GenTextures(1, &g->ui_fill_tex);
	gl.BindTexture(GL_TEXTURE_2D, g->ui_fill_tex);
	gld_texture_params();
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	gl.GenBuffers(1, &g->ui_vbo);
	g->ui_scratch = calloc((size_t)BUF_WIDTH * GLD_UI_SCRATCH_HEIGHT, sizeof(uint16_t));
	if (g->ui_scratch != NULL)
		g->ui_scratch_tex = gld_create_texture(BUF_WIDTH, GLD_UI_SCRATCH_HEIGHT, false);

	/* Recorder memory: vertex chunks and CLUT chunks, uploaded per present. */
	g->vertices = malloc((size_t)GLD_MAX_CHUNKS * HW_CHUNK_QUADS * 4 * sizeof(hw_vertex_t));
	g->cluts = malloc((size_t)GLD_MAX_CLUTS * HW_CLUT_CHUNK_BYTES);
	if (g->vertices == NULL || g->cluts == NULL)
		goto fail;
	for (int c = 0; c < GLD_MAX_CLUTS; c++)
		g->clut_tex[c] = gld_create_texture(HW_CLUT_ROW_ENTRIES, HW_CLUT_ROWS, false);

	/* Vertex layout of hw_vertex_t and a static quad index buffer. */
	gl.GenVertexArrays(1, &g->vao);
	gl.BindVertexArray(g->vao);
	gl.GenBuffers(1, &g->vbo);
	gld_bind_vertex_buffer(g, g->vbo);

	uint16_t *indices = malloc(GLD_DRAW_MAX_QUADS * 6 * sizeof(uint16_t));
	if (indices == NULL)
		goto fail;
	for (uint32_t q = 0; q < GLD_DRAW_MAX_QUADS; q++) {
		uint16_t *i = indices + q * 6;
		const uint16_t b = (uint16_t)(q * 4);
		i[0] = b; i[1] = b + 1; i[2] = b + 2;
		i[3] = b; i[4] = b + 2; i[5] = b + 3;
	}
	gl.GenBuffers(1, &g->ibo);
	gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->ibo);
	gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, GLD_DRAW_MAX_QUADS * 6 * sizeof(uint16_t),
		indices, GL_STATIC_DRAW);
	free(indices);

	/* 1x target for frame dumps. */
	gl.GenTextures(1, &g->dump_color);
	gl.BindTexture(GL_TEXTURE_2D, g->dump_color);
	gld_texture_params();
	gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLD_WORK_WIDTH, GLD_WORK_HEIGHT, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, NULL);
	gl.GenRenderbuffers(1, &g->dump_depth);
	gl.BindRenderbuffer(GL_RENDERBUFFER, g->dump_depth);
	gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, GLD_WORK_WIDTH, GLD_WORK_HEIGHT);
	gl.GenFramebuffers(1, &g->dump_fbo);
	gl.BindFramebuffer(GL_FRAMEBUFFER, g->dump_fbo);
	gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g->dump_color, 0);
	gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER,
		g->dump_depth);
	if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("desktop_gl: dump framebuffer incomplete\n");
		goto fail;
	}
	gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

	if (!hw_rec_init(&g->rec, &gld_ops, g, GLD_WORK_WIDTH, GLD_WORK_HEIGHT, clut_info))
		goto fail;

	const char *stats = getenv("NJEMU_VIDEO_STATS");
	g->stats = stats != NULL && stats[0] == '1';
	return g;

fail:
	desktop_gl_free(g);
	return NULL;
}

static void desktop_gl_free(void *data)
{
	desktop_gl_video_t *g = data;
	if (g == NULL)
		return;

	if (g->context != NULL) {
		gl.Finish();
		for (uint8_t i = 0; i < g->layer_count; i++) {
			if (g->layers[i].bytes_per_pixel == 2) {
				gl.DeleteTextures(1, &g->layers[i].tex);
				free(g->layers[i].cpu);
			}
		}
		for (uint8_t p = 0; p < g->page_count; p++) {
			gl.DeleteTextures(1, &g->pages[p].tex);
			free(g->pages[p].data);
		}
		gl.DeleteTextures(GLD_MAX_CLUTS, g->clut_tex);
		if (g->ui_tex) gl.DeleteTextures(1, &g->ui_tex);
		if (g->ui_fill_tex) gl.DeleteTextures(1, &g->ui_fill_tex);
		if (g->ui_vbo) gl.DeleteBuffers(1, &g->ui_vbo);
		if (g->ui_scratch_tex) gl.DeleteTextures(1, &g->ui_scratch_tex);
		gl.DeleteTextures(1, &g->dump_color);
		gl.DeleteRenderbuffers(1, &g->dump_depth);
		gl.DeleteFramebuffers(1, &g->dump_fbo);
		gl.DeleteBuffers(1, &g->vbo);
		gl.DeleteBuffers(1, &g->ibo);
		gl.DeleteVertexArrays(1, &g->vao);
		for (int i = 0; i < HW_PROG_COUNT; i++) {
			if (g->progs[i].id)
				gl.DeleteProgram(g->progs[i].id);
		}
		SDL_GL_DeleteContext(g->context);
	}
	if (g->window != NULL)
		SDL_DestroyWindow(g->window);

	hw_rec_free(&g->rec);
	free(g->vertices);
	free(g->cluts);
	free(g->layers);
	free(g->ui_scratch);
	free(g);
}


/******************************************************************************
	Driver: frame control
******************************************************************************/

static void desktop_gl_waitVsync(void *data)
{
	(void)data;
}

static void desktop_gl_flipScreen(void *data, bool vsync)
{
	desktop_gl_video_t *g = data;

	SDL_GL_SetSwapInterval(vsync ? 1 : 0);
	SDL_GL_SwapWindow(g->window);

	if (g->stats && ++g->stat_frames == 300) {
		const float n = (float)g->stat_frames;
		printf("desktop_gl frames=%u scenes/frame=1.00 draws/frame=%.1f quads/frame=%.0f clut_rows/frame=%.1f\n",
			(unsigned)g->stat_frames, g->stat_draws / n, g->stat_quads / n,
			g->rec.stat_clut_rows / n);
		g->stat_frames = g->stat_draws = g->stat_quads = g->rec.stat_clut_rows = 0;
	}
}

static void desktop_gl_beginFrame(void *data)
{
	desktop_gl_video_t *g = data;
	g->ui_clip = (RECT){ 0, 0, GLD_CANVAS_WIDTH, GLD_CANVAS_HEIGHT };
}

static void desktop_gl_endFrame(void *data)
{
	hw_rec_flush(&((desktop_gl_video_t *)data)->rec);
}

static void *desktop_gl_frameAddr(void *data, int frameIndex, int x, int y)
{
	desktop_gl_video_t *g = data;

	if (frameIndex != COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER || g->ui_scratch == NULL ||
		x < 0 || y < 0 || x >= BUF_WIDTH || y >= GLD_UI_SCRATCH_HEIGHT)
		return NULL;
	return g->ui_scratch + (size_t)y * BUF_WIDTH + x;
}

static int gld_read_work_rgba(desktop_gl_video_t *g, int x, int y, int width, int height,
	uint8_t *pixels)
{
	const float row_x[3] = { 2.0f / GLD_WORK_WIDTH, 0.0f, -1.0f };
	const float row_y[3] = { 0.0f, -2.0f / GLD_WORK_HEIGHT, 1.0f };

	if (pixels == NULL || x < 0 || y < 0 || width <= 0 || height <= 0 ||
		x + width > GLD_WORK_WIDTH || y + height > GLD_WORK_HEIGHT)
		return 0;
	hw_rec_flush(&g->rec);
	gld_upload(g);
	gl.BindFramebuffer(GL_FRAMEBUFFER, g->dump_fbo);
	gl.Viewport(0, 0, GLD_WORK_WIDTH, GLD_WORK_HEIGHT);
	gl.Disable(GL_SCISSOR_TEST);
	gl.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	gl.ClearDepth(1.0);
	gl.DepthMask(GL_TRUE);
	gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	gld_replay(g, row_x, row_y);
	gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
	gl.ReadPixels(x, GLD_WORK_HEIGHT - (y + height), width, height, GL_RGBA,
		GL_UNSIGNED_BYTE, pixels);
	gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
	return 1;
}

static int desktop_gl_readFrame(void *data, int frameIndex, int x, int y, int width, int height,
	uint16_t *dst, int dstPitch)
{
	desktop_gl_video_t *g = data;

	if (dst == NULL || dstPitch < width || width <= 0 || height <= 0 || x < 0 || y < 0)
		return 0;
	if (frameIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER) {
		if (g->ui_scratch == NULL || x + width > BUF_WIDTH ||
			y + height > GLD_UI_SCRATCH_HEIGHT)
			return 0;
		for (int row = 0; row < height; row++)
			memcpy(dst + (size_t)row * dstPitch,
				g->ui_scratch + (size_t)(y + row) * BUF_WIDTH + x,
				(size_t)width * sizeof(uint16_t));
		return 1;
	}
	if (frameIndex == COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER) {
		uint8_t *rgba = malloc((size_t)width * height * 4);
		if (rgba == NULL || !gld_read_work_rgba(g, x, y, width, height, rgba)) {
			free(rgba);
			return 0;
		}
		for (int row = 0; row < height; row++) {
			/* glReadPixels is bottom-up. */
			const uint8_t *src = rgba + (size_t)(height - 1 - row) * width * 4;
			for (int col = 0; col < width; col++)
				dst[(size_t)row * dstPitch + col] =
					(uint16_t)(0x8000 | MAKECOL15(src[col * 4], src[col * 4 + 1], src[col * 4 + 2]));
		}
		free(rgba);
		return 1;
	}
	return 0;
}

/* The canvas plays the Vita display: the frame is laid out on 960x544. */
static void desktop_gl_getOutputSize(void *data, int *width, int *height)
{
	(void)data;
	*width = GLD_CANVAS_WIDTH;
	*height = GLD_CANVAS_HEIGHT;
}

static void desktop_gl_scissor(void *data, uint16_t left, uint16_t top, uint16_t right,
							   uint16_t bottom)
{
	hw_rec_set_clip(&((desktop_gl_video_t *)data)->rec, left, top, right, bottom);
}

static void desktop_gl_clearDisplay(uint32_t color)
{
	gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
	gl.Disable(GL_SCISSOR_TEST);
	gl.ClearColor((color & 0xff) / 255.0f, ((color >> 8) & 0xff) / 255.0f,
		((color >> 16) & 0xff) / 255.0f, 1.0f);
	gl.Clear(GL_COLOR_BUFFER_BIT);
}

static void desktop_gl_clearScreen(void *data)
{
	(void)data;
	desktop_gl_clearDisplay(0);
}

static void desktop_gl_clearFrame(void *data, int index)
{
	desktop_gl_video_t *g = data;

	/* SCREEN_BITMAP is the recorded emulator work frame. DRAW/SHOW are the
	 * display surface used by the common GUI. */
	if (index == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP)
		hw_rec_fill(&g->rec, &g->rec.work, 0, HW_DEPTH_OFF);
	else if (index == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER) {
		if (g->ui_scratch != NULL)
			memset(g->ui_scratch, 0, (size_t)BUF_WIDTH * GLD_UI_SCRATCH_HEIGHT * sizeof(uint16_t));
	} else {
		desktop_gl_clearDisplay(0);
		g->draw_fill = 0xff000000u;
	}
}

static void desktop_gl_fillFrame(void *data, int frameIndex, uint32_t color)
{
	desktop_gl_video_t *g = data;

	if (frameIndex == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP)
		hw_rec_fill(&g->rec, &g->rec.work, hw_rgba_to_555(color), HW_DEPTH_OFF);
	else {
		desktop_gl_clearDisplay(color);
		g->draw_fill = color | 0xff000000u;
	}
}

static void desktop_gl_startWorkFrame(void *data, uint32_t color)
{
	desktop_gl_video_t *g = data;
	gld_reset_frame(g);
	g->pending_flip = false;
	g->screen_fill_valid = false;
	hw_rec_begin_work(&g->rec, hw_rgba_to_555(color));
}

static void gld_ui_fill(desktop_gl_video_t *g, int x, int y, int w, int h,
	uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3);

static void desktop_gl_transferWorkFrame(void *data, RECT *src_rect, RECT *dst_rect)
{
	desktop_gl_video_t *g = data;

	/* The common GUI caches a solid background in SCREEN_BITMAP. Keep that
	 * cache independent of the per-frame emulator recorder so it survives
	 * flipScreen(). */
	if (g->screen_fill_valid) {
		gld_ui_fill(g, dst_rect->left, dst_rect->top,
			dst_rect->right - dst_rect->left, dst_rect->bottom - dst_rect->top,
			g->screen_fill, g->screen_fill, g->screen_fill, g->screen_fill);
		return;
	}
	gld_present(g, src_rect, dst_rect, HW_ORIENT_NORMAL);
}

static int gld_capture_to_scratch(desktop_gl_video_t *g, const RECT *src_rect,
	const RECT *dst_rect, bool rotate)
{
	const int sw = src_rect->right - src_rect->left;
	const int sh = src_rect->bottom - src_rect->top;
	const int dw = dst_rect->right - dst_rect->left;
	const int dh = dst_rect->bottom - dst_rect->top;
	uint8_t *rgba;

	if (g->ui_scratch == NULL || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 ||
		dst_rect->left < 0 || dst_rect->top < 0 || dst_rect->right > BUF_WIDTH ||
		dst_rect->bottom > GLD_UI_SCRATCH_HEIGHT)
		return 0;
	rgba = malloc((size_t)sw * sh * 4);
	if (rgba == NULL)
		return 0;
	if (!gld_read_work_rgba(g, src_rect->left, src_rect->top, sw, sh, rgba)) {
		free(rgba);
		return 0;
	}

	for (int y = 0; y < dh; y++) {
		uint16_t *dst = g->ui_scratch +
			(size_t)(dst_rect->top + y) * BUF_WIDTH + dst_rect->left;
		for (int x = 0; x < dw; x++) {
			int sx, sy;
			if (rotate) {
				sx = (y * sw) / dh;
				sy = sh - 1 - (x * sh) / dw;
			} else {
				sx = (x * sw) / dw;
				sy = (y * sh) / dh;
			}
			const uint8_t *c = rgba + ((size_t)(sh - 1 - sy) * sw + sx) * 4;
			dst[x] = (uint16_t)(0x8000 | MAKECOL15(c[0], c[1], c[2]));
		}
	}
	free(rgba);
	return 1;
}

static void gld_draw_scratch(desktop_gl_video_t *g, const RECT *src_rect, const RECT *dst_rect);

static void desktop_gl_copyRect(void *data, int srcIndex, int dstIndex, RECT *src_rect,
								RECT *dst_rect)
{
	desktop_gl_video_t *g = data;
	const bool dst_display = dstIndex == COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER ||
		dstIndex == COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER;

	if (srcIndex == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP) {
		if (dstIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER)
			gld_capture_to_scratch(g, src_rect, dst_rect, false);
		else if (dst_display)
			desktop_gl_transferWorkFrame(g, src_rect, dst_rect);
	} else if (srcIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER && dst_display) {
		gld_draw_scratch(g, src_rect, dst_rect);
	} else if (srcIndex == COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER &&
			dstIndex == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP) {
		/* GUI background caching. Without UI_DRAW_CAP_CACHE_CHROME the cached
		 * content is exactly the solid fill selected by load_background(). */
		if (!g->pending_flip) {
			g->screen_fill = g->draw_fill;
			g->screen_fill_valid = true;
		}
	}
}


static void desktop_gl_copyRectFlip(void *data, int srcIndex, int dstIndex, RECT *src_rect,
									RECT *dst_rect)
{
	desktop_gl_video_t *g = data;
	(void)dstIndex;

	if (srcIndex != COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP)
		return;

	/* CPS rotate+flip flips the work frame in place first (src == dst). */
	if (src_rect->left == dst_rect->left && src_rect->top == dst_rect->top &&
		src_rect->right == dst_rect->right && src_rect->bottom == dst_rect->bottom) {
		g->pending_flip = true;
		return;
	}
	gld_present(g, src_rect, dst_rect, HW_ORIENT_FLIP);
}

static void desktop_gl_copyRectRotate(void *data, int srcIndex, int dstIndex, RECT *src_rect,
									  RECT *dst_rect)
{
	desktop_gl_video_t *g = data;
	(void)dstIndex;

	if (srcIndex != COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP)
		return;
	if (dstIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER) {
		gld_capture_to_scratch(g, src_rect, dst_rect, true);
		return;
	}
	gld_present(g, src_rect, dst_rect,
		g->pending_flip ? HW_ORIENT_ROTATE_FLIP : HW_ORIENT_ROTATE);
	g->pending_flip = false;
}

static void desktop_gl_drawTexture(void *data, int srcIndex, int dstIndex,
								   RECT *src_rect, RECT *dst_rect)
{
	if (srcIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER &&
		(dstIndex == COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER ||
		 dstIndex == COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER))
		gld_draw_scratch(data, src_rect, dst_rect);
}



/******************************************************************************
	Driver: drawing
******************************************************************************/

static void desktop_gl_uploadMem(void *data, uint8_t textureIndex)
{
	/* Every referenced texture is uploaded once per present. */
	(void)data;
	(void)textureIndex;
}

/* Copies texels into the CPU copy of a layer, uploaded when a present uses it. */
static void gld_write_rect(desktop_gl_video_t *g, uint8_t textureIndex, uint8_t bpp,
						   int x, int y, int width, int height,
						   const void *pixels, int srcPitch)
{
	const gld_layer_t *layer;
	const uint8_t *src = pixels;

	if (textureIndex >= g->layer_count || g->layers[textureIndex].bytes_per_pixel != bpp
		|| pixels == NULL || x < 0 || y < 0 || width <= 0 || height <= 0)
		return;
	layer = &g->layers[textureIndex];
	if (x + width > layer->width)
		width = layer->width - x;
	if (y + height > layer->height)
		height = layer->height - y;
	for (int row = 0; row < height; row++)
		memcpy(layer->cpu + ((size_t)(y + row) * layer->width + x) * bpp,
			src + (size_t)row * srcPitch * bpp, (size_t)width * bpp);
}

static void desktop_gl_writeIndexedTextureRect(void *data, uint8_t textureIndex,
											   int x, int y, int width, int height,
											   const uint8_t *pixels, int srcPitch)
{
	gld_write_rect(data, textureIndex, 1, x, y, width, height, pixels, srcPitch);
}

static void desktop_gl_writeDirectTextureRect(void *data, uint8_t textureIndex,
											  int x, int y, int width, int height,
											  const uint16_t *pixels, int srcPitch)
{
	gld_write_rect(data, textureIndex, 2, x, y, width, height, pixels, srcPitch);
}

static void desktop_gl_uploadClut(void *data, uint16_t *bank, uint8_t bank_index)
{
	/* CLUT windows are captured lazily per batch by the recorder. */
	(void)data;
	(void)bank;
	(void)bank_index;
}

static void desktop_gl_blitSpriteVertices(void *data, uint8_t textureIndex,
										  const uint16_t *clut, uint8_t bank_index,
										  uint32_t vertices_count,
										  const video_sprite_vertex_t *vertices)
{
	desktop_gl_video_t *g = data;
	(void)bank_index;

	if (textureIndex >= g->layer_count)
		return;
	hw_rec_blit(&g->rec, &g->layers[textureIndex].hw, clut, vertices, vertices_count);
}

static void desktop_gl_blitPointVertices(void *data, uint32_t points_count,
										 const video_point_vertex_t *vertices)
{
	hw_rec_points(&((desktop_gl_video_t *)data)->rec, vertices, points_count);
}


static void desktop_gl_enableDepthTest(void *data)
{
	hw_rec_set_depth_test(&((desktop_gl_video_t *)data)->rec, true);
}

static void desktop_gl_disableDepthTest(void *data)
{
	hw_rec_set_depth_test(&((desktop_gl_video_t *)data)->rec, false);
}

static void desktop_gl_clearDepthBuffer(void *data)
{
	desktop_gl_video_t *g = data;

	/* PSP semantics: depth cleared to 0, GEQUAL test.  Colour untouched. */
	hw_rec_fill(&g->rec, &g->rec.work, 0x8000, HW_DEPTH_WRITE);
}

static void desktop_gl_clearColorBuffer(void *data)
{
	desktop_gl_video_t *g = data;

	/* Clears the colour inside the current scissor, keeping depth. */
	hw_rec_fill(&g->rec, &g->rec.clip, 0, HW_DEPTH_OFF);
}

static void gld_ui_setup(desktop_gl_video_t *g, GLuint texture, int tex_w, int tex_h,
                         const hw_vertex_t *vertices, int count, GLenum mode, int blend)
{
	const gld_program_t *p = &g->progs[HW_PROG_DIRECT];

	gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
	{
		int win_w, win_h;
		SDL_GL_GetDrawableSize(g->window, &win_w, &win_h);
		float scale = (float)win_w / GLD_CANVAS_WIDTH;
		if ((float)win_h / GLD_CANVAS_HEIGHT < scale) scale = (float)win_h / GLD_CANVAS_HEIGHT;
		int vp_w = (int)(GLD_CANVAS_WIDTH * scale), vp_h = (int)(GLD_CANVAS_HEIGHT * scale);
		gl.Viewport((win_w - vp_w) / 2, (win_h - vp_h) / 2, vp_w, vp_h);
	}
	gl.Disable(GL_DEPTH_TEST);
	gl.Disable(GL_SCISSOR_TEST);
	if (blend) {
		gl.Enable(GL_BLEND);
		gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	} else {
		gl.Disable(GL_BLEND);
	}
	gl.UseProgram(p->id);
	gl.Uniform4f(p->u_row_x, 2.0f / GLD_CANVAS_WIDTH, 0.0f, -1.0f, 0.0f);
	gl.Uniform4f(p->u_row_y, 0.0f, -2.0f / GLD_CANVAS_HEIGHT, 1.0f, 0.0f);
	gl.Uniform4f(p->u_tex_scale, 1.0f / tex_w, 1.0f / tex_h, 0.0f, 0.0f);
	gl.ActiveTexture(GL_TEXTURE0);
	gl.BindTexture(GL_TEXTURE_2D, texture);
	gld_bind_vertex_buffer(g, g->ui_vbo);
	gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * sizeof(*vertices), vertices, GL_STREAM_DRAW);
	gl.DrawArrays(mode, 0, count);
}

static bool gld_ui_quad(const RECT *clip, hw_vertex_t q[4], int x0, int y0, int x1, int y1,
                        float u0, float v0, float u1, float v1)
{
	if (!hw_clip_quad(clip, &x0, &y0, &x1, &y1, &u0, &v0, &u1, &v1))
		return false;
	hw_write_quad(q, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1,
		u0, v0, u1, v1, 0, 0);
	return true;
}

static void gld_draw_scratch(desktop_gl_video_t *g, const RECT *src_rect, const RECT *dst_rect)
{
	hw_vertex_t q[4];

	if (g->ui_scratch == NULL || g->ui_scratch_tex == 0 ||
		!gld_ui_quad(&g->ui_clip, q, dst_rect->left, dst_rect->top,
			dst_rect->right, dst_rect->bottom, src_rect->left, src_rect->top,
			src_rect->right, src_rect->bottom))
		return;
	gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
	gl.ActiveTexture(GL_TEXTURE0);
	gl.BindTexture(GL_TEXTURE_2D, g->ui_scratch_tex);
	gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, BUF_WIDTH, GLD_UI_SCRATCH_HEIGHT,
		GL_RGBA, GL_UNSIGNED_SHORT_1_5_5_5_REV, g->ui_scratch);
	gld_ui_setup(g, g->ui_scratch_tex, BUF_WIDTH, GLD_UI_SCRATCH_HEIGHT, q, 4,
		GL_TRIANGLE_FAN, 0);
}

static void desktop_gl_drawUISprite(void *data, void *tex, int tex_format, int tex_swizzled,
                                    int tex_width, int tex_height, int tex_stride,
                                    int su, int sv, int sw, int sh,
                                    int dx, int dy, int dw, int dh, int blend)
{
	desktop_gl_video_t *g = data;
	hw_vertex_t q[4];
	GLenum type;
	const uint16_t *src;
	(void)tex_swizzled;
	(void)tex_width;

	if (tex == NULL || sw <= 0 || sh <= 0 || su < 0 || sv < 0 ||
		su + sw > tex_stride || sv + sh > tex_height ||
		sw > BUF_WIDTH || sh > GLD_UI_UPLOAD_HEIGHT ||
		!gld_ui_quad(&g->ui_clip, q, dx, dy, dx + dw, dy + dh, 0, 0, sw, sh))
		return;

	/* The common linear UI storage uses the PSP 16-bit layouts. Upload the
	 * requested rectangle into a reusable atlas region in one GL transfer.
	 * GL_UNPACK_ROW_LENGTH preserves the source pitch without per-row uploads. */
	type = tex_format == UI_PIXFMT_4444
		? GL_UNSIGNED_SHORT_4_4_4_4_REV
		: GL_UNSIGNED_SHORT_1_5_5_5_REV;
	src = (const uint16_t *)tex + (size_t)sv * tex_stride + su;
	gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
	gl.PixelStorei(GL_UNPACK_ROW_LENGTH, tex_stride);
	gl.ActiveTexture(GL_TEXTURE0);
	gl.BindTexture(GL_TEXTURE_2D, g->ui_tex);
	gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sw, sh, GL_RGBA, type, src);
	gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	gld_ui_setup(g, g->ui_tex, BUF_WIDTH, GLD_UI_UPLOAD_HEIGHT, q, 4, GL_TRIANGLE_FAN, blend);
}

static void gld_ui_fill(desktop_gl_video_t *g, int x, int y, int w, int h,
                        uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3)
{
	hw_vertex_t q[4];
	uint32_t pixels[4] = { c0, c1, c2, c3 };
	if (w <= 0 || h <= 0 ||
		!gld_ui_quad(&g->ui_clip, q, x, y, x + w, y + h, 0.5f, 0.5f, 1.5f, 1.5f))
		return;
	gl.ActiveTexture(GL_TEXTURE0);
	gl.BindTexture(GL_TEXTURE_2D, g->ui_fill_tex);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	gld_ui_setup(g, g->ui_fill_tex, 2, 2, q, 4, GL_TRIANGLE_FAN, 1);
}

static void desktop_gl_drawUILine(void *data, int x1, int y1, int x2, int y2, uint32_t color)
{
	desktop_gl_video_t *g = data;
	if (y1 == y2) {
		if (x2 < x1) { int t = x1; x1 = x2; x2 = t; }
		gld_ui_fill(g, x1, y1, x2 - x1, 1, color, color, color, color);
	} else if (x1 == x2) {
		if (y2 < y1) { int t = y1; y1 = y2; y2 = t; }
		gld_ui_fill(g, x1, y1, 1, y2 - y1, color, color, color, color);
	} else {
		/* Preserve diagonal UI separators with the same immediate path. */
		hw_vertex_t v[2] = { { .u=0.5f,.v=0.5f,.x=x1,.y=y1 }, { .u=0.5f,.v=0.5f,.x=x2,.y=y2 } };
		uint32_t pixels[4] = { color, color, color, color };
		gl.BindTexture(GL_TEXTURE_2D, g->ui_fill_tex);
		gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		gld_ui_setup(g, g->ui_fill_tex, 2, 2, v, 2, GL_LINES, 1);
	}
}

static void desktop_gl_drawUILineGradient(void *data, int x1, int y1, int x2, int y2,
                                          uint32_t color1, uint32_t color2)
{
	/* Axis-aligned gradients cover all current chrome/progress uses. */
	if (y1 == y2) {
		if (x2 < x1) { int t=x1; x1=x2; x2=t; uint32_t c=color1; color1=color2; color2=c; }
		gld_ui_fill(data, x1, y1, x2 - x1, 1, color1, color2, color1, color2);
	} else if (x1 == x2) {
		if (y2 < y1) { int t=y1; y1=y2; y2=t; uint32_t c=color1; color1=color2; color2=c; }
		gld_ui_fill(data, x1, y1, 1, y2 - y1, color1, color1, color2, color2);
	} else {
		desktop_gl_drawUILine(data, x1, y1, x2, y2, color1);
	}
}

static void desktop_gl_drawUIRect(void *data, int x, int y, int w, int h, uint32_t color)
{
	gld_ui_fill(data, x, y, w, 1, color, color, color, color);
	gld_ui_fill(data, x, y + h - 1, w, 1, color, color, color, color);
	gld_ui_fill(data, x, y, 1, h, color, color, color, color);
	gld_ui_fill(data, x + w - 1, y, 1, h, color, color, color, color);
}

static void desktop_gl_fillUIRect(void *data, int x, int y, int w, int h, uint32_t color)
{
	gld_ui_fill(data, x, y, w, h, color, color, color, color);
}

static void desktop_gl_fillUIRectGradient(void *data, int x, int y, int w, int h,
                                          uint32_t color1, uint32_t color2, int direction)
{
	if (direction == UI_GRADIENT_HORIZONTAL)
		gld_ui_fill(data, x, y, w, h, color1, color2, color1, color2);
	else
		gld_ui_fill(data, x, y, w, h, color1, color1, color2, color2);
}

static void desktop_gl_setUIScissor(void *data, int x, int y, int w, int h)
{
	desktop_gl_video_t *g = data;
	int x1 = x + w, y1 = y + h;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x1 > GLD_CANVAS_WIDTH) x1 = GLD_CANVAS_WIDTH;
	if (y1 > GLD_CANVAS_HEIGHT) y1 = GLD_CANVAS_HEIGHT;
	g->ui_clip = (RECT){ x, y, x1, y1 };
}

video_driver_t video_desktop_gl = {
	.ident = "desktop_gl",
	.init = desktop_gl_init,
	.free = desktop_gl_free,
	.waitVsync = desktop_gl_waitVsync,
	.flipScreen = desktop_gl_flipScreen,
	.beginFrame = desktop_gl_beginFrame,
	.endFrame = desktop_gl_endFrame,
	.frameAddr = desktop_gl_frameAddr,
	.readFrame = desktop_gl_readFrame,
	.getOutputSize = desktop_gl_getOutputSize,
	.scissor = desktop_gl_scissor,
	.clearScreen = desktop_gl_clearScreen,
	.clearFrame = desktop_gl_clearFrame,
	.fillFrame = desktop_gl_fillFrame,
	.startWorkFrame = desktop_gl_startWorkFrame,
	.transferWorkFrame = desktop_gl_transferWorkFrame,
	.copyRect = desktop_gl_copyRect,
	.copyRectFlip = desktop_gl_copyRectFlip,
	.copyRectRotate = desktop_gl_copyRectRotate,
	.drawTexture = desktop_gl_drawTexture,
	.uploadMem = desktop_gl_uploadMem,
	.uploadClut = desktop_gl_uploadClut,
	.writeIndexedTextureRect = desktop_gl_writeIndexedTextureRect,
	.writeDirectTextureRect = desktop_gl_writeDirectTextureRect,
	.blitSpriteVertices = desktop_gl_blitSpriteVertices,
	.blitPointVertices = desktop_gl_blitPointVertices,
	.enableDepthTest = desktop_gl_enableDepthTest,
	.disableDepthTest = desktop_gl_disableDepthTest,
	.clearDepthBuffer = desktop_gl_clearDepthBuffer,
	.clearColorBuffer = desktop_gl_clearColorBuffer,
	.drawUISprite = desktop_gl_drawUISprite,
	.drawUILine = desktop_gl_drawUILine,
	.drawUILineGradient = desktop_gl_drawUILineGradient,
	.drawUIRect = desktop_gl_drawUIRect,
	.fillUIRect = desktop_gl_fillUIRect,
	.fillUIRectGradient = desktop_gl_fillUIRectGradient,
	.setUIScissor = desktop_gl_setUIScissor,
};
