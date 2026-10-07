#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mvs/mvs.h"
#include "mvs/sprite_common.h"
#include "common/cache.h"
#include "common/video_driver.h"

uint32_t frames_displayed;
int option_display_mode;
uint8_t palette_bank;
uint16_t video_palettebank[2][4096];
uint16_t *video_palette = video_palettebank[0];
static uint8_t source_tiles[4096 * 128] __attribute__((aligned(16)));
uint8_t *memory_region_gfx3 = source_tiles;
uint8_t *fix_memory = source_tiles;
void *video_data;
void (*update_cache)(uint32_t);
static unsigned reads, uploads[TEXTURE_LAYER_COUNT], draws, vertices_drawn;
static uint8_t cpu_atlas[TEXTURE_LAYER_COUNT][512 * 512];
static uint8_t gpu_atlas[TEXTURE_LAYER_COUNT][512 * 512];
static unsigned layer_order[32], order_count;

void video_get_pixel_aspect_ratio(int *numerator, int *denominator)
{
    if (numerator) *numerator = 1;
    if (denominator) *denominator = 1;
}

static uint32_t read_tile(uint32_t offset)
{
    assert(offset + 128 <= sizeof(source_tiles));
    reads++;
    return offset;
}
uint32_t (*read_cache)(uint32_t) = read_tile;
static void noop(void *data) { (void)data; }
static void start_work(void *data, uint32_t color) { (void)data; (void)color; }
static void scissor(void *data, uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{ (void)data; (void)x; (void)y; (void)w; (void)h; }
static void upload_clut(void *data, uint16_t *colors, uint8_t bank)
{ (void)data; (void)colors; (void)bank; }
static void write_pixels(void *data, uint8_t layer, int x, int y, int w, int h,
    const uint8_t *pixels, int pitch)
{
    (void)data;
    for (int row = 0; row < h; row++)
        memcpy(cpu_atlas[layer] + (y + row) * 512 + x, pixels + row * pitch, w);
}
static void upload(void *data, uint8_t layer)
{
    (void)data;
    uploads[layer]++;
    memcpy(gpu_atlas[layer], cpu_atlas[layer], sizeof(cpu_atlas[layer]));
}
static void draw(void *data, uint8_t layer, const uint16_t *colors, uint8_t bank,
    uint32_t count, const video_sprite_vertex_t *vertices)
{
    (void)data; (void)colors; (void)bank;
    assert(count && !(count & 1));
    /* Every sampled rectangle must already have current pixels on the GPU. */
    for (unsigned i = 0; i < count; i += 2) {
        unsigned x = vertices[i].u < vertices[i+1].u ? vertices[i].u : vertices[i+1].u;
        unsigned y = vertices[i].v < vertices[i+1].v ? vertices[i].v : vertices[i+1].v;
        for (unsigned row = 0; row < 16; row++)
            assert(memcmp(cpu_atlas[layer] + (y + row) * 512 + x,
                gpu_atlas[layer] + (y + row) * 512 + x, 16) == 0);
    }
    draws++;
    vertices_drawn += count;
    if (order_count < 32) layer_order[order_count++] = layer;
}
static video_driver_t mock = {
    .beginFrame = noop, .startWorkFrame = start_work, .scissor = scissor,
    .uploadClut = upload_clut, .writeIndexedTextureRect = write_pixels,
    .commitTextureUpdates = upload, .blitSpriteVertices = draw,
};
video_driver_t *video_driver = &mock;

static void sprite(unsigned code) { blit_draw_spr(24, 16, 16, 16, code, 0); }
static void start_frame(void)
{
    frames_displayed++;
    blit_start(FIRST_VISIBLE_LINE, LAST_VISIBLE_LINE);
}
int main(void)
{
    memset(source_tiles, 0x35, sizeof(source_tiles));
    blit_reset();
    start_frame();
    sprite(0);
    blit_finish_spr();
#if USE_CACHE
    assert(reads == 1);
#else
    assert(reads == 0);
#endif
    assert(uploads[0] == 1 && vertices_drawn == 2);
    /* Same tile next frame: draw it again but no texture upload or ROM read. */
    start_frame();
    sprite(0);
    blit_finish_spr();
#ifdef MVS_DIRTY_SPRITE_UPLOADS
    assert(uploads[0] == 1);
#else
    assert(uploads[0] == 2);
#endif
#if USE_CACHE
    assert(reads == 1);
#else
    assert(reads == 0);
#endif
    assert(vertices_drawn == 4);
    unsigned before = uploads[0];
    /* A later raster batch writes a new tile in the same atlas. */
    blit_start(FIRST_VISIBLE_LINE + 1, LAST_VISIBLE_LINE);
    sprite(1);
    blit_finish_spr();
    assert(uploads[0] == before + 1);
#if USE_CACHE
    assert(reads == 2);
#else
    assert(reads == 0);
#endif
    /* Palette bank changes do not change indexed pixel data. */
    before = uploads[0];
    palette_bank = 1;
    start_frame();
    sprite(1);
    blit_finish_spr();
#ifdef MVS_DIRTY_SPRITE_UPLOADS
    assert(uploads[0] == before);
#else
    assert(uploads[0] == before + 1);
#endif
    /* Cross an atlas boundary, then revisit it in the original draw order. */
    start_frame();
    for (unsigned i = 2; i <= 1024; i++) sprite(i);
    blit_finish_spr();
    assert(uploads[1] == 1);
    start_frame();
    order_count = 0;
    sprite(0); sprite(1024); sprite(1);
    unsigned u0 = uploads[0], u1 = uploads[1];
    blit_finish_spr();
    assert(order_count == 3 && layer_order[0] == 0 && layer_order[1] == 1 && layer_order[2] == 0);
#ifdef MVS_DIRTY_SPRITE_UPLOADS
    assert(uploads[0] == u0 && uploads[1] == u1);
#else
    assert(uploads[0] == u0 + 1 && uploads[1] == u1 + 1);
#endif
    /* Invalidating the tile cache reuses slot zero, which must be uploaded. */
    blit_set_spr_clear_flag();
    start_frame();
    unsigned invalidated_layer = (2048 / SPR_TEXTURE_LAYER_SIZE) % SPR_TEXTURE_LAYERS;
    before = uploads[invalidated_layer];
    source_tiles[2048 * 128] = 0x76;
    sprite(2048);
    blit_finish_spr();
    assert(uploads[invalidated_layer] == before + 1);
    /* Full batches reject new tiles without causing a C-ROM read. */
    start_frame();
    for (unsigned i = 0; i < SPR_MAX_SPRITES; i++) sprite(2048);
    unsigned before_reads = reads;
    sprite(2049);
    assert(reads == before_reads);
    blit_finish_spr();
    /* Reset must invalidate residency bookkeeping too. */
    blit_reset();
    start_frame();
    before = uploads[0];
    sprite(0);
    blit_finish_spr();
    assert(uploads[0] == before + 1);
    printf("PASS: resident tiles, raster updates, palette changes, atlas order, invalidation, batch limit, reset (%u draws)\n", draws);
    return 0;
}
