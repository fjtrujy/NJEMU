/******************************************************************************

	video.c

	PS2ビデオ制御関数

******************************************************************************/

#include "emumain.h"

#include <kernel.h>
#include <malloc.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <gsToolkit.h>

#include <gsInline.h>

/******************************************************************************
	ローカル変数/構造体
******************************************************************************/

// static const ScePs2IMatrix4 dither_matrix =
// {
// 	// Bayer dither
// 	{  0,  8,  2, 10 },
// 	{ 12,  4, 14,  6 },
// 	{  3, 11,  1,  9 },
// 	{ 15,  7, 13,  5 }
// };


/******************************************************************************
	グローバル関数
******************************************************************************/

/* turn black GS Screen */
#define GS_BLACK GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80)
/* Size of Persistent drawbuffer (Single Buffered) */
#define RENDER_QUEUE_PER_POOLSIZE 1024 * 256 // 256K of persistent renderqueue
/* Size of Oneshot drawbuffer (Double Buffered, so it uses this size * 2) */
#define RENDER_QUEUE_OS_POOLSIZE 1024 * 1024 * 2 // 2048K of oneshot renderqueue

typedef struct ps2_video {
	GSGLOBAL *gsGlobal;
    uint64_t drawColor;
    int32_t vsync_callback_id;
    uint8_t vsync; /* 0 (Disabled), 1 (Enabled), 2 (Dynamic) */
	uint8_t pixel_format;
} ps2_video_t;

static int vsync_sema_id = 0;

/* PRIVATE METHODS */
static int vsync_handler()
{
   iSignalSema(vsync_sema_id);

   ExitHandler();
   return 0;
}

/* Copy of gsKit_sync_flip, but without the 'flip' */
static void gsKit_sync(GSGLOBAL *gsGlobal)
{
   if (!gsGlobal->FirstFrame) WaitSema(vsync_sema_id);
   while (PollSema(vsync_sema_id) >= 0)
   	;
}

/* Copy of gsKit_sync_flip, but without the 'sync' */
static void gsKit_flip(GSGLOBAL *gsGlobal)
{
   if (!gsGlobal->FirstFrame)
   {
      if (gsGlobal->DoubleBuffering == GS_SETTING_ON)
      {
         GS_SET_DISPFB2( gsGlobal->ScreenBuffer[
               gsGlobal->ActiveBuffer & 1] / 8192,
               gsGlobal->Width / 64, gsGlobal->PSM, 0, 0 );

         gsGlobal->ActiveBuffer ^= 1;
      }

   }

   gsKit_setactive(gsGlobal);
}

/*--------------------------------------------------------
	ビデオ処理初期化
--------------------------------------------------------*/
static void ps2_start(void *data) {
	ps2_video_t *ps2 = (ps2_video_t*)data;

	GSGLOBAL *gsGlobal;
	
	gsGlobal = gsKit_init_global();

	gsGlobal->Mode = GS_MODE_NTSC;
    gsGlobal->Height = 448;

	gsGlobal->PSM  = GS_PSM_CT24;
	gsGlobal->PSMZ = GS_PSMZ_16S;
	gsGlobal->ZBuffering = GS_SETTING_OFF;
	gsGlobal->DoubleBuffering = GS_SETTING_ON;
	gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
	gsGlobal->Dithering = GS_SETTING_OFF;

	gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

	dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
	dmaKit_chan_init(DMA_CHANNEL_GIF);

	gsKit_set_clamp(gsGlobal, GS_CMODE_REPEAT);

	gsKit_vram_clear(gsGlobal);

	gsKit_init_screen(gsGlobal);

	gsKit_TexManager_init(gsGlobal);

	ps2->vsync_callback_id = gsKit_add_vsync_handler(vsync_handler);

	gsKit_mode_switch(gsGlobal, GS_ONESHOT);

    gsKit_clear(gsGlobal, GS_BLACK);
	ps2->gsGlobal = gsGlobal;

	#if VIDEO_32BPP
	if (video_mode == 32)
	{
		ps2->pixel_format = GS_PSM_CT32;

// 		show_frame = (void *)(FRAMESIZE32 * 0);
// 		draw_frame = (void *)(FRAMESIZE32 * 1);
// 		work_frame = (void *)(FRAMESIZE32 * 2);
// 		tex_frame  = (void *)(FRAMESIZE32 * 3);
	}
	else
#endif
	{
		ps2->pixel_format = GS_PSM_CT16;

// 		show_frame = (void *)(FRAMESIZE * 0);
// 		draw_frame = (void *)(FRAMESIZE * 1);
// 		work_frame = (void *)(FRAMESIZE * 2);
// 		tex_frame  = (void *)(FRAMESIZE * 3);
	}

// 	sceGuDisplay(GU_FALSE);
// 	sceGuInit();

// 	sceGuStart(GU_DIRECT, gulist);
// 	sceGuDrawBuffer(pixel_format, draw_frame, BUF_WIDTH);
// 	sceGuDispBuffer(SCR_WIDTH, SCR_HEIGHT, show_frame, BUF_WIDTH);
// 	sceGuOffset(2048 - (SCR_WIDTH / 2), 2048 - (SCR_HEIGHT / 2));
// 	sceGuViewport(2048, 2048, SCR_WIDTH, SCR_HEIGHT);

// 	sceGuEnable(GU_SCISSOR_TEST);
// 	sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);

// 	sceGuDisable(GU_ALPHA_TEST);
// 	sceGuAlphaFunc(GU_LEQUAL, 0, 0x01);

// 	sceGuDisable(GU_BLEND);
// 	sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);

// 	sceGuDisable(GU_DEPTH_TEST);
// 	sceGuDepthRange(65535, 0);
// 	sceGuDepthFunc(GU_GEQUAL);
// 	sceGuDepthMask(GU_TRUE);

// 	sceGuEnable(GU_TEXTURE_2D);
// 	sceGuTexMode(pixel_format, 0, 0, GU_FALSE);
// 	sceGuTexScale(1.0f / BUF_WIDTH, 1.0f / BUF_WIDTH);
// 	sceGuTexOffset(0, 0);
// 	sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);

// 	sceGuClutMode(GU_PSM_5551, 0, 0xff, 0);

// 	sceGuSetDither(&dither_matrix);
// 	sceGuDisable(GU_DITHER);

// 	sceGuClearDepth(0);
// 	sceGuClearColor(0);

// 	sceGuFinish();
// 	sceGuSync(0, GU_SYNC_FINISH);

// 	video_driver->clearFrame(data, show_frame);
// 	video_driver->clearFrame(data, draw_frame);
// 	video_driver->clearFrame(data, work_frame);

// 	sceDisplayWaitVblankStart();
// 	sceGuDisplay(GU_TRUE);

	ui_init();
}

static void *ps2_init(void)
{
	ps2_video_t *ps2 = (ps2_video_t*)calloc(1, sizeof(ps2_video_t));

	ps2_start(ps2);
	return ps2;
}


/*--------------------------------------------------------
	ビデオ処理終了(共通)
--------------------------------------------------------*/

static void ps2_exit(ps2_video_t *ps2) {
	gsKit_clear(ps2->gsGlobal, GS_BLACK);
	gsKit_vram_clear(ps2->gsGlobal);
	gsKit_deinit_global(ps2->gsGlobal);
	gsKit_remove_vsync_handler(ps2->vsync_callback_id);

	if (vsync_sema_id >= 0)
        DeleteSema(vsync_sema_id);
}

static void ps2_free(void *data)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;
	
	ps2_exit(ps2);
	free(ps2);
}

/*--------------------------------------------------------
	ビデオモード設定
--------------------------------------------------------*/


static void ps2_setMode(void *data, int mode)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;
#if VIDEO_32BPP
	if (video_mode != mode)
	{
		if (video_mode) ps2_exit(ps2);

		video_mode = mode;

		ps2_start(data);
	}
#endif
}

/*--------------------------------------------------------
	VSYNCを待つ
--------------------------------------------------------*/

static void ps2_waitVsync(void *data)
{
	gsKit_vsync_wait();
}


/*--------------------------------------------------------
	スクリーンをフリップ
--------------------------------------------------------*/

static void ps2_flipScreen(void *data, bool vsync)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;
	gsKit_queue_exec(ps2->gsGlobal);
	gsKit_finish();

	if (vsync) gsKit_vsync_wait();

	gsKit_flip(ps2->gsGlobal);
	gsKit_TexManager_nextFrame(ps2->gsGlobal);
    gsKit_clear(ps2->gsGlobal, GS_BLACK);
}


/*--------------------------------------------------------
	VRAMのアドレスを取得
--------------------------------------------------------*/

static void *ps2_frameAddr(void *data, void *frame, int x, int y)
{
// #if VIDEO_32BPP
// 	if (video_mode == 32)
// 		return (void *)(((uint32_t)frame | 0x44000000) + ((x + (y << 9)) << 2));
// 	else
// #endif
// 		return (void *)(((uint32_t)frame | 0x44000000) + ((x + (y << 9)) << 1));
}

static void *ps2_workFrame(void *data)
{
	return NULL;
}


/*--------------------------------------------------------
	描画/表示フレームをクリア
--------------------------------------------------------*/

static void ps2_clearScreen(void *data)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;
	gsKit_clear(ps2->gsGlobal, GS_BLACK);
}

/*--------------------------------------------------------
	指定したフレームをクリア
--------------------------------------------------------*/

static void ps2_clearFrame(void *data, void *frame)
{
	// sceGuStart(GU_DIRECT, gulist);
	// sceGuDrawBufferList(pixel_format, frame, BUF_WIDTH);
	// sceGuScissor(0, 0, BUF_WIDTH, SCR_HEIGHT);
	// sceGuClearColor(0);
	// sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);
	// sceGuFinish();
	// sceGuSync(0, GU_SYNC_FINISH);
}


/*--------------------------------------------------------
	指定したフレームを塗りつぶし
--------------------------------------------------------*/

static void ps2_fillFrame(void *data, void *frame, uint32_t color)
{
	// TODO: FJTRUJY so far just used by the menu

	// sceGuStart(GU_DIRECT, gulist);
	// sceGuDrawBufferList(pixel_format, frame, BUF_WIDTH);
	// sceGuScissor(0, 0, SCR_WIDTH, SCR_HEIGHT);
	// sceGuClearColor(color);
	// sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT);
	// sceGuFinish();
	// sceGuSync(0, GU_SYNC_FINISH);
}


/*--------------------------------------------------------
	矩形範囲をコピー
--------------------------------------------------------*/

static void ps2_copyRect(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
	// int j, sw, dw, sh, dh;
	// struct Vertex *vertices;

	// sw = src_rect->right - src_rect->left;
	// dw = dst_rect->right - dst_rect->left;
	// sh = src_rect->bottom - src_rect->top;
	// dh = dst_rect->bottom - dst_rect->top;

	// sceGuStart(GU_DIRECT, gulist);

	// sceGuDrawBufferList(pixel_format, dst, BUF_WIDTH);
	// sceGuScissor(dst_rect->left, dst_rect->top, dst_rect->right, dst_rect->bottom);
	// sceGuDisable(GU_ALPHA_TEST);

	// sceGuTexMode(pixel_format, 0, 0, GU_FALSE);
	// sceGuTexImage(0, BUF_WIDTH, BUF_WIDTH, BUF_WIDTH, GU_FRAME_ADDR(src));
	// if (sw == dw && sh == dh)
	// 	sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	// else
	// 	sceGuTexFilter(GU_LINEAR, GU_LINEAR);

	// for (j = 0; (j + SLICE_SIZE) < sw; j = j + SLICE_SIZE)
	// {
	// 	vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

	// 	vertices[0].u = src_rect->left + j;
	// 	vertices[0].v = src_rect->top;
	// 	vertices[0].x = dst_rect->left + j * dw / sw;
	// 	vertices[0].y = dst_rect->top;

	// 	vertices[1].u = src_rect->left + j + SLICE_SIZE;
	// 	vertices[1].v = src_rect->bottom;
	// 	vertices[1].x = dst_rect->left + (j + SLICE_SIZE) * dw / sw;
	// 	vertices[1].y = dst_rect->bottom;

	// 	sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	// }

	// if (j < sw)
	// {
	// 	vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

	// 	vertices[0].u = src_rect->left + j;
	// 	vertices[0].v = src_rect->top;
	// 	vertices[0].x = dst_rect->left + j * dw / sw;
	// 	vertices[0].y = dst_rect->top;

	// 	vertices[1].u = src_rect->right;
	// 	vertices[1].v = src_rect->bottom;
	// 	vertices[1].x = dst_rect->right;
	// 	vertices[1].y = dst_rect->bottom;

	// 	sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	// }

	// sceGuFinish();
	// sceGuSync(0, GU_SYNC_FINISH);
}


/*--------------------------------------------------------
	矩形範囲を左右反転してコピー
--------------------------------------------------------*/

static void ps2_copyRectFlip(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
	// int16_t j, sw, dw, sh, dh;
	// struct Vertex *vertices;

	// sw = src_rect->right - src_rect->left;
	// dw = dst_rect->right - dst_rect->left;
	// sh = src_rect->bottom - src_rect->top;
	// dh = dst_rect->bottom - dst_rect->top;

	// sceGuStart(GU_DIRECT, gulist);

	// sceGuDrawBufferList(pixel_format, dst, BUF_WIDTH);
	// sceGuScissor(dst_rect->left, dst_rect->top, dst_rect->right, dst_rect->bottom);
	// sceGuDisable(GU_ALPHA_TEST);

	// sceGuTexMode(pixel_format, 0, 0, GU_FALSE);
	// sceGuTexImage(0, 512, 512, BUF_WIDTH, GU_FRAME_ADDR(src));
	// if (sw == dw && sh == dh)
	// 	sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	// else
	// 	sceGuTexFilter(GU_LINEAR, GU_LINEAR);

	// for (j = 0; (j + SLICE_SIZE) < sw; j = j + SLICE_SIZE)
	// {
    // 	vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

	// 	vertices[0].u = src_rect->left + j;
	// 	vertices[0].v = src_rect->top;
	// 	vertices[0].x = dst_rect->right - j * dw / sw;
	// 	vertices[0].y = dst_rect->bottom;

	// 	vertices[1].u = src_rect->left + j + SLICE_SIZE;
	// 	vertices[1].v = src_rect->bottom;
	// 	vertices[1].x = dst_rect->right - (j + SLICE_SIZE) * dw / sw;
	// 	vertices[1].y = dst_rect->top;

	// 	sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	// }

	// if (j < sw)
	// {
	// 	vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

	// 	vertices[0].u = src_rect->left + j;
	// 	vertices[0].v = src_rect->top;
	// 	vertices[0].x = dst_rect->right - j * dw / sw;
	// 	vertices[0].y = dst_rect->bottom;

	// 	vertices[1].u = src_rect->right;
	// 	vertices[1].v = src_rect->bottom;
	// 	vertices[1].x = dst_rect->left;
	// 	vertices[1].y = dst_rect->top;

	// 	sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	// }

	// sceGuFinish();
	// sceGuSync(0, GU_SYNC_FINISH);
}


/*--------------------------------------------------------
	矩形範囲を270度回転してコピー
--------------------------------------------------------*/

static void ps2_copyRectRotate(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
	// int16_t j, sw, dw, sh, dh;
	// struct Vertex *vertices;

	// sw = src_rect->right - src_rect->left;
	// dw = dst_rect->right - dst_rect->left;
	// sh = src_rect->bottom - src_rect->top;
	// dh = dst_rect->bottom - dst_rect->top;

	// sceGuStart(GU_DIRECT, gulist);

	// sceGuDrawBufferList(pixel_format, dst, BUF_WIDTH);
	// sceGuScissor(dst_rect->left, dst_rect->top, dst_rect->right, dst_rect->bottom);
	// sceGuDisable(GU_ALPHA_TEST);

	// sceGuTexMode(pixel_format, 0, 0, GU_FALSE);
	// sceGuTexImage(0, 512, 512, BUF_WIDTH, GU_FRAME_ADDR(src));
	// if (sw == dh && sh == dw)
	// 	sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	// else
	// 	sceGuTexFilter(GU_LINEAR, GU_LINEAR);

	// vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

	// for (j = 0; (j + SLICE_SIZE) < sw; j = j + SLICE_SIZE)
	// {
	// 	vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

	// 	vertices[0].u = src_rect->right - j;
	// 	vertices[0].v = src_rect->bottom;
	// 	vertices[0].x = dst_rect->right;
	// 	vertices[0].y = dst_rect->top - j * dh / sw;

	// 	vertices[1].u = src_rect->right - j + SLICE_SIZE;
	// 	vertices[1].v = src_rect->top;
	// 	vertices[1].x = dst_rect->right;
	// 	vertices[1].y = dst_rect->bottom - (j + SLICE_SIZE) * dh / sw;

	// 	sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	// }

	// if (j < sw)
	// {
	// 	vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

	// 	vertices[0].u = src_rect->right + j;
	// 	vertices[0].v = src_rect->bottom;
	// 	vertices[0].x = dst_rect->right;
	// 	vertices[0].y = dst_rect->top - j * dh / sw;

	// 	vertices[1].u = src_rect->left;
	// 	vertices[1].v = src_rect->top;
	// 	vertices[1].x = dst_rect->left;
	// 	vertices[1].y = dst_rect->bottom;

	// 	sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	// }

	// sceGuFinish();
	// sceGuSync(0, GU_SYNC_FINISH);
}


/*--------------------------------------------------------
	テクスチャを矩形範囲を指定して描画
--------------------------------------------------------*/

static void ps2_drawTexture(void *data, uint32_t src_fmt, uint32_t dst_fmt, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
	// int j, sw, dw, sh, dh;
	// struct Vertex *vertices;

	// sw = src_rect->right - src_rect->left;
	// dw = dst_rect->right - dst_rect->left;
	// sh = src_rect->bottom - src_rect->top;
	// dh = dst_rect->bottom - dst_rect->top;

	// sceGuStart(GU_DIRECT, gulist);
	// sceGuDrawBufferList(dst_fmt, dst, BUF_WIDTH);
	// sceGuScissor(dst_rect->left, dst_rect->top, dst_rect->right, dst_rect->bottom);

	// sceGuTexMode(src_fmt, 0, 0, GU_FALSE);
	// sceGuTexImage(0, BUF_WIDTH, BUF_WIDTH, BUF_WIDTH, GU_FRAME_ADDR(src));
	// if (sw == dw && sh == dh)
	// 	sceGuTexFilter(GU_NEAREST, GU_NEAREST);
	// else
	// 	sceGuTexFilter(GU_LINEAR, GU_LINEAR);

	// for (j = 0; (j + SLICE_SIZE) < sw; j = j + SLICE_SIZE)
	// {
    // 	vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

	// 	vertices[0].u = src_rect->left + j;
	// 	vertices[0].v = src_rect->top;
	// 	vertices[0].x = dst_rect->left + j * dw / sw;
	// 	vertices[0].y = dst_rect->top;

	// 	vertices[1].u = src_rect->left + j + SLICE_SIZE;
	// 	vertices[1].v = src_rect->bottom;
	// 	vertices[1].x = dst_rect->left + (j + SLICE_SIZE) * dw / sw;
	// 	vertices[1].y = dst_rect->bottom;

	// 	sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	// }

	// if (j < sw)
	// {
	// 	vertices = (struct Vertex *)sceGuGetMemory(2 * sizeof(struct Vertex));

	// 	vertices[0].u = src_rect->left + j;
	// 	vertices[0].v = src_rect->top;
	// 	vertices[0].x = dst_rect->left + j * dw / sw;
	// 	vertices[0].y = dst_rect->top;

	// 	vertices[1].u = src_rect->right;
	// 	vertices[1].v = src_rect->bottom;
	// 	vertices[1].x = dst_rect->right;
	// 	vertices[1].y = dst_rect->bottom;

	// 	sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, 2, NULL, vertices);
	// }

	// sceGuFinish();
	// sceGuSync(0, GU_SYNC_FINISH);
}


video_driver_t video_ps2 = {
	"ps2",
	ps2_init,
	ps2_free,
	ps2_setMode,
	ps2_waitVsync,
	ps2_flipScreen,
	ps2_frameAddr,
	ps2_workFrame,
	ps2_clearScreen,
	ps2_clearFrame,
	ps2_fillFrame,
	ps2_copyRect,
	ps2_copyRectFlip,
	ps2_copyRectRotate,
	ps2_drawTexture,
};