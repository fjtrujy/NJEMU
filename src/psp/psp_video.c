/******************************************************************************

	video.c

	PSP Video Control Functions

******************************************************************************/

#include "psp_video.h"


/******************************************************************************
	Local Variables/Structures
******************************************************************************/

static const ScePspIMatrix4 dither_matrix =
{
	// Bayer dither
	{  0,  8,  2, 10 },
	{ 12,  4, 14,  6 },
	{  3, 11,  1,  9 },
	{ 15,  7, 13,  5 }
};

static int pixel_format;


/******************************************************************************
	Global Functions
******************************************************************************/

typedef struct psp_video {
	// Base clut starting address
	uint16_t *clut_base;
} psp_video_t;

/*--------------------------------------------------------
	Video Processing Initialization
--------------------------------------------------------*/
static void psp_start(void *data) {
	#if VIDEO_32BPP
	if (video_mode == 32)
	{
		pixel_format = GU_PSM_8888;

		show_frame = (void *)(FRAMESIZE32 * 0);
		draw_frame = (void *)(FRAMESIZE32 * 1);
		work_frame = (void *)(FRAMESIZE32 * 2);
		tex_frame  = (void *)(FRAMESIZE32 * 3);
	}
	else
#endif
	{
		pixel_format = GU_PSM_5551;

		show_frame = (void *)(FRAMESIZE * 0);
		draw_frame = (void *)(FRAMESIZE * 1);
		work_frame = (void *)(FRAMESIZE * 2);
		tex_frame  = (void *)(FRAMESIZE * 3);
	}

	sceGuDisplay(GU_FALSE);
	sceGuInit();

	sceGuStart(GU_DIRECT, gulist);
	sceGuDrawBuffer(pixel_format, draw_frame, BUF_WIDTH);
	sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, show_frame, BUF_WIDTH);
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

	video_driver->clearFrame(data, show_frame);
	video_driver->clearFrame(data, draw_frame);
	video_driver->clearFrame(data, work_frame);

	sceDisplayWaitVblankStart();
	sceGuDisplay(GU_TRUE);

	ui_init();
}

static void *psp_init(void)
{
	psp_video_t *psp = (psp_video_t*)calloc(1, sizeof(psp_video_t));

	psp_start(psp);
	return psp;
}


/*--------------------------------------------------------
	Video Processing Termination (Common)
--------------------------------------------------------*/

static void psp_exit() {
	sceGuDisplay(GU_FALSE);
	sceGuTerm();
}

static void psp_free(void *data)
{
	psp_video_t *psp = (psp_video_t*)data;
	
	psp_exit();
	free(psp);
}

/*--------------------------------------------------------
	Video Mode Setting
--------------------------------------------------------*/


static void psp_setMode(void *data, int mode)
{
#if VIDEO_32BPP
	if (video_mode != mode)
	{
		if (video_mode) psp_exit();

		video_mode = mode;

		psp_start(data);
	}
#endif
}

static void psp_setClutBaseAddr(void *data, uint16_t *clut_base)
{
	psp_video_t *psp = (psp_video_t*)data;
	psp->clut_base = clut_base;
}

/*--------------------------------------------------------
	Wait for VSYNC
--------------------------------------------------------*/

static void psp_waitVsync(void *data)
{
	sceDisplayWaitVblankStart();
}


/*--------------------------------------------------------
	Flip Screen
--------------------------------------------------------*/

static void psp_flipScreen(void *data, bool vsync)
{
	if (vsync) sceDisplayWaitVblankStart();
	show_frame = draw_frame;
	draw_frame = sceGuSwapBuffers();
}


/*--------------------------------------------------------
	Get VRAM Address
--------------------------------------------------------*/

static void *psp_frameAddr(void *data, void *frame, int x, int y)
{
#if VIDEO_32BPP
	if (video_mode == 32)
		return (void *)(((uint32_t)frame | 0x44000000) + ((x + (y << 9)) << 2));
	else
#endif
		return (void *)(((uint32_t)frame | 0x44000000) + ((x + (y << 9)) << 1));
}

static void *psp_workFrame(void *data, enum WorkBuffer buffer)
{
	uint16_t *scrbitmap_tmp  = (uint16_t *)psp_frameAddr(data, work_frame, 0, 0);
	uint8_t *tex_spr0 = (uint8_t *)(scrbitmap_tmp + BUF_WIDTH * SCR_HEIGHT);
	uint8_t *tex_spr1 = tex_spr0 + BUF_WIDTH * TEXTURE_HEIGHT;
	uint8_t *tex_spr2 = tex_spr1 + BUF_WIDTH * TEXTURE_HEIGHT;
	uint8_t *tex_fix = tex_spr2 + BUF_WIDTH * TEXTURE_HEIGHT;
	switch (buffer)
	{
		case SCRBITMAP:
			return scrbitmap_tmp;
		case TEX_SPR0:
			return tex_spr0;
		case TEX_SPR1:
			return tex_spr1;
		case TEX_SPR2:
			return tex_spr2;
		case TEX_FIX:
			return tex_fix;
	}

	return NULL;
}


/*--------------------------------------------------------
	Clear Draw/Display Frame
--------------------------------------------------------*/

static void psp_clearScreen(void *data)
{
	video_driver->clearFrame(data, show_frame);
	video_driver->clearFrame(data, draw_frame);
}

/*--------------------------------------------------------
	Clear Specified Frame
--------------------------------------------------------*/

static void psp_clearFrame(void *data, void *frame)
{
	sceGuStart(GU_DIRECT, gulist);
	sceGuDrawBufferList(pixel_format, frame, BUF_WIDTH);
	sceGuScissor(0, 0, BUF_WIDTH, SCR_HEIGHT);
	sceGuClearColor(0);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);
	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);
}


/*--------------------------------------------------------
	Fill Specified Frame
--------------------------------------------------------*/

static void psp_fillFrame(void *data, void *frame, uint32_t color)
{
	sceGuStart(GU_DIRECT, gulist);
	sceGuDrawBufferList(pixel_format, frame, BUF_WIDTH);
	sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
	sceGuClearColor(color);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);
	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);
}


/*--------------------------------------------------------
	Copy Rectangular Area
--------------------------------------------------------*/

static void psp_copyRect(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
	int j, sw, dw, sh, dh;
	struct Vertex *vertices;

	sw = src_rect->right - src_rect->left;
	dw = dst_rect->right - dst_rect->left;
	sh = src_rect->bottom - src_rect->top;
	dh = dst_rect->bottom - dst_rect->top;

	sceGuStart(GU_DIRECT, gulist);

	sceGuDrawBufferList(pixel_format, dst, BUF_WIDTH);
	sceGuScissor(dst_rect->left, dst_rect->top, dst_rect->right, dst_rect->bottom);
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

	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);
}

static void psp_startWorkFrame(void *data, uint32_t color) {
	sceGuStart(GU_DIRECT, gulist);
	sceGuDrawBufferList(GU_PSM_5551, draw_frame, BUF_WIDTH);
	sceGuScissor(0, 0, BUF_WIDTH, SCR_WIDTH);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);

	sceGuDrawBufferList(GU_PSM_5551, work_frame, BUF_WIDTH);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);

	sceGuScissor(24, 16, 336, 240);
	sceGuClearColor(color);
	sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);

	sceGuClearColor(0);
	sceGuEnable(GU_ALPHA_TEST);
	sceGuTexMode(GU_PSM_T8, 0, 0, GU_TRUE);
	sceGuTexFilter(GU_NEAREST, GU_NEAREST);

	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);
}

static void psp_transferWorkFrame(void *data, RECT *src_rect, RECT *dst_rect) {
	psp_copyRect(data, work_frame, draw_frame, src_rect, dst_rect);
}
/*--------------------------------------------------------
	Copy Rectangular Area with Horizontal Flip
--------------------------------------------------------*/

static void psp_copyRectFlip(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
	int16_t j, sw, dw, sh, dh;
	struct Vertex *vertices;

	sw = src_rect->right - src_rect->left;
	dw = dst_rect->right - dst_rect->left;
	sh = src_rect->bottom - src_rect->top;
	dh = dst_rect->bottom - dst_rect->top;

	sceGuStart(GU_DIRECT, gulist);

	sceGuDrawBufferList(pixel_format, dst, BUF_WIDTH);
	sceGuScissor(dst_rect->left, dst_rect->top, dst_rect->right, dst_rect->bottom);
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

	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);
}


/*--------------------------------------------------------
	Copy Rectangular Area with 270-degree Rotation
--------------------------------------------------------*/

static void psp_copyRectRotate(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
	int16_t j, sw, dw, sh, dh;
	struct Vertex *vertices;

	sw = src_rect->right - src_rect->left;
	dw = dst_rect->right - dst_rect->left;
	sh = src_rect->bottom - src_rect->top;
	dh = dst_rect->bottom - dst_rect->top;

	sceGuStart(GU_DIRECT, gulist);

	sceGuDrawBufferList(pixel_format, dst, BUF_WIDTH);
	sceGuScissor(dst_rect->left, dst_rect->top, dst_rect->right, dst_rect->bottom);
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

	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);
}


/*--------------------------------------------------------
	Draw Texture with Specified Rectangular Area
--------------------------------------------------------*/

static void psp_drawTexture(void *data, uint32_t src_fmt, uint32_t dst_fmt, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
	int j, sw, dw, sh, dh;
	struct Vertex *vertices;

	sw = src_rect->right - src_rect->left;
	dw = dst_rect->right - dst_rect->left;
	sh = src_rect->bottom - src_rect->top;
	dh = dst_rect->bottom - dst_rect->top;

	sceGuStart(GU_DIRECT, gulist);
	sceGuDrawBufferList(dst_fmt, dst, BUF_WIDTH);
	sceGuScissor(dst_rect->left, dst_rect->top, dst_rect->right, dst_rect->bottom);

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

	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);
}

static void *psp_getNativeObjects(void *data, int index) {
	return NULL;
}

static void psp_uploadMem(void *data, enum WorkBuffer buffer) {
}

static void psp_uploadClut(void *data, uint16_t *bank, uint8_t bank_index) {
}

static void psp_blitTexture(void *data, enum WorkBuffer buffer, void *clut, uint8_t clut_index, uint32_t vertices_count, void *vertices) {
	uint8_t *tex_fix = psp_workFrame(data, buffer);
	sceGuStart(GU_DIRECT, gulist);
	sceGuDrawBufferList(GU_PSM_5551, work_frame, BUF_WIDTH);
	sceGuScissor(24, 16, 336, 240);
	sceGuTexImage(0, 512, 512, BUF_WIDTH, tex_fix);
	sceGuClutLoad(256/8, clut);

	sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, vertices_count, NULL, vertices);

	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);
}


video_driver_t video_psp = {
	"psp",
	psp_init,
	psp_free,
	psp_setMode,
	psp_setClutBaseAddr,
	psp_waitVsync,
	psp_flipScreen,
	psp_frameAddr,
	psp_workFrame,
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
};