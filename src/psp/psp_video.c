/******************************************************************************

		video.c

		PSP Video Control Functions

******************************************************************************/

#include "psp_video.h"
#include <pspge.h> // for sceGeEdramGetAddr
#include <stdint.h>
#include <assert.h>

/******************************************************************************
		Local Variables/Structures
******************************************************************************/

static const ScePspIMatrix4 dither_matrix = {
	// Bayer dither
	{0, 8, 2, 10},
	{12, 4, 14, 6},
	{3, 11, 1, 9},
	{15, 7, 13, 5}};

static int pixel_format = GU_PSM_5551;

typedef struct texture_layer
{
	uint8_t *buffer;
	uint16_t width;
	uint16_t height;
	uint16_t stride; // actual allocated stride (next power of two of width)
	uint8_t bytes_per_pixel;
} texture_layer_t;

typedef struct psp_video
{
	uintptr_t show_frame; // Relative VRAM offset
	uintptr_t draw_frame; // Relative VRAM offset
	uintptr_t scrbitmap;  // Relative VRAM offset
	texture_layer_t *tex_layers;
	uint8_t tex_layers_count;
	uint8_t *texturesMem;
	uint16_t *clut_base;

	texture_layer_t *current_tex_layer;
	uint16_t *current_clut;
} psp_video_t;

static uint16_t next_pow2(uint16_t v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v++;
	return v;
}

static void *psp_init(layer_texture_info_t *layer_textures,
					  uint8_t layer_textures_count, clut_info_t *clut_info)
{
	psp_video_t *psp = (psp_video_t *)calloc(1, sizeof(psp_video_t));
	psp->clut_base = clut_info->base;
	uint32_t framesize = BUF_WIDTH * SCR_HEIGHT * 2;
	uint8_t *vram_base = (uint8_t *)sceGeEdramGetAddr();
	uintptr_t offset = 0;
// Align to 64 bytes for GU
#define VRAM_ALIGN 64
#define VRAM_ALIGN_UP(x) (((x) + (VRAM_ALIGN - 1)) & ~(VRAM_ALIGN - 1))

	offset = VRAM_ALIGN_UP(offset);
	psp->show_frame = offset; // Store relative offset
	offset += framesize;
	offset = VRAM_ALIGN_UP(offset);
	psp->draw_frame = offset; // Store relative offset
	offset += framesize;
	offset = VRAM_ALIGN_UP(offset);
	psp->scrbitmap = offset; // Store relative offset
	offset += framesize;
	offset = VRAM_ALIGN_UP(offset);

	// Original buffers containing clut indexes
	psp->tex_layers =
		(texture_layer_t *)calloc(layer_textures_count, sizeof(texture_layer_t));
	psp->tex_layers_count = layer_textures_count;
	for (int i = 0; i < layer_textures_count; i++)
	{
		uint16_t stride = next_pow2(layer_textures[i].width);
		size_t texsize = stride * layer_textures[i].height;
		psp->tex_layers[i].buffer = vram_base + offset;
		psp->tex_layers[i].width = layer_textures[i].width;
		psp->tex_layers[i].height = layer_textures[i].height;
		psp->tex_layers[i].stride = stride;
		psp->tex_layers[i].bytes_per_pixel = layer_textures[i].bytes_per_pixel;
		offset += VRAM_ALIGN_UP(texsize * layer_textures[i].bytes_per_pixel);
		offset = VRAM_ALIGN_UP(offset);
		printf("[PSP_VIDEO] tex_layer[%d]: buffer=%p width=%u height=%u stride=%u "
			   "size=%zu\n",
			   i, psp->tex_layers[i].buffer, psp->tex_layers[i].width,
			   psp->tex_layers[i].height, psp->tex_layers[i].stride,
			   (size_t)(stride * layer_textures[i].height * layer_textures[i].bytes_per_pixel));
	}

	printf("[PSP_VIDEO] show_frame=0x%lx draw_frame=0x%lx scrbitmap=0x%lx\n",
		   (unsigned long)psp->show_frame, (unsigned long)psp->draw_frame,
		   (unsigned long)psp->scrbitmap);
	printf("[PSP_VIDEO] VRAM base=%p, total offset used=%lu bytes\n", vram_base,
		   (unsigned long)offset);

	sceGuInit();
	sceGuDisplay(GU_FALSE);

	sceGuStart(GU_DIRECT, gulist);
	sceGuDrawBuffer(pixel_format, (void *)psp->draw_frame, BUF_WIDTH);
	sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, (void *)psp->show_frame, BUF_WIDTH);
	sceGuOffset(2048 - (SCR_WIDTH / 2), 2048 - (SCR_HEIGHT / 2));
	sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);

	sceGuEnable(GU_SCISSOR_TEST);
	sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);

	sceGuDisable(GU_ALPHA_TEST);
	sceGuAlphaFunc(GU_LEQUAL, 0, 0x01);

	sceGuDisable(GU_BLEND);
	sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

	sceGuDisable(GU_DEPTH_TEST);
	sceGuDepthRange(65535, 0);
	sceGuDepthFunc(GU_GEQUAL);
	sceGuDepthMask(GU_TRUE);

	sceGuEnable(GU_TEXTURE_2D);
	sceGuTexMode(pixel_format, 0, 0, GU_FALSE);
	sceGuTexScale(1.0f / BUF_WIDTH, 1.0f / BUF_WIDTH);
	sceGuTexOffset(0, 0);
	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);

	sceGuClutMode(GU_PSM_5551, 0, 0xff, 0);

	sceGuSetDither(&dither_matrix);
	sceGuDisable(GU_DITHER);

	sceGuClearDepth(0);
	sceGuClearColor(0);

	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);

	sceGuStart(GU_DIRECT, gulist);
	video_driver->clearFrame(psp, COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER);
	video_driver->clearFrame(psp, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER);
	video_driver->clearFrame(psp, COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP);
	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);

	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);

	ui_init();

	psp->current_tex_layer = NULL;
	psp->current_clut = NULL;

	return psp;
}

/*--------------------------------------------------------
		Video Processing Termination (Common)
--------------------------------------------------------*/

static void psp_exit()
{
	sceGuDisplay(GU_FALSE);
	sceGuTerm();
}

static void psp_free(void *data)
{
	psp_video_t *psp = (psp_video_t *)data;

	psp_exit();
	free(psp);
}

/*--------------------------------------------------------
		Wait for VSYNC
--------------------------------------------------------*/

static void psp_waitVsync(void *data) { sceDisplayWaitVblankStart(); }

/*--------------------------------------------------------
		Flip Screen
--------------------------------------------------------*/

static void psp_flipScreen(void *data, bool vsync)
{
	psp_video_t *psp = (psp_video_t *)data;

	if (vsync)
		sceDisplayWaitVblankStart();
	psp->show_frame = psp->draw_frame;
	psp->draw_frame = (uintptr_t)sceGuSwapBuffers();
}

/*--------------------------------------------------------
		Get VRAM Address
--------------------------------------------------------*/

static void *psp_frameAddr(void *data, void *frame, int x, int y)
{
	// Buffers are stored as relative offsets, convert to pointer
	return (void *)((uintptr_t)frame + ((x + (y << 9)) << 1));
}

static void *psp_workFrame(void *data)
{
	psp_video_t *psp = (psp_video_t *)data;
	return (void *)psp->scrbitmap;
}

static void *psp_textureLayer(void *data, uint8_t layerIndex)
{
	psp_video_t *psp = (psp_video_t *)data;
	return psp->tex_layers[layerIndex].buffer;
}

static void psp_scissor(void *data, uint16_t left, uint16_t top, uint16_t right, uint16_t bottom)
{
	sceGuScissor(left, top, right - left, bottom - top);
}

/*--------------------------------------------------------
		Clear Draw/Display Frame
--------------------------------------------------------*/

static void psp_clearScreen(void *data)
{
	psp_video_t *psp = (psp_video_t *)data;
	video_driver->clearFrame(psp, COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER);
	video_driver->clearFrame(psp, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER);
}

/*--------------------------------------------------------
		Clear Specified Frame
--------------------------------------------------------*/

static void psp_clearFrame(void *data, int index) {
	psp_video_t *psp = (psp_video_t *)data;
	void *frame = NULL;
	uint16_t width = 0;
	uint16_t height = 0;
	uint16_t buffer_width = BUF_WIDTH;
	switch (index) {
	case COMMON_GRAPHIC_OBJECTS_GLOBAL_CONTEXT:
		assert("Cannot clear global context");
		break;
	case COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER:
		frame = (void *)psp->show_frame;
		width = SCR_WIDTH;
		height = SCR_HEIGHT;
		buffer_width = BUF_WIDTH;
		break;
	case COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER:
		frame = (void *)psp->draw_frame;
		width = SCR_WIDTH;
		height = SCR_HEIGHT;
		buffer_width = BUF_WIDTH;
		break;
	case COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP:
		frame = (void *)psp->scrbitmap;
		width = SCR_WIDTH;
		height = SCR_HEIGHT;
		buffer_width = BUF_WIDTH;
		break;
	default:
		assert("Shouldn't clear texture layers");
		break;
	}

	sceGuDrawBufferList(pixel_format, frame, buffer_width);
	sceGuScissor(0, 0, width, height);
	sceGuClearColor(0);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);
}

/*--------------------------------------------------------
		Fill Specified Frame
--------------------------------------------------------*/

static void psp_fillFrame(void *data, void *frame, uint32_t color)
{
	sceGuDrawBufferList(pixel_format, frame, BUF_WIDTH);
	sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
	sceGuClearColor(color);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);
}

/*--------------------------------------------------------
		Copy Rectangular Area
--------------------------------------------------------*/

static void psp_copyRect(void *data, void *src, void *dst, RECT *src_rect,
						 RECT *dst_rect)
{
	int j, sw, dw, sh, dh;
	struct Vertex *vertices;

	sw = src_rect->right - src_rect->left;
	dw = dst_rect->right - dst_rect->left;
	sh = src_rect->bottom - src_rect->top;
	dh = dst_rect->bottom - dst_rect->top;

	// sceGuStart(GU_DIRECT, gulist);

	sceGuDrawBufferList(pixel_format, dst, BUF_WIDTH);
	sceGuScissor(dst_rect->left, dst_rect->top, dst_rect->right,
				 dst_rect->bottom);
	sceGuDisable(GU_ALPHA_TEST);

	sceGuTexMode(pixel_format, 0, 0, GU_FALSE);
	sceGuTexImage(0, BUF_WIDTH, BUF_WIDTH, BUF_WIDTH, GU_FRAME_ADDR(src));
	if (sw == dw && sh == dh)
		sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	else
		sceGuTexFilter(GU_LINEAR, GU_LINEAR);

	for (j = 0; (j + SLICE_SIZE) < sw; j = j + SLICE_SIZE)
	{
		vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

		vertices[0].u = src_rect->left + j;
		vertices[0].v = src_rect->top;
		vertices[0].x = dst_rect->left + j * dw / sw;
		vertices[0].y = dst_rect->top;

		vertices[1].u = src_rect->left + j + SLICE_SIZE;
		vertices[1].v = src_rect->bottom;
		vertices[1].x = dst_rect->left + (j + SLICE_SIZE) * dw / sw;
		vertices[1].y = dst_rect->bottom;

		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	}

	if (j < sw)
	{
		vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

		vertices[0].u = src_rect->left + j;
		vertices[0].v = src_rect->top;
		vertices[0].x = dst_rect->left + j * dw / sw;
		vertices[0].y = dst_rect->top;

		vertices[1].u = src_rect->right;
		vertices[1].v = src_rect->bottom;
		vertices[1].x = dst_rect->right;
		vertices[1].y = dst_rect->bottom;

		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	}

	// sceGuFinish();
	// sceGuSync(0, GU_SYNC_FINISH);
}

static void psp_startWorkFrame(void *data, uint32_t color)
{
	psp_video_t *psp = (psp_video_t *)data;

	sceKernelDcacheWritebackRange(gulist, GULIST_SIZE);
	sceGuStart(GU_DIRECT, gulist);
	sceGuDrawBufferList(GU_PSM_5551, (void *)psp->draw_frame, BUF_WIDTH);
	sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);

	sceGuDrawBufferList(GU_PSM_5551, (void *)psp->scrbitmap, BUF_WIDTH);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);

	sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
	sceGuClearColor(color);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);

	sceGuClearColor(0);
	sceGuEnable(GU_ALPHA_TEST);
	sceGuTexMode(GU_PSM_T8, 0, 0, GU_TRUE);
	sceGuTexFilter(GU_NEAREST, GU_NEAREST);

	// sceGuFinish();
	// sceGuSync(0, GU_SYNC_FINISH);
}

static void psp_transferWorkFrame(void *data, RECT *src_rect, RECT *dst_rect)
{
	psp_video_t *psp = (psp_video_t *)data;
	psp_copyRect(psp, (void *)psp->scrbitmap, (void *)psp->draw_frame, src_rect,
				 dst_rect);

	size_t listSize = sceGuFinish();
	u_int64_t tick = ticker_driver->currentUs(ticker_data);
	sceGuSync(0, GU_SYNC_FINISH);
	// printf("Transfer Work Frame GU List Size: %zu bytes, time: %llu us\n",
	// listSize, ticker_driver->currentUs(ticker_data) - tick);
	psp->current_tex_layer = NULL;
	psp->current_clut = NULL;
}
/*--------------------------------------------------------
		Copy Rectangular Area with Horizontal Flip
--------------------------------------------------------*/

static void psp_copyRectFlip(void *data, void *src, void *dst, RECT *src_rect,
							 RECT *dst_rect)
{
	int16_t j, sw, dw, sh, dh;
	struct Vertex *vertices;

	sw = src_rect->right - src_rect->left;
	dw = dst_rect->right - dst_rect->left;
	sh = src_rect->bottom - src_rect->top;
	dh = dst_rect->bottom - dst_rect->top;

	// sceGuStart(GU_DIRECT, gulist);

	sceGuDrawBufferList(pixel_format, dst, BUF_WIDTH);
	sceGuScissor(dst_rect->left, dst_rect->top, dst_rect->right,
				 dst_rect->bottom);
	sceGuDisable(GU_ALPHA_TEST);

	sceGuTexMode(pixel_format, 0, 0, GU_FALSE);
	sceGuTexImage(0, 512, 512, BUF_WIDTH, GU_FRAME_ADDR(src));
	if (sw == dw && sh == dh)
		sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	else
		sceGuTexFilter(GU_LINEAR, GU_LINEAR);

	for (j = 0; (j + SLICE_SIZE) < sw; j = j + SLICE_SIZE)
	{
		vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

		vertices[0].u = src_rect->left + j;
		vertices[0].v = src_rect->top;
		vertices[0].x = dst_rect->right - j * dw / sw;
		vertices[0].y = dst_rect->bottom;

		vertices[1].u = src_rect->left + j + SLICE_SIZE;
		vertices[1].v = src_rect->bottom;
		vertices[1].x = dst_rect->right - (j + SLICE_SIZE) * dw / sw;
		vertices[1].y = dst_rect->top;

		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	}

	if (j < sw)
	{
		vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

		vertices[0].u = src_rect->left + j;
		vertices[0].v = src_rect->top;
		vertices[0].x = dst_rect->right - j * dw / sw;
		vertices[0].y = dst_rect->bottom;

		vertices[1].u = src_rect->right;
		vertices[1].v = src_rect->bottom;
		vertices[1].x = dst_rect->left;
		vertices[1].y = dst_rect->top;

		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	}

	// sceGuFinish();
	// sceGuSync(0, GU_SYNC_FINISH);
}

/*--------------------------------------------------------
		Copy Rectangular Area with 270-degree Rotation
--------------------------------------------------------*/

static void psp_copyRectRotate(void *data, void *src, void *dst, RECT *src_rect,
							   RECT *dst_rect)
{
	int16_t j, sw, dw, sh, dh;
	struct Vertex *vertices;

	sw = src_rect->right - src_rect->left;
	dw = dst_rect->right - dst_rect->left;
	sh = src_rect->bottom - src_rect->top;
	dh = dst_rect->bottom - dst_rect->top;

	// sceGuStart(GU_DIRECT, gulist);

	sceGuDrawBufferList(pixel_format, dst, BUF_WIDTH);
	sceGuScissor(dst_rect->left, dst_rect->top, dst_rect->right,
				 dst_rect->bottom);
	sceGuDisable(GU_ALPHA_TEST);

	sceGuTexMode(pixel_format, 0, 0, GU_FALSE);
	sceGuTexImage(0, 512, 512, BUF_WIDTH, GU_FRAME_ADDR(src));
	if (sw == dh && sh == dw)
		sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	else
		sceGuTexFilter(GU_LINEAR, GU_LINEAR);

	vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

	for (j = 0; (j + SLICE_SIZE) < sw; j = j + SLICE_SIZE)
	{
		vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

		vertices[0].u = src_rect->right - j;
		vertices[0].v = src_rect->bottom;
		vertices[0].x = dst_rect->right;
		vertices[0].y = dst_rect->top - j * dh / sw;

		vertices[1].u = src_rect->right - j + SLICE_SIZE;
		vertices[1].v = src_rect->top;
		vertices[1].x = dst_rect->right;
		vertices[1].y = dst_rect->bottom - (j + SLICE_SIZE) * dh / sw;

		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	}

	if (j < sw)
	{
		vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

		vertices[0].u = src_rect->right + j;
		vertices[0].v = src_rect->bottom;
		vertices[0].x = dst_rect->right;
		vertices[0].y = dst_rect->top - j * dh / sw;

		vertices[1].u = src_rect->left;
		vertices[1].v = src_rect->top;
		vertices[1].x = dst_rect->left;
		vertices[1].y = dst_rect->bottom;

		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	}

	// sceGuFinish();
	// sceGuSync(0, GU_SYNC_FINISH);
}

/*--------------------------------------------------------
		Draw Texture with Specified Rectangular Area
--------------------------------------------------------*/

static void psp_drawTexture(void *data, uint32_t src_fmt, uint32_t dst_fmt,
							void *src, void *dst, RECT *src_rect,
							RECT *dst_rect)
{
	int j, sw, dw, sh, dh;
	struct Vertex *vertices;

	sw = src_rect->right - src_rect->left;
	dw = dst_rect->right - dst_rect->left;
	sh = src_rect->bottom - src_rect->top;
	dh = dst_rect->bottom - dst_rect->top;

	// sceGuStart(GU_DIRECT, gulist);
	sceGuDrawBufferList(dst_fmt, dst, BUF_WIDTH);
	sceGuScissor(dst_rect->left, dst_rect->top, dst_rect->right,
				 dst_rect->bottom);

	sceGuTexMode(src_fmt, 0, 0, GU_FALSE);
	sceGuTexImage(0, BUF_WIDTH, BUF_WIDTH, BUF_WIDTH, GU_FRAME_ADDR(src));
	if (sw == dw && sh == dh)
		sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	else
		sceGuTexFilter(GU_LINEAR, GU_LINEAR);

	for (j = 0; (j + SLICE_SIZE) < sw; j = j + SLICE_SIZE)
	{
		vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

		vertices[0].u = src_rect->left + j;
		vertices[0].v = src_rect->top;
		vertices[0].x = dst_rect->left + j * dw / sw;
		vertices[0].y = dst_rect->top;

		vertices[1].u = src_rect->left + j + SLICE_SIZE;
		vertices[1].v = src_rect->bottom;
		vertices[1].x = dst_rect->left + (j + SLICE_SIZE) * dw / sw;
		vertices[1].y = dst_rect->bottom;

		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	}

	if (j < sw)
	{
		vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

		vertices[0].u = src_rect->left + j;
		vertices[0].v = src_rect->top;
		vertices[0].x = dst_rect->left + j * dw / sw;
		vertices[0].y = dst_rect->top;

		vertices[1].u = src_rect->right;
		vertices[1].v = src_rect->bottom;
		vertices[1].x = dst_rect->right;
		vertices[1].y = dst_rect->bottom;

		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	}

	// sceGuFinish();
	// sceGuSync(0, GU_SYNC_FINISH);
}

static void *psp_getNativeObjects(void *data, int index) { return NULL; }

static void psp_uploadMem(void *data, uint8_t textureIndex)
{
	psp_video_t *psp = (psp_video_t *)data;
	texture_layer_t *layer = &psp->tex_layers[textureIndex];
	size_t size = layer->stride * layer->height * layer->bytes_per_pixel;
	sceKernelDcacheWritebackRange(layer->buffer, size);
}

static void psp_uploadClut(void *data, uint16_t *clut, uint8_t bank_index)
{
	/* Flush the actual CLUT bank pointer that will be used by sceGuClutLoad */
	size_t size = 256 * sizeof(uint16_t);
	sceKernelDcacheWritebackRange(clut, size);
}

static void psp_blitTexture(void *data, uint8_t textureIndex, void *clut,
							uint8_t clut_index, uint32_t vertices_count,
							void *vertices)
{
	psp_video_t *psp = (psp_video_t *)data;
	texture_layer_t *layer = &psp->tex_layers[textureIndex];
	if (psp->current_tex_layer != layer) {
		psp->current_tex_layer = layer;
		int is_indexed = (layer->bytes_per_pixel == 1);
		sceGuTexMode(is_indexed ? GU_PSM_T8 : GU_PSM_5551, 0, 0, is_indexed ? GU_TRUE : GU_FALSE);
		sceGuTexImage(0, layer->width, layer->height, layer->stride, layer->buffer);
	}
	if (clut != NULL && psp->current_clut != (uint16_t *)clut) {
		psp->current_clut = (uint16_t *)clut;
		sceGuClutLoad(256 / 8, clut);
	}
	sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, vertices_count, NULL, vertices);
}

static void psp_blitPoints(void *data, uint32_t points_count, void *vertices)
{
	sceGuDisable(GU_TEXTURE_2D);
	sceGuDisable(GU_ALPHA_TEST);
	sceGuDrawArray(GU_POINTS, GU_COLOR_5551 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, points_count, NULL, vertices);
	sceGuEnable(GU_TEXTURE_2D);
	sceGuEnable(GU_ALPHA_TEST);
}

static void psp_flushCache(void *data, void *addr, size_t size)
{
	size_t aligned_size = (size + 63) & ~63;
	sceKernelDcacheWritebackRange(addr, aligned_size);
}

video_driver_t video_psp = {
	"psp",
	psp_init,
	psp_free,
	psp_waitVsync,
	psp_flipScreen,
	psp_frameAddr,
	psp_workFrame,
	psp_textureLayer,
	psp_scissor,
	psp_clearScreen,
	psp_clearFrame,
	psp_fillFrame,
	psp_startWorkFrame,
	psp_transferWorkFrame,
	psp_copyRect,
	psp_copyRectFlip,
	psp_copyRectRotate,
	psp_drawTexture,
	psp_getNativeObjects,
	psp_uploadMem,
	psp_uploadClut,
	psp_blitTexture,
	psp_blitPoints,
	psp_flushCache,
};