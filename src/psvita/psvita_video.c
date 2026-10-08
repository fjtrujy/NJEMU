/******************************************************************************

    psvita_video.c

    PS Vita video backend: vita2d for the GXM context, display and texture
    allocation; direct GXM for everything the emulator draws.  It renders
    exactly like the vitaGL backend (psvita_video_gl.c), with the same
    shaders (psvita_shaders.h) and geometry (psvita_video_common.h):

    - One GXM scene per frame.  Draws are recorded while the core renders
      and replayed straight into the display back buffer by
      transferWorkFrame(), which knows the destination rectangle and the
      orientation.  No off-screen work target.

    - GPU-visible textures.  Indexed layers are stacked into one U8 texture
      that writeIndexedTextureRect() fills in place on a cache miss (no
      upload pass); CPS1 SCROLLH is a native U1U5U5U5 texture.

    - Palettes are raw 555 rows sampled from the shader, copied only when a
      256-entry window changes, and selected per vertex so palette changes do
      not split draws.

    - Vertices and palette rows live in a triple-buffered ring of GPU-mapped
      memory; each scene ends with a GXM notification that fences the reuse
      of its segment, so the CPU never waits for an idle GPU.

    NJEMU uses bit 15 of a 555 colour as the "transparent" marker.  Blending
    is inverted (dst = src * (1 - a) + dst * a) instead of rewriting colours,
    and the depth-tested CPS2 path discards.

******************************************************************************/

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <vita2d.h>

#include "common/hw_recorder.h"
#include "common/ui_draw_driver.h"
#include "common/video_driver.h"
#include "common/video_geometry.h"
#include "psvita_shaders.h"
#include "psvita_video_common.h"

#define V2D_WORK_WIDTH		SCR_WIDTH
#define V2D_WORK_HEIGHT		SCR_HEIGHT

#define V2D_MAX_TEXTURE_DIM	4096
#define V2D_MAX_PAGES		4

/* Triple-buffered transient memory, one segment per presented frame. */
#define V2D_SEGMENTS		3
#define V2D_SEG_QUADS		(4 * HW_CHUNK_QUADS)
#define V2D_CLUT_CHUNKS		2			/* chunks per segment */
#define V2D_SEG_VTX_BYTES	(V2D_SEG_QUADS * 4 * sizeof(hw_vertex_t))
#define V2D_SEG_UI_OFFSET	(V2D_SEG_VTX_BYTES + V2D_CLUT_CHUNKS * HW_CLUT_CHUNK_BYTES)
#define V2D_SEG_UI_BYTES	(1024 * 1024)		/* UI vertices and texel copies */
#define V2D_SEG_BYTES		(V2D_SEG_UI_OFFSET + V2D_SEG_UI_BYTES)

/* CPU surface returned for COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER (GUI). */
#define V2D_SCRATCH_WIDTH	BUF_WIDTH
#define V2D_SCRATCH_HEIGHT	SCR_HEIGHT

/* UI texel atlas (psvita_video_common.h): 1 MB of 16-bit texels. */
#define V2D_UI_ATLAS_WIDTH	1024
#define V2D_UI_ATLAS_HEIGHT	512

/* One draw covers at most 65536 vertices of the static quad index buffer. */
#define V2D_DRAW_MAX_QUADS	16384

/* Notification region slots used to fence the ring segments. */
#define V2D_NOTIFY_SLOT		500

typedef struct v2d_page {
    vita2d_texture *texture;
    uint8_t *data;
    uint16_t width;
    uint16_t height;
} v2d_page_t;

typedef struct v2d_layer {
    uint8_t *cpu;				/* pointer handed to the core */
    vita2d_texture *texture;
    uint16_t width;
    uint16_t height;
    hw_layer_t hw;				/* texture handle: &texture->gxm_tex */
    uint8_t bytes_per_pixel;
} v2d_layer_t;

typedef struct psvita_video {
    v2d_layer_t *layers;
    uint8_t layer_count;
    v2d_page_t pages[V2D_MAX_PAGES];
    uint8_t page_count;

    /* Programs */
    SceGxmShaderPatcherId program_ids[HW_PROG_COUNT + 1];
    SceGxmVertexProgram *vertex_program;
    SceGxmFragmentProgram *fragment_programs[HW_PROG_COUNT];
    const SceGxmProgramParameter *u_row_x;
    const SceGxmProgramParameter *u_row_y;
    const SceGxmProgramParameter *u_tex_scale;

    /* GPU-mapped memory */
    SceUID ring_uid;
    uint8_t *ring;
    SceUID index_uid;
    uint16_t *quad_indices;
    volatile unsigned int *notify;
    uint32_t seg_fence[V2D_SEGMENTS];
    uint32_t fence_value;

    /* Current frame */
    uint32_t flips;
    uint32_t frame;
    bool frame_valid;
    uint8_t *seg;
    uint8_t vtx_chunks_used;
    uint8_t clut_chunks_used;

    hw_recorder_t rec;
    bool pending_flip;
    hw_xform_t last_present_xform;
    bool last_present_valid;

    /*
     * The display scene: opened by the first draw of a frame (present, fill,
     * UI primitive) and ended by flipScreen(), so menus and overlays draw on
     * top of the presented frame in the same scene.
     */
    bool in_scene;
    size_t ui_used;				/* bytes of the segment's UI arena in use */
    RECT ui_clip;				/* setUIScissor(), display pixels */
    bool ui_arena_full_logged;

    /* DRAW / SCREEN_BITMAP as solid fills (menu backgrounds, see copyRect). */
    uint32_t draw_fill;
    uint32_t screen_fill;
    bool screen_fill_valid;
    vita2d_texture *scratch;	/* INITIAL_TEXTURE_LAYER */
    vita2d_texture *ui_atlas_tex;
    psvita_ui_atlas_t *ui_atlas;

    /* PSVITA_DUMP_LIST: the presented frame is read back after the swap. */
    uint32_t presented;
    bool dump_pending;
    RECT dump_src;
    hw_xform_t dump_m;

    /* Statistics */
    uint32_t stat_frames;
    uint32_t stat_scenes;
    uint32_t stat_draws;
    uint32_t stat_quads;
} psvita_video_t;


/******************************************************************************
    GPU memory and programs
******************************************************************************/

static void *v2d_alloc_mapped(SceKernelMemBlockType type, size_t size, SceUID *uid)
{
    size = (size + 0xfff) & ~(size_t)0xfff;
    *uid = sceKernelAllocMemBlock("njemu_video", type, size, NULL);
    if (*uid < 0)
        return NULL;

    void *base = NULL;
    sceKernelGetMemBlockBase(*uid, &base);
    if (base == NULL || sceGxmMapMemory(base, size, SCE_GXM_MEMORY_ATTRIB_READ) < 0) {
        sceKernelFreeMemBlock(*uid);
        *uid = -1;
        return NULL;
    }
    return base;
}

static void v2d_free_mapped(void *base, SceUID uid)
{
    if (base == NULL || uid < 0)
        return;
    sceGxmUnmapMemory(base);
    sceKernelFreeMemBlock(uid);
}

static bool v2d_create_programs(psvita_video_t *v)
{
    static const SceGxmProgram *const fragments[HW_PROG_COUNT] = {
        (const SceGxmProgram *)psvita_shader_indexed_gxp,
        (const SceGxmProgram *)psvita_shader_indexed_depth_gxp,
        (const SceGxmProgram *)psvita_shader_direct_gxp,
    };
    const SceGxmProgram *vertex = (const SceGxmProgram *)psvita_shader_vertex_gxp;
    SceGxmShaderPatcher *patcher = vita2d_get_shader_patcher();

    if (sceGxmShaderPatcherRegisterProgram(patcher, vertex, &v->program_ids[HW_PROG_COUNT]) < 0)
        return false;
    for (int i = 0; i < HW_PROG_COUNT; i++) {
        if (sceGxmShaderPatcherRegisterProgram(patcher, fragments[i], &v->program_ids[i]) < 0)
            return false;
    }

    const SceGxmProgramParameter *a_uv = sceGxmProgramFindParameterByName(vertex, "aUV");
    const SceGxmProgramParameter *a_pos = sceGxmProgramFindParameterByName(vertex, "aPos");
    const SceGxmProgramParameter *a_zp = sceGxmProgramFindParameterByName(vertex, "aZP");
    v->u_row_x = sceGxmProgramFindParameterByName(vertex, "uRowX");
    v->u_row_y = sceGxmProgramFindParameterByName(vertex, "uRowY");
    v->u_tex_scale = sceGxmProgramFindParameterByName(vertex, "uTexScale");
    if (!a_uv || !a_pos || !a_zp || !v->u_row_x || !v->u_row_y || !v->u_tex_scale)
        return false;

    const SceGxmVertexAttribute attributes[3] = {
        { 0, offsetof(hw_vertex_t, u), SCE_GXM_ATTRIBUTE_FORMAT_F32, 2,
          (uint16_t)sceGxmProgramParameterGetResourceIndex(a_uv) },
        { 0, offsetof(hw_vertex_t, x), SCE_GXM_ATTRIBUTE_FORMAT_S16, 2,
          (uint16_t)sceGxmProgramParameterGetResourceIndex(a_pos) },
        { 0, offsetof(hw_vertex_t, z), SCE_GXM_ATTRIBUTE_FORMAT_U16, 2,
          (uint16_t)sceGxmProgramParameterGetResourceIndex(a_zp) },
    };
    const SceGxmVertexStream stream = {
        sizeof(hw_vertex_t), SCE_GXM_INDEX_SOURCE_INDEX_16BIT
    };
    if (sceGxmShaderPatcherCreateVertexProgram(patcher, v->program_ids[HW_PROG_COUNT],
            attributes, 3, &stream, 1, &v->vertex_program) < 0)
        return false;

    /* Inverted alpha: bit 15 set (a = 1) keeps the destination. */
    const SceGxmBlendInfo inverted = {
        .colorMask = SCE_GXM_COLOR_MASK_ALL,
        .colorFunc = SCE_GXM_BLEND_FUNC_ADD,
        .alphaFunc = SCE_GXM_BLEND_FUNC_ADD,
        .colorSrc = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorDst = SCE_GXM_BLEND_FACTOR_SRC_ALPHA,
        .alphaSrc = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaDst = SCE_GXM_BLEND_FACTOR_SRC_ALPHA,
    };
    for (int i = 0; i < HW_PROG_COUNT; i++) {
        if (sceGxmShaderPatcherCreateFragmentProgram(patcher, v->program_ids[i],
                SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE,
                i == HW_PROG_INDEXED_DEPTH ? NULL : &inverted, vertex,
                &v->fragment_programs[i]) < 0)
            return false;
    }
    return true;
}

static void v2d_release_programs(psvita_video_t *v)
{
    SceGxmShaderPatcher *patcher = vita2d_get_shader_patcher();

    for (int i = 0; i < HW_PROG_COUNT; i++) {
        if (v->fragment_programs[i])
            sceGxmShaderPatcherReleaseFragmentProgram(patcher, v->fragment_programs[i]);
    }
    if (v->vertex_program)
        sceGxmShaderPatcherReleaseVertexProgram(patcher, v->vertex_program);
    for (int i = 0; i <= HW_PROG_COUNT; i++) {
        if (v->program_ids[i])
            sceGxmShaderPatcherUnregisterProgram(patcher, v->program_ids[i]);
    }
}

static vita2d_texture *v2d_create_texture(int width, int height, bool indexed, uint8_t **data)
{
    vita2d_texture *texture = vita2d_create_empty_texture_format(width, height,
        indexed ? SCE_GXM_TEXTURE_FORMAT_U8_R : SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ABGR);
    if (texture == NULL)
        return NULL;

    /* The core addresses layers as [row * width + x]. */
    if (vita2d_texture_get_stride(texture) != (unsigned)width * (indexed ? 1 : 2)) {
        vita2d_free_texture(texture);
        return NULL;
    }

    vita2d_texture_set_filters(texture, SCE_GXM_TEXTURE_FILTER_POINT,
        SCE_GXM_TEXTURE_FILTER_POINT);
    *data = vita2d_texture_get_datap(texture);
    memset(*data, 0, (size_t)width * height * (indexed ? 1 : 2));
    return texture;
}

static bool v2d_create_layers(psvita_video_t *v, const layer_texture_info_t *info,
                              uint8_t count)
{
    uint16_t page_height[V2D_MAX_PAGES] = { 0 };
    uint16_t page_width[V2D_MAX_PAGES] = { 0 };
    uint8_t layer_page[256];

    v->layers = calloc(count, sizeof(*v->layers));
    if (v->layers == NULL)
        return false;
    v->layer_count = count;

    /* Group indexed layers into pages: same width, stacked vertically. */
    for (uint8_t i = 0; i < count; i++) {
        v2d_layer_t *layer = &v->layers[i];
        layer->width = (uint16_t)info[i].width;
        layer->height = (uint16_t)info[i].height;
        layer->bytes_per_pixel = info[i].bytes_per_pixel;

        if (layer->bytes_per_pixel != 1)
            continue;

        uint8_t p;
        for (p = 0; p < v->page_count; p++) {
            if (page_width[p] == layer->width &&
                page_height[p] + layer->height <= V2D_MAX_TEXTURE_DIM)
                break;
        }
        if (p == v->page_count) {
            if (v->page_count == V2D_MAX_PAGES)
                return false;
            page_width[p] = layer->width;
            v->page_count++;
        }
        layer_page[i] = p;
        layer->hw.row_offset = page_height[p];
        page_height[p] += layer->height;
    }

    for (uint8_t p = 0; p < v->page_count; p++) {
        v2d_page_t *page = &v->pages[p];
        page->width = page_width[p];
        page->height = page_height[p];
        page->texture = v2d_create_texture(page->width, page->height, true, &page->data);
        if (page->texture == NULL)
            return false;
    }

    for (uint8_t i = 0; i < count; i++) {
        v2d_layer_t *layer = &v->layers[i];

        if (layer->bytes_per_pixel == 1) {
            v2d_page_t *page = &v->pages[layer_page[i]];
            layer->texture = page->texture;
            layer->hw.tex_width = page->width;
            layer->hw.tex_height = page->height;
            layer->hw.indexed = true;
            layer->cpu = page->data + (size_t)layer->hw.row_offset * page->width;
        } else if (layer->bytes_per_pixel == 2) {
            /* NJEMU 555 + bit 15 is exactly U1U5U5U5_ABGR: no shadow copy. */
            layer->texture = v2d_create_texture(layer->width, layer->height, false, &layer->cpu);
            if (layer->texture == NULL)
                return false;
            layer->hw.tex_width = layer->width;
            layer->hw.tex_height = layer->height;
            layer->hw.indexed = false;
        } else {
            return false;
        }
        layer->hw.texture = &layer->texture->gxm_tex;
    }
    return true;
}


/******************************************************************************
    Frame memory and recording
******************************************************************************/

/* Recorder memory: chunks of the current ring segment. */
static hw_vertex_t *v2d_alloc_vertices(void *user, const void **handle)
{
    psvita_video_t *v = user;
    if ((uint32_t)(v->vtx_chunks_used + 1) * HW_CHUNK_QUADS > V2D_SEG_QUADS)
        return NULL;
    hw_vertex_t *vtx = (hw_vertex_t *)v->seg +
        (size_t)v->vtx_chunks_used++ * HW_CHUNK_QUADS * 4;
    *handle = vtx;
    return vtx;
}

static uint16_t *v2d_alloc_clut(void *user, const void **handle)
{
    psvita_video_t *v = user;
    if (v->clut_chunks_used == V2D_CLUT_CHUNKS)
        return NULL;
    uint16_t *clut = (uint16_t *)(v->seg + V2D_SEG_VTX_BYTES +
        (size_t)v->clut_chunks_used++ * HW_CLUT_CHUNK_BYTES);
    *handle = clut;
    return clut;
}

static const hw_recorder_ops_t v2d_ops = { v2d_alloc_vertices, v2d_alloc_clut };

/*
 * Starts using this frame's ring segment.  The GPU may still be reading it
 * from the scene presented V2D_SEGMENTS frames ago: wait for that scene's
 * fragment notification (normally long signalled).
 */
static void v2d_frame_sync(psvita_video_t *v)
{
    if (v->frame_valid && v->frame == v->flips)
        return;

    const uint32_t s = v->flips % V2D_SEGMENTS;
    if (v->seg_fence[s] != 0) {
        const SceGxmNotification fence = { &v->notify[V2D_NOTIFY_SLOT + s], v->seg_fence[s] };
        sceGxmNotificationWait(&fence);
        v->seg_fence[s] = 0;
    }

    v->frame_valid = true;
    v->frame = v->flips;
    v->seg = v->ring + (size_t)s * V2D_SEG_BYTES;
    v->vtx_chunks_used = 0;
    v->clut_chunks_used = 0;
    v->ui_used = 0;
    hw_rec_reset(&v->rec);

    /* A full atlas is reset between scenes, once the GPU stopped reading it. */
    if (v->ui_atlas != NULL && v->ui_atlas->full) {
        vita2d_wait_rendering_done();
        psvita_ui_atlas_reset(v->ui_atlas);
    }
}

/* Opens the display scene if needed; a new scene starts cleared to black. */
static void v2d_open_scene(psvita_video_t *v)
{
    SceGxmContext *ctx;

    if (v->in_scene)
        return;
    v2d_frame_sync(v);

    ctx = vita2d_get_context();
    /* Only vita2d_start_drawing() resets the pool that textured draws take
     * their tint colour from; it is CPU-only, so reusing it is safe. */
    vita2d_pool_reset();
    vita2d_start_drawing_advanced(NULL, 0);
    vita2d_set_region_clip(SCE_GXM_REGION_CLIP_NONE, 0, 0,
        PSVITA_DISPLAY_WIDTH - 1, PSVITA_DISPLAY_HEIGHT - 1);
    sceGxmSetCullMode(ctx, SCE_GXM_CULL_NONE);
    sceGxmSetFrontPolygonMode(ctx, SCE_GXM_POLYGON_MODE_TRIANGLE_FILL);
    sceGxmSetBackPolygonMode(ctx, SCE_GXM_POLYGON_MODE_TRIANGLE_FILL);
    sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetBackDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetBackDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetFrontStencilFunc(ctx, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP,
        SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0xff, 0xff);
    sceGxmSetBackStencilFunc(ctx, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP,
        SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0xff, 0xff);
    vita2d_set_clear_color(RGBA8(0, 0, 0, 255));
    vita2d_clear_screen();

    v->in_scene = true;
    v->ui_clip = (RECT){ 0, 0, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT };
    v->stat_scenes++;
}

/* Ends the display scene, fencing the memory of its segment. */
static void v2d_close_scene(psvita_video_t *v)
{
    if (!v->in_scene)
        return;

    const uint32_t s = v->frame % V2D_SEGMENTS;
    const SceGxmNotification fence = { &v->notify[V2D_NOTIFY_SLOT + s], ++v->fence_value };
    sceGxmEndScene(vita2d_get_context(), NULL, &fence);
    v->seg_fence[s] = fence.value;
    v->in_scene = false;
}


/******************************************************************************
    Replay
******************************************************************************/

static void v2d_set_depth(uint8_t mode)
{
    SceGxmContext *ctx = vita2d_get_context();
    SceGxmDepthFunc func = mode == HW_DEPTH_TEST ? SCE_GXM_DEPTH_FUNC_GREATER_EQUAL
                                                 : SCE_GXM_DEPTH_FUNC_ALWAYS;
    SceGxmDepthWriteMode write = mode == HW_DEPTH_OFF ? SCE_GXM_DEPTH_WRITE_DISABLED
                                                      : SCE_GXM_DEPTH_WRITE_ENABLED;
    sceGxmSetFrontDepthFunc(ctx, func);
    sceGxmSetBackDepthFunc(ctx, func);
    sceGxmSetFrontDepthWriteEnable(ctx, write);
    sceGxmSetBackDepthWriteEnable(ctx, write);
}

static void v2d_set_stencil(SceGxmStencilFunc func, SceGxmStencilOp fail, SceGxmStencilOp pass)
{
    SceGxmContext *ctx = vita2d_get_context();
    sceGxmSetFrontStencilFunc(ctx, func, fail, SCE_GXM_STENCIL_OP_KEEP, pass, 0xff, 0xff);
    sceGxmSetBackStencilFunc(ctx, func, fail, SCE_GXM_STENCIL_OP_KEEP, pass, 0xff, 0xff);
}

static void v2d_replay_draw(psvita_video_t *v, const hw_cmd_t *cmd,
                            const float row_x[3], const float row_y[3])
{
    SceGxmContext *ctx = vita2d_get_context();
    SceGxmTexture clut;
    void *uniforms;

    v2d_set_depth(cmd->depth);
    sceGxmSetVertexProgram(ctx, v->vertex_program);
    sceGxmSetFragmentProgram(ctx, v->fragment_programs[cmd->prog]);

    if (cmd->clut != NULL) {
        sceGxmTextureInitLinear(&clut, cmd->clut, SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ABGR,
            HW_CLUT_ROW_ENTRIES, HW_CLUT_ROWS, 0);
        sceGxmTextureSetMinFilter(&clut, SCE_GXM_TEXTURE_FILTER_POINT);
        sceGxmTextureSetMagFilter(&clut, SCE_GXM_TEXTURE_FILTER_POINT);
        sceGxmSetFragmentTexture(ctx, 1, &clut);
    }
    sceGxmSetFragmentTexture(ctx, 0, cmd->texture != NULL ? cmd->texture : &clut);

    const float scale[4] = { 1.0f / cmd->tex_w, 1.0f / cmd->tex_h, 1.0f / HW_CLUT_ROWS, 0.0f };
    const float rx[4] = { row_x[0], row_x[1], row_x[2], 0.0f };
    const float ry[4] = { row_y[0], row_y[1], row_y[2], 0.0f };
    sceGxmReserveVertexDefaultUniformBuffer(ctx, &uniforms);
    sceGxmSetUniformDataF(uniforms, v->u_row_x, 0, 4, rx);
    sceGxmSetUniformDataF(uniforms, v->u_row_y, 0, 4, ry);
    sceGxmSetUniformDataF(uniforms, v->u_tex_scale, 0, 4, scale);

    uint32_t first = cmd->first_quad, quads = cmd->quads;
    while (quads) {
        uint32_t n = quads > V2D_DRAW_MAX_QUADS ? V2D_DRAW_MAX_QUADS : quads;
        sceGxmSetVertexStream(ctx, 0, (const hw_vertex_t *)cmd->vertices + (size_t)first * 4);
        sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16,
            v->quad_indices, n * 6);
        v->stat_draws++;
        v->stat_quads += n;
        first += n;
        quads -= n;
    }
}

/*
 * Replays the recorded work frame into the display back buffer, mapping the
 * work frame rectangle `src_rect` onto `dst_rect` with the given orientation.
 */
static void v2d_present(psvita_video_t *v, const RECT *src_rect, const RECT *dst_rect,
                        int orient)
{
    hw_xform_t m;
    RECT d;
    float row_x[3], row_y[3];

    hw_rec_flush(&v->rec);
    v2d_frame_sync(v);

    if (!hw_present_geometry(src_rect, dst_rect, orient, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT, &d, &m))
        return;
    v->last_present_xform = m;
    v->last_present_valid = true;
    hw_clip_rows(&m, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT, row_x, row_y);

    /*
     * Only the source rectangle is visible, at pixel precision.  GXM's region
     * clip works on whole tiles, so build a stencil mask of the destination
     * rectangle with two display-space quads, as vitaGL does for glScissor.
     */
    const RECT display = { 0, 0, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT };
    hw_recorder_t *rec = &v->rec;
    const uint32_t cmd_first = rec->cmd_count;
    hw_rec_fill(rec, &display, 0x8000, HW_DEPTH_OFF);
    hw_rec_flush(rec);
    hw_rec_fill(rec, &d, 0x8000, HW_DEPTH_OFF);
    hw_rec_flush(rec);
    if (rec->cmd_count != cmd_first + 2 || rec->cmds[cmd_first].quads != 1 ||
        rec->cmds[cmd_first + 1].quads != 1)
        return;
    rec->cmd_count -= 2;
    const hw_cmd_t mask_all = rec->cmds[rec->cmd_count];
    const hw_cmd_t mask_dst = rec->cmds[rec->cmd_count + 1];
    const float display_x[3] = { 2.0f / PSVITA_DISPLAY_WIDTH, 0.0f, -1.0f };
    const float display_y[3] = { 0.0f, -2.0f / PSVITA_DISPLAY_HEIGHT, 1.0f };

    SceGxmContext *ctx = vita2d_get_context();
    v2d_open_scene(v);

    sceGxmSetFrontStencilRef(ctx, 1);
    sceGxmSetBackStencilRef(ctx, 1);
    v2d_set_stencil(SCE_GXM_STENCIL_FUNC_NEVER, SCE_GXM_STENCIL_OP_ZERO, SCE_GXM_STENCIL_OP_KEEP);
    v2d_replay_draw(v, &mask_all, display_x, display_y);
    v2d_set_stencil(SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_REPLACE);
    v2d_replay_draw(v, &mask_dst, display_x, display_y);
    v2d_set_stencil(SCE_GXM_STENCIL_FUNC_EQUAL, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP);

    for (uint32_t i = 0; i < rec->cmd_count; i++)
        v2d_replay_draw(v, &rec->cmds[i], row_x, row_y);

    v2d_set_depth(HW_DEPTH_OFF);
    v2d_set_stencil(SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP);

    if (psvita_dump_wanted(++v->presented)) {
        v->dump_pending = true;
        v->dump_src = *src_rect;
        v->dump_m = m;
    }
}


/******************************************************************************
    Driver: lifetime
******************************************************************************/

static void psvita_free(void *data);

static void *psvita_init(layer_texture_info_t *layer_textures,
                         uint8_t layer_textures_count,
                         clut_info_t *clut_info)
{
    psvita_video_t *v = calloc(1, sizeof(*v));
    if (v == NULL)
        return NULL;
    v->ring_uid = v->index_uid = -1;

    if (!vita2d_init()) {
        free(v);
        return NULL;
    }
    vita2d_set_vblank_wait(1);
    vita2d_set_clear_color(RGBA8(0, 0, 0, 255));

    if (!v2d_create_programs(v)) {
        psvita_video_log("vita2d: program setup failed\n");
        goto fail;
    }
    if (!v2d_create_layers(v, layer_textures, layer_textures_count)) {
        psvita_video_log("vita2d: texture layer setup failed\n");
        goto fail;
    }

    if (!hw_rec_init(&v->rec, &v2d_ops, v, V2D_WORK_WIDTH, V2D_WORK_HEIGHT, clut_info))
        goto fail;

    /* Uncached like vita2d's own pool: CPU stores reach the GPU directly. */
    v->ring = v2d_alloc_mapped(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
        (size_t)V2D_SEGMENTS * V2D_SEG_BYTES, &v->ring_uid);
    v->quad_indices = v2d_alloc_mapped(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
        V2D_DRAW_MAX_QUADS * 6 * sizeof(uint16_t), &v->index_uid);
    if (v->ring == NULL || v->quad_indices == NULL) {
        psvita_video_log("vita2d: frame memory allocation failed\n");
        goto fail;
    }
    for (uint32_t q = 0; q < V2D_DRAW_MAX_QUADS; q++) {
        uint16_t *i = v->quad_indices + q * 6;
        const uint16_t b = (uint16_t)(q * 4);
        i[0] = b; i[1] = b + 1; i[2] = b + 2;
        i[3] = b; i[4] = b + 2; i[5] = b + 3;
    }

    /* CPU-visible UI scratch surface (NCDZ titles, PNG loading). */
    v->scratch = vita2d_create_empty_texture_format(V2D_SCRATCH_WIDTH, V2D_SCRATCH_HEIGHT,
        SCE_GXM_TEXTURE_FORMAT_X1U5U5U5_1BGR);
    v->ui_atlas = calloc(1, sizeof(*v->ui_atlas));
    v->ui_atlas_tex = vita2d_create_empty_texture_format(V2D_UI_ATLAS_WIDTH, V2D_UI_ATLAS_HEIGHT,
        SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_ABGR);
    if (v->ui_atlas != NULL && v->ui_atlas_tex != NULL) {
        v->ui_atlas->texels = vita2d_texture_get_datap(v->ui_atlas_tex);
        v->ui_atlas->width = vita2d_texture_get_stride(v->ui_atlas_tex) / 2;
        v->ui_atlas->height = V2D_UI_ATLAS_HEIGHT;
    }

    v->notify = sceGxmGetNotificationRegion();
    for (int s = 0; s < V2D_SEGMENTS; s++)
        v->notify[V2D_NOTIFY_SLOT + s] = 0;

    psvita_video_log("vita2d init ok: %u layers in %u page(s), %u CLUT windows\n",
        (unsigned)v->layer_count, (unsigned)v->page_count, (unsigned)v->rec.clut_windows);
    return v;

fail:
    psvita_free(v);
    return NULL;
}

static void psvita_free(void *data)
{
    psvita_video_t *v = data;
    if (v == NULL)
        return;

    vita2d_wait_rendering_done();

    for (uint8_t i = 0; i < v->layer_count; i++) {
        if (v->layers[i].bytes_per_pixel != 1 && v->layers[i].texture)
            vita2d_free_texture(v->layers[i].texture);
    }
    for (uint8_t p = 0; p < v->page_count; p++)
        vita2d_free_texture(v->pages[p].texture);
    if (v->scratch != NULL)
        vita2d_free_texture(v->scratch);
    if (v->ui_atlas_tex != NULL)
        vita2d_free_texture(v->ui_atlas_tex);
    free(v->ui_atlas);
    v2d_release_programs(v);
    v2d_free_mapped(v->ring, v->ring_uid);
    v2d_free_mapped(v->quad_indices, v->index_uid);

    vita2d_fini();
    hw_rec_free(&v->rec);
    free(v->layers);
    free(v);
}


/******************************************************************************
    Driver: frame control
******************************************************************************/

static void psvita_waitVsync(void *data)
{
    (void)data;
    sceDisplayWaitVblankStart();
}

static void psvita_flipScreen(void *data, bool vsync)
{
    psvita_video_t *v = data;

    /* Nothing drawn since the last flip (e.g. a message box waiting for a
     * key): keep showing the current frame instead of a stale back buffer. */
    if (!v->in_scene) {
        if (vsync)
            sceDisplayWaitVblankStart();
        return;
    }

    v2d_close_scene(v);
    vita2d_set_vblank_wait(vsync ? 1 : 0);
    vita2d_common_dialog_update();
    vita2d_swap_buffers();
    v->flips++;

    if (v->dump_pending) {
        /* The front buffer is the frame just presented. */
        v->dump_pending = false;
        vita2d_wait_rendering_done();
        psvita_dump_frame("vita2d", v->presented, vita2d_get_current_fb(), 960, false,
            &v->dump_src, &v->dump_m);
    }

#if PSVITA_VIDEO_STATS
    if (++v->stat_frames == PSVITA_VIDEO_STATS_FRAMES) {
        const float n = (float)v->stat_frames;
        psvita_video_log("vita2d frames=%u scenes/frame=%.2f draws/frame=%.1f quads/frame=%.0f clut_rows/frame=%.1f\n",
            (unsigned)v->stat_frames, v->stat_scenes / n, v->stat_draws / n,
            v->stat_quads / n, v->rec.stat_clut_rows / n);
        v->stat_frames = v->stat_scenes = v->stat_draws = v->stat_quads = v->rec.stat_clut_rows = 0;
    }
#endif
}

static void psvita_beginFrame(void *data)
{
    v2d_frame_sync(data);
}

static void psvita_endFrame(void *data)
{
    hw_rec_flush(&((psvita_video_t *)data)->rec);
}

static void *psvita_frameAddr(void *data, int frameIndex, int x, int y)
{
    psvita_video_t *v = data;

    /* Only the UI scratch surface is CPU-addressable (PSP 5551 texels). */
    if (frameIndex != COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER || v->scratch == NULL
        || x < 0 || y < 0 || x >= V2D_SCRATCH_WIDTH || y >= V2D_SCRATCH_HEIGHT)
        return NULL;
    return (uint16_t *)vita2d_texture_get_datap(v->scratch) + y * V2D_SCRATCH_WIDTH + x;
}

/*
 * CPU copy of a frame as PSP 555 texels. SHOW_FRAME_BUFFER is read from the
 * displayed frame in the 480x272 logical space (sampled from the 960x544
 * display); INITIAL_TEXTURE_LAYER from the scratch surface.
 */
static int psvita_readFrame(void *data, int frameIndex, int x, int y, int width, int height,
                            uint16_t *dst, int dstPitch)
{
    psvita_video_t *v = data;

    if (dst == NULL || width <= 0 || height <= 0 || x < 0 || y < 0)
        return 0;

    if (frameIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER) {
        if (v->scratch == NULL || x + width > V2D_SCRATCH_WIDTH || y + height > V2D_SCRATCH_HEIGHT)
            return 0;
        const uint16_t *src = (const uint16_t *)vita2d_texture_get_datap(v->scratch);
        for (int row = 0; row < height; row++)
            memcpy(dst + (size_t)row * dstPitch, src + (size_t)(y + row) * V2D_SCRATCH_WIDTH + x,
                (size_t)width * sizeof(uint16_t));
        return 1;
    }

    if (frameIndex != COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER
        || x + width > SCR_WIDTH || y + height > SCR_HEIGHT)
        return 0;

    vita2d_wait_rendering_done();
    const uint32_t *fb = vita2d_get_current_fb();
    for (int row = 0; row < height; row++) {
        const uint32_t *src = fb + (size_t)((y + row) * PSVITA_DISPLAY_HEIGHT / SCR_HEIGHT)
            * PSVITA_DISPLAY_WIDTH;
        for (int col = 0; col < width; col++) {
            const uint32_t c = src[(x + col) * PSVITA_DISPLAY_WIDTH / SCR_WIDTH];
            dst[(size_t)row * dstPitch + col] = hw_rgba_to_555(c);
        }
    }
    return 1;
}

static void psvita_getOutputSize(void *data, int *width, int *height)
{
    (void)data;
    *width = PSVITA_DISPLAY_WIDTH;
    *height = PSVITA_DISPLAY_HEIGHT;
}

static void psvita_scissor(void *data, uint16_t left, uint16_t top,
                           uint16_t right, uint16_t bottom)
{
    hw_rec_set_clip(&((psvita_video_t *)data)->rec, left, top, right, bottom);
}

/* DRAW_FRAME_BUFFER / SHOW_FRAME_BUFFER are the display being built. */
static void v2d_clear_display(psvita_video_t *v, uint32_t color)
{
    v2d_open_scene(v);
    vita2d_set_clear_color(color | 0xff000000u);
    vita2d_clear_screen();
    v->draw_fill = color | 0xff000000u;
}

static void psvita_clearScreen(void *data)
{
    v2d_clear_display(data, 0);
}

static void psvita_clearFrame(void *data, int index)
{
    /* SCREEN_BITMAP is the recorded work frame; the others are the display. */
    psvita_video_t *v = data;

    if (index == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP) {
        v2d_frame_sync(v);
        hw_rec_fill(&v->rec, &v->rec.work, 0, HW_DEPTH_OFF);
    } else if (index == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER) {
        if (v->scratch != NULL)
            memset(vita2d_texture_get_datap(v->scratch), 0,
                (size_t)V2D_SCRATCH_WIDTH * V2D_SCRATCH_HEIGHT * sizeof(uint16_t));
    } else {
        v2d_clear_display(v, 0);
    }
}

static void psvita_fillFrame(void *data, int frameIndex, uint32_t color)
{
    psvita_video_t *v = data;

    if (frameIndex == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP) {
        v2d_frame_sync(v);
        hw_rec_fill(&v->rec, &v->rec.work, hw_rgba_to_555(color), HW_DEPTH_OFF);
    } else
        v2d_clear_display(data, color);
}

static void psvita_startWorkFrame(void *data, uint32_t color)
{
    psvita_video_t *v = data;

    v2d_frame_sync(v);
    v->pending_flip = false;
    v->screen_fill_valid = false;
    hw_rec_begin_work(&v->rec, hw_rgba_to_555(color));
}

static void v2d_ui_fill(psvita_video_t *v, int x, int y, int w, int h, uint32_t c0,
                        uint32_t c1, uint32_t c2, uint32_t c3, bool clip);
static void v2d_ui_texture(psvita_video_t *v, const void *pixels, SceGxmTextureFormat format,
                           int bpp, int tex_stride, int su, int sv, int sw, int sh,
                           int dx, int dy, int dw, int dh, bool copy, bool clip);

static bool v2d_capture_front_to_scratch(psvita_video_t *v, const RECT *src,
                                        const RECT *dst, bool rotate)
{
    const int sw = src->right - src->left;
    const int sh = src->bottom - src->top;
    const int dw = dst->right - dst->left;
    const int dh = dst->bottom - dst->top;
    const uint32_t *fb;
    uint16_t *scratch;

    if (v->scratch == NULL || !v->last_present_valid || sw <= 0 || sh <= 0 ||
        dw <= 0 || dh <= 0 || dst->left < 0 || dst->top < 0 ||
        dst->right > V2D_SCRATCH_WIDTH || dst->bottom > V2D_SCRATCH_HEIGHT)
        return false;

    vita2d_wait_rendering_done();
    fb = vita2d_get_current_fb();
    scratch = (uint16_t *)vita2d_texture_get_datap(v->scratch);
    if (fb == NULL || scratch == NULL)
        return false;

    for (int y = 0; y < dh; y++) {
        uint16_t *out = scratch + (size_t)(dst->top + y) * V2D_SCRATCH_WIDTH + dst->left;
        for (int x = 0; x < dw; x++) {
            float sx, sy, px, py;
            if (rotate) {
                sx = src->right - ((y + 0.5f) * sw / dh);
                sy = src->top + ((x + 0.5f) * sh / dw);
            } else {
                sx = src->left + ((x + 0.5f) * sw / dw);
                sy = src->top + ((y + 0.5f) * sh / dh);
            }
            hw_map_point(&v->last_present_xform, sx, sy, &px, &py);
            int ix = (int)px;
            int iy = (int)py;
            if (ix < 0) ix = 0;
            if (iy < 0) iy = 0;
            if (ix >= PSVITA_DISPLAY_WIDTH) ix = PSVITA_DISPLAY_WIDTH - 1;
            if (iy >= PSVITA_DISPLAY_HEIGHT) iy = PSVITA_DISPLAY_HEIGHT - 1;
            out[x] = hw_rgba_to_555(fb[(size_t)iy * PSVITA_DISPLAY_WIDTH + ix]);
        }
    }
    return true;
}

static void psvita_transferWorkFrame(void *data, RECT *src_rect, RECT *dst_rect)
{
    psvita_video_t *v = data;

    /* The UI background cached in SCREEN_BITMAP is a solid fill (copyRect). */
    if (v->screen_fill_valid) {
        v2d_open_scene(v);
        v2d_ui_fill(v, dst_rect->left, dst_rect->top, dst_rect->right - dst_rect->left,
            dst_rect->bottom - dst_rect->top, v->screen_fill, v->screen_fill,
            v->screen_fill, v->screen_fill, false);
        return;
    }
    v2d_present(v, src_rect, dst_rect, HW_ORIENT_NORMAL);
}

/* Draws the scratch surface (INITIAL_TEXTURE_LAYER) opaque onto the display. */
static void v2d_draw_scratch(psvita_video_t *v, const RECT *src, const RECT *dst)
{
    if (v->scratch == NULL)
        return;
    v2d_open_scene(v);
    v2d_ui_texture(v, vita2d_texture_get_datap(v->scratch), SCE_GXM_TEXTURE_FORMAT_X1U5U5U5_1BGR,
        2, V2D_SCRATCH_WIDTH, src->left, src->top, src->right - src->left, src->bottom - src->top,
        dst->left, dst->top, dst->right - dst->left, dst->bottom - dst->top, false, false);
}

static void psvita_copyRect(void *data, int srcIndex, int dstIndex,
                            RECT *src_rect, RECT *dst_rect)
{
    psvita_video_t *v = data;
    const bool dst_display = dstIndex == COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER
        || dstIndex == COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER;

    if (srcIndex == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP) {
        if (dstIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER)
            v2d_capture_front_to_scratch(v, src_rect, dst_rect, false);
        else if (dst_display)
            v2d_present(v, src_rect, dst_rect, HW_ORIENT_NORMAL);
    } else if (srcIndex == COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER
               && dstIndex == COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP) {
        /*
         * Either the CPS rotate+flip sequence, whose flip is folded into the
         * final rotated present, or the UI caching its background, which is
         * the solid fill of the display (no chrome without UI_DRAW_CAP_CACHE_CHROME).
         */
        if (!v->pending_flip) {
            v->screen_fill = v->draw_fill;
            v->screen_fill_valid = true;
        }
    } else if (srcIndex == COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER && dstIndex == COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER) {
        /* Dialogs start from the displayed frame: draw it back, whole. */
        v2d_open_scene(v);
        v2d_ui_texture(v, vita2d_get_current_fb(), SCE_GXM_TEXTURE_FORMAT_X8U8U8U8_1BGR, 4,
            PSVITA_DISPLAY_WIDTH, 0, 0, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT,
            0, 0, PSVITA_DISPLAY_WIDTH, PSVITA_DISPLAY_HEIGHT, false, false);
    } else if (srcIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER && dst_display) {
        v2d_draw_scratch(v, src_rect, dst_rect);
    }
}

static void psvita_copyRectFlip(void *data, int srcIndex, int dstIndex,
                                RECT *src_rect, RECT *dst_rect)
{
    psvita_video_t *v = data;
    (void)dstIndex;

    if (srcIndex != COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP)
        return;

    /* CPS rotate+flip flips the work frame in place first (src == dst). */
    if (src_rect->left == dst_rect->left && src_rect->top == dst_rect->top &&
        src_rect->right == dst_rect->right && src_rect->bottom == dst_rect->bottom) {
        v->pending_flip = true;
        return;
    }

    v2d_present(v, src_rect, dst_rect, HW_ORIENT_FLIP);
}

static void psvita_copyRectRotate(void *data, int srcIndex, int dstIndex,
                                  RECT *src_rect, RECT *dst_rect)
{
    psvita_video_t *v = data;
    (void)dstIndex;

    if (srcIndex != COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP)
        return;
    if (dstIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER) {
        v2d_capture_front_to_scratch(v, src_rect, dst_rect, true);
        return;
    }

    v2d_present(v, src_rect, dst_rect,
        v->pending_flip ? HW_ORIENT_ROTATE_FLIP : HW_ORIENT_ROTATE);
    v->pending_flip = false;
}

static void psvita_drawTexture(void *data, int srcIndex, int dstIndex,
                               RECT *src_rect, RECT *dst_rect)
{
    if (srcIndex == COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER
        && (dstIndex == COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER
            || dstIndex == COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER))
        v2d_draw_scratch(data, src_rect, dst_rect);
}



/******************************************************************************
    Driver: drawing
******************************************************************************/

static void psvita_commitTextureUpdates(void *data, uint8_t textureIndex)
{
    /* The texture rects were written straight into the GPU-visible textures. */
    (void)data;
    (void)textureIndex;
}

static void psvita_writeIndexedTextureRect(void *data, uint8_t textureIndex,
                                           int x, int y, int width, int height,
                                           const uint8_t *pixels, int srcPitch)
{
    psvita_video_t *v = data;
    const v2d_layer_t *layer;

    if (textureIndex >= v->layer_count || v->layers[textureIndex].bytes_per_pixel != 1)
        return;
    layer = &v->layers[textureIndex];
    psvita_write_texture_rect(layer->cpu, layer->width, layer->height, 1,
        x, y, width, height, pixels, srcPitch);
}

static void psvita_writeDirectTextureRect(void *data, uint8_t textureIndex,
                                          int x, int y, int width, int height,
                                          const uint16_t *pixels, int srcPitch)
{
    psvita_video_t *v = data;
    const v2d_layer_t *layer;

    if (textureIndex >= v->layer_count || v->layers[textureIndex].bytes_per_pixel != 2)
        return;
    layer = &v->layers[textureIndex];
    psvita_write_texture_rect(layer->cpu, layer->width, layer->height, 2,
        x, y, width, height, pixels, srcPitch);
}

static void psvita_uploadClut(void *data, uint16_t *bank, uint8_t bank_index)
{
    /* CLUT windows are captured lazily per batch in blitSpriteVertices(). */
    (void)data;
    (void)bank;
    (void)bank_index;
}

static void psvita_blitSpriteVertices(void *data, uint8_t textureIndex,
                                     const uint16_t *clut, uint8_t bank_index,
                                     uint32_t vertices_count,
                                     const video_sprite_vertex_t *vertices)
{
    psvita_video_t *v = data;
    (void)bank_index;

    if (textureIndex >= v->layer_count)
        return;
    v2d_frame_sync(v);
    hw_rec_blit(&v->rec, &v->layers[textureIndex].hw, clut, vertices, vertices_count);
}

static void psvita_blitPointVertices(void *data, uint32_t points_count,
                                    const video_point_vertex_t *vertices)
{
    psvita_video_t *v = data;

    v2d_frame_sync(v);
    hw_rec_points(&v->rec, vertices, points_count);
}


static void psvita_enableDepthTest(void *data)
{
    hw_rec_set_depth_test(&((psvita_video_t *)data)->rec, true);
}

static void psvita_disableDepthTest(void *data)
{
    hw_rec_set_depth_test(&((psvita_video_t *)data)->rec, false);
}

static void psvita_clearDepthBuffer(void *data)
{
    /* PSP semantics: depth cleared to 0, GEQUAL test.  Colour untouched. */
    psvita_video_t *v = data;

    v2d_frame_sync(v);
    hw_rec_fill(&v->rec, &v->rec.work, 0x8000, HW_DEPTH_WRITE);
}

static void psvita_clearColorBuffer(void *data)
{
    psvita_video_t *v = data;

    /* Clears the colour inside the current scissor, keeping depth. */
    v2d_frame_sync(v);
    hw_rec_fill(&v->rec, &v->rec.clip, 0, HW_DEPTH_OFF);
}

/******************************************************************************
    Driver: UI primitives

    Common UI code passes display pixels. Everything goes into the display
    scene through vita2d's colour and texture programs (alpha blending), with
    vertices and texel copies taken from the frame segment's UI arena, fenced
    with the scene like the emulator's draws. The UI scissor is applied on the
    CPU: GXM's region clip only works on whole tiles.
******************************************************************************/

static void *v2d_ui_alloc(psvita_video_t *v, size_t bytes, size_t align)
{
    const size_t offset = (v->ui_used + align - 1) & ~(align - 1);

    if (offset + bytes > V2D_SEG_UI_BYTES) {
        if (!v->ui_arena_full_logged)
            psvita_video_log("vita2d: UI arena full, dropping UI draws\n");
        v->ui_arena_full_logged = true;
        return NULL;
    }
    v->ui_used = offset + bytes;
    return v->seg + V2D_SEG_UI_OFFSET + offset;
}

/* Clips [x0, x1) x [y0, y1) to the UI scissor; false when nothing is left. */
static bool v2d_ui_clip(const psvita_video_t *v, float *x0, float *y0, float *x1, float *y1)
{
    const RECT *c = &v->ui_clip;

    if (*x0 < c->left) *x0 = c->left;
    if (*y0 < c->top) *y0 = c->top;
    if (*x1 > c->right) *x1 = c->right;
    if (*y1 > c->bottom) *y1 = c->bottom;
    return *x1 > *x0 && *y1 > *y0;
}

/* Rectangle with one colour per corner: top-left, top-right, bottom-left, bottom-right. */
static void v2d_ui_fill(psvita_video_t *v, int x, int y, int w, int h, uint32_t c0,
                        uint32_t c1, uint32_t c2, uint32_t c3, bool clip)
{
    float x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    vita2d_color_vertex *q;

    if (w <= 0 || h <= 0)
        return;
    /* Gradients are only ever clipped along the direction they do not vary. */
    if (clip && !v2d_ui_clip(v, &x0, &y0, &x1, &y1))
        return;
    v2d_open_scene(v);
    if ((q = v2d_ui_alloc(v, 4 * sizeof(*q), 16)) == NULL)
        return;
    q[0] = (vita2d_color_vertex){ x0, y0, 0.5f, c0 };
    q[1] = (vita2d_color_vertex){ x1, y0, 0.5f, c1 };
    q[2] = (vita2d_color_vertex){ x0, y1, 0.5f, c2 };
    q[3] = (vita2d_color_vertex){ x1, y1, 0.5f, c3 };
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, q, 4);
}

/*
 * Draws texels [su, su + sw) x [sv, sv + sh) of a linear texture onto a
 * display rectangle, with nearest filtering. `copy` is for CPU buffers that
 * change before the GPU reads them: 16-bit texels are taken from the UI atlas,
 * anything else is copied into the scene's arena. Otherwise `pixels` must be
 * GPU-mapped memory.
 */
static void v2d_ui_texture(psvita_video_t *v, const void *pixels, SceGxmTextureFormat format,
                           int bpp, int tex_stride, int su, int sv, int sw, int sh,
                           int dx, int dy, int dw, int dh, bool copy, bool clip)
{
    float x0 = dx, y0 = dy, x1 = dx + dw, y1 = dy + dh;
    float u0, v0, u1, v1;
    int tex_w, tex_h;
    const void *data = pixels;
    vita2d_texture texture;
    vita2d_texture_vertex *q;

    if (pixels == NULL || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
        return;
    if (clip && !v2d_ui_clip(v, &x0, &y0, &x1, &y1))
        return;
    v2d_open_scene(v);

    int ax, ay;
    if (copy && bpp == 2 && v->ui_atlas != NULL
        && psvita_ui_atlas_get(v->ui_atlas, (const uint16_t *)pixels + (size_t)sv * tex_stride + su,
            tex_stride, sw, sh, &ax, &ay)) {
        data = v->ui_atlas->texels;
        tex_w = v->ui_atlas->width;
        tex_h = v->ui_atlas->height;
        u0 = ax;
        v0 = ay;
    } else if (copy) {
        const int stride = (sw + 7) & ~7;
        uint8_t *dst = v2d_ui_alloc(v, (size_t)stride * sh * bpp, 64);
        if (dst == NULL)
            return;
        for (int row = 0; row < sh; row++)
            memcpy(dst + (size_t)row * stride * bpp,
                (const uint8_t *)pixels + ((size_t)(sv + row) * tex_stride + su) * bpp,
                (size_t)sw * bpp);
        data = dst;
        tex_w = stride;
        tex_h = sh;
        u0 = 0.0f;
        v0 = 0.0f;
    } else {
        tex_w = tex_stride;
        tex_h = sv + sh;
        u0 = su;
        v0 = sv;
    }
    u1 = u0 + sw;
    v1 = v0 + sh;

    /* Keep texels attached to pixels when the scissor cuts the rectangle. */
    const float su_px = (u1 - u0) / dw, sv_px = (v1 - v0) / dh;
    const float cu0 = u0 + (x0 - dx) * su_px, cu1 = u0 + (x1 - dx) * su_px;
    const float cv0 = v0 + (y0 - dy) * sv_px, cv1 = v0 + (y1 - dy) * sv_px;

    memset(&texture, 0, sizeof(texture));
    if (sceGxmTextureInitLinear(&texture.gxm_tex, data, format, tex_w, tex_h, 0) < 0)
        return;
    sceGxmTextureSetMinFilter(&texture.gxm_tex, SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetMagFilter(&texture.gxm_tex, SCE_GXM_TEXTURE_FILTER_POINT);
    sceGxmTextureSetUAddrMode(&texture.gxm_tex, SCE_GXM_TEXTURE_ADDR_CLAMP);
    sceGxmTextureSetVAddrMode(&texture.gxm_tex, SCE_GXM_TEXTURE_ADDR_CLAMP);

    if ((q = v2d_ui_alloc(v, 4 * sizeof(*q), 16)) == NULL)
        return;
    q[0] = (vita2d_texture_vertex){ x0, y0, 0.5f, cu0 / tex_w, cv0 / tex_h };
    q[1] = (vita2d_texture_vertex){ x1, y0, 0.5f, cu1 / tex_w, cv0 / tex_h };
    q[2] = (vita2d_texture_vertex){ x0, y1, 0.5f, cu0 / tex_w, cv1 / tex_h };
    q[3] = (vita2d_texture_vertex){ x1, y1, 0.5f, cu1 / tex_w, cv1 / tex_h };
    vita2d_draw_array_textured(&texture, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, q, 4, 0xffffffff);
}

/*
 * UI textures come from psvita_ui_draw.c as linear CPU buffers of PSP-layout
 * texels (4444 or 5551, alpha in the top bits): the ABGR GXM formats. Without
 * blending the alpha is forced to 1.
 */
static void psvita_drawUISprite(void *data, void *tex, int tex_format, int tex_swizzled,
                                int tex_width, int tex_height, int tex_stride,
                                int su, int sv, int sw, int sh,
                                int dx, int dy, int dw, int dh, int blend)
{
    SceGxmTextureFormat format;
    (void)tex_swizzled;
    (void)tex_width;

    if (su < 0 || sv < 0 || sv + sh > tex_height || su + sw > tex_stride)
        return;
    if (tex_format == UI_PIXFMT_4444)
        format = blend ? SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_ABGR : SCE_GXM_TEXTURE_FORMAT_X4U4U4U4_1BGR;
    else
        format = blend ? SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ABGR : SCE_GXM_TEXTURE_FORMAT_X1U5U5U5_1BGR;
    v2d_ui_texture(data, tex, format, 2, tex_stride, su, sv, sw, sh, dx, dy, dw, dh, true, true);
}

static void v2d_ui_line(psvita_video_t *v, int x1, int y1, int x2, int y2,
                        uint32_t color1, uint32_t color2)
{
    /* hline()/vline() pass an exclusive end point: draw 1-pixel rectangles. */
    if (y1 == y2) {
        if (x2 < x1) {
            int t = x1; x1 = x2; x2 = t;
            uint32_t c = color1; color1 = color2; color2 = c;
        }
        v2d_ui_fill(v, x1, y1, x2 - x1, 1, color1, color2, color1, color2, true);
    } else if (x1 == x2) {
        if (y2 < y1) {
            int t = y1; y1 = y2; y2 = t;
            uint32_t c = color1; color1 = color2; color2 = c;
        }
        v2d_ui_fill(v, x1, y1, 1, y2 - y1, color1, color1, color2, color2, true);
    } else {
        vita2d_color_vertex *q;
        v2d_open_scene(v);
        if ((q = v2d_ui_alloc(v, 2 * sizeof(*q), 16)) == NULL)
            return;
        q[0] = (vita2d_color_vertex){ x1 + 0.5f, y1 + 0.5f, 0.5f, color1 };
        q[1] = (vita2d_color_vertex){ x2 + 0.5f, y2 + 0.5f, 0.5f, color2 };
        vita2d_draw_array(SCE_GXM_PRIMITIVE_LINES, q, 2);
    }
}

static void psvita_drawUILine(void *data, int x1, int y1, int x2, int y2, uint32_t color)
{
    v2d_ui_line(data, x1, y1, x2, y2, color, color);
}

static void psvita_drawUILineGradient(void *data, int x1, int y1, int x2, int y2,
                                      uint32_t color1, uint32_t color2)
{
    v2d_ui_line(data, x1, y1, x2, y2, color1, color2);
}

static void psvita_drawUIRect(void *data, int x, int y, int w, int h, uint32_t color)
{
    psvita_video_t *v = data;

    v2d_ui_fill(v, x, y, w, 1, color, color, color, color, true);
    v2d_ui_fill(v, x, y + h - 1, w, 1, color, color, color, color, true);
    v2d_ui_fill(v, x, y, 1, h, color, color, color, color, true);
    v2d_ui_fill(v, x + w - 1, y, 1, h, color, color, color, color, true);
}

static void psvita_fillUIRect(void *data, int x, int y, int w, int h, uint32_t color)
{
    v2d_ui_fill(data, x, y, w, h, color, color, color, color, true);
}

static void psvita_fillUIRectGradient(void *data, int x, int y, int w, int h,
                                      uint32_t color1, uint32_t color2, int direction)
{
    if (direction == UI_GRADIENT_HORIZONTAL)
        v2d_ui_fill(data, x, y, w, h, color1, color2, color1, color2, true);
    else
        v2d_ui_fill(data, x, y, w, h, color1, color1, color2, color2, true);
}

static void psvita_setUIScissor(void *data, int x, int y, int w, int h)
{
    psvita_video_t *v = data;
    int x1 = x + w, y1 = y + h;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > PSVITA_DISPLAY_WIDTH) x1 = PSVITA_DISPLAY_WIDTH;
    if (y1 > PSVITA_DISPLAY_HEIGHT) y1 = PSVITA_DISPLAY_HEIGHT;
    v->ui_clip = (RECT){ x, y, x1, y1 };
}

video_driver_t video_psvita_gxm = {
    .ident = "psvita",
    .init = psvita_init,
    .free = psvita_free,
    .waitVsync = psvita_waitVsync,
    .flipScreen = psvita_flipScreen,
    .beginFrame = psvita_beginFrame,
    .endFrame = psvita_endFrame,
    .frameAddr = psvita_frameAddr,
    .readFrame = psvita_readFrame,
    .getOutputSize = psvita_getOutputSize,
    .scissor = psvita_scissor,
    .clearScreen = psvita_clearScreen,
    .clearFrame = psvita_clearFrame,
    .fillFrame = psvita_fillFrame,
    .startWorkFrame = psvita_startWorkFrame,
    .transferWorkFrame = psvita_transferWorkFrame,
    .copyRect = psvita_copyRect,
    .copyRectFlip = psvita_copyRectFlip,
    .copyRectRotate = psvita_copyRectRotate,
    .drawTexture = psvita_drawTexture,
    .commitTextureUpdates = psvita_commitTextureUpdates,
    .uploadClut = psvita_uploadClut,
    .writeIndexedTextureRect = psvita_writeIndexedTextureRect,
    .writeDirectTextureRect = psvita_writeDirectTextureRect,
    .blitSpriteVertices = psvita_blitSpriteVertices,
    .blitPointVertices = psvita_blitPointVertices,
    .enableDepthTest = psvita_enableDepthTest,
    .disableDepthTest = psvita_disableDepthTest,
    .clearDepthBuffer = psvita_clearDepthBuffer,
    .clearColorBuffer = psvita_clearColorBuffer,
    .drawUISprite = psvita_drawUISprite,
    .drawUILine = psvita_drawUILine,
    .drawUILineGradient = psvita_drawUILineGradient,
    .drawUIRect = psvita_drawUIRect,
    .fillUIRect = psvita_fillUIRect,
    .fillUIRectGradient = psvita_fillUIRectGradient,
    .setUIScissor = psvita_setUIScissor,
    .flushAndWait = NULL,
    .getNativeContext = NULL,
};
