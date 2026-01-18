/******************************************************************************

	video.c

	PS2 Video Control Functions

******************************************************************************/

#include "emumain.h"

#include <stdlib.h>
#include <kernel.h>
#include <malloc.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <gsToolkit.h>

#include <gsInline.h>
#include <gsCore.h>


/******************************************************************************
	Global Functions
******************************************************************************/

/* turn black GS Screen */
#define GS_BLACK GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80)

#define CLUT_WIDTH 256
#define CLUT_HEIGHT 1
#define CLUT_BANK_HEIGHT 16
#define CLUT_BANKS_COUNT 2
#define CLUT_CBW (CLUT_WIDTH >> 6)

/* 6000000 Hz pixel clock / 384 H / 264 V = 59.1856060~ Hz */
/* Render width must be 64 pixel multiple,  */
#define RENDER_SCREEN_WIDTH 384
#define RENDER_SCREEN_HEIGHT 264

typedef struct ps2_video {
	gs_rgbaq vertexColor;
	gs_rgbaq clearScreenColor;
	gs_texclut currentTexclut;
	GSGLOBAL *gsGlobal;
	bool drawExtraInfo;

	// Base clut starting address
	uint16_t *clut_base;

	// Original buffers containing clut indexes
	uint8_t *spr;
	uint8_t *spr0;
	uint8_t *spr1;
	uint8_t *spr2;
	uint8_t *fix;


	GSTEXTURE *scrbitmap;
	GSTEXTURE *tex_spr0;
	GSTEXTURE *tex_spr1;
	GSTEXTURE *tex_spr2;
	GSTEXTURE *tex_fix;
	uint32_t offset;
	uint8_t vsync; /* 0 (Disabled), 1 (Enabled), 2 (Dynamic) */
	uint8_t pixel_format;

	void *vram_cluts;
	uint32_t clut_vram_size;
	uint32_t finish_callback_id;
} ps2_video_t;

static uint32_t finish_sema_id = 0;

/*--------------------------------------------------------
	Video Processing Initialization
--------------------------------------------------------*/

static int finish_handler(int reason)
{
	if (GS_CSR_FINISH) {
		iSignalSema(finish_sema_id);
	}

   ExitHandler();
   return 0;
}

static GSTEXTURE *initializeTexture(GSGLOBAL *gsGlobal, int width, int height, void *mem) {
	GSTEXTURE *tex = (GSTEXTURE *)calloc(1, sizeof(GSTEXTURE));
	tex->Width = width;
	tex->Height = height;
	tex->PSM = GS_PSM_T8;
	tex->ClutPSM = GS_PSM_CT16;
	tex->Filter = GS_FILTER_NEAREST;
	tex->Mem = mem;
	tex->Vram = gsKit_vram_alloc(gsGlobal, gsKit_texture_size(width, height, GS_PSM_T8), GSKIT_ALLOC_USERBUFFER);
	tex->ClutStorageMode = GS_CLUT_STORAGE_CSM2;

	gsKit_setup_tbw(tex);
	return tex;
}

static GSTEXTURE *initializeRenderTexture(GSGLOBAL *gsGlobal, int width, int height) {
	GSTEXTURE *tex = (GSTEXTURE *)calloc(1, sizeof(GSTEXTURE));
	tex->Width = width;
	tex->Height = height;
	tex->PSM = GS_PSM_CT16;
	tex->Filter = GS_FILTER_NEAREST;
	tex->Mem = 0;
	tex->Vram = gsKit_vram_alloc(gsGlobal, gsKit_texture_size(width, height, GS_PSM_CT16), GSKIT_ALLOC_USERBUFFER);

	gsKit_setup_tbw(tex);
	return tex;
}

static inline void *ps2_vramClutForBankIndex(void *data, uint8_t bank_index) {
	ps2_video_t *ps2 = (ps2_video_t*)data;
	return (void *)((uint8_t *)ps2->vram_cluts + bank_index * ps2->clut_vram_size);
}

static inline gs_texclut ps2_textclutForParameters(void *data, uint16_t *current_clut, uint8_t bank_index) {
	ps2_video_t *ps2 = (ps2_video_t*)data;
	ptrdiff_t offset = current_clut - ps2->clut_base;
	uint8_t cov = offset / CLUT_WIDTH - (bank_index * CLUT_BANK_HEIGHT);

	gs_texclut texclut = postion_to_TEXCLUT(CLUT_CBW, 0, cov);
	return texclut;
}

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
void gsKit_clearRenderTexture(GSGLOBAL *gsGlobal, gs_rgbaq color, GSTEXTURE *texture)
{
	u8 PrevZState;
	u8 strips;
	u8 remain;
	u8 index;
	u32 pos;
	u8 slices = (texture->Width + 63)/ 64;
	u32 count = (slices * 2) + 1;
	u128 flat_content[count];

	PrevZState = gsGlobal->Test->ZTST;
	gsKit_set_test(gsGlobal, GS_ZTEST_OFF);

	flat_content[0] = color.rgbaq;
	for (index = 0; index < slices; index++)
	{
		flat_content[index * 2 + 1] = vertex_to_XYZ2(gsGlobal, index * 64, 0, 0).xyz2;
		flat_content[index * 2 + 2] = (u128)vertex_to_XYZ2(gsGlobal, MIN((index + 1) * 64, texture->Width) , texture->Height, 0).xyz2;
	}
	gsKit_prim_list_sprite_flat(gsGlobal, count, flat_content);

	gsGlobal->Test->ZTST = PrevZState;
	gsKit_set_test(gsGlobal, 0);
}

static inline void gsKit_setRegFrame(GSGLOBAL *gsGlobal, uint32_t fbp, uint32_t width, uint32_t height, uint8_t psm) {
	u64 *p_data;
	u64 *p_store;
	int qsize = 2;

	p_store = p_data = gsKit_heap_alloc(gsGlobal, qsize, (qsize*16), GIF_AD);

	if(p_store == gsGlobal->CurQueue->last_tag)
	{
		*p_data++ = GIF_TAG_AD(qsize);
		*p_data++ = GIF_AD;
	}

	*p_data++ = GS_SETREG_FRAME_1(fbp / 8192, width / 64, psm, 0);
	*p_data++ = GS_FRAME_1;

	*p_data++ = GS_SETREG_SCISSOR_1(0, width - 1, 0, height - 1);
	*p_data++ = GS_SCISSOR_1;
}

static inline void gsKit_renderToScreen(GSGLOBAL *gsGlobal)
{
	gsKit_setRegFrame(gsGlobal, gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer & 1], gsGlobal->Width, gsGlobal->Height, gsGlobal->PSM);
}

static inline void gsKit_renderToTexture(GSGLOBAL *gsGlobal, GSTEXTURE *texture)
{
	gsKit_setRegFrame(gsGlobal, texture->Vram, texture->Width, texture->Height, texture->PSM);
}

void gsKit_setactive_queue(GSGLOBAL *gsGlobal)
{
	u64 *p_data;
	u64 *p_store;

	p_data = p_store = (u64 *)gsGlobal->dma_misc;

	*p_data++ = GIF_TAG( 4, 1, 0, 0, GSKIT_GIF_FLG_PACKED, 1 );
	*p_data++ = GIF_AD;

	// Context 1

	*p_data++ = GS_SETREG_SCISSOR_1( 0, gsGlobal->Width - 1, 0, gsGlobal->Height - 1 );
	*p_data++ = GS_SCISSOR_1;

	*p_data++ = GS_SETREG_FRAME_1( gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer & 1] / 8192,
                                 gsGlobal->Width / 64, gsGlobal->PSM, 0 );
	*p_data++ = GS_FRAME_1;

	// Context 2

	*p_data++ = GS_SETREG_SCISSOR_1( 0, gsGlobal->Width - 1, 0, gsGlobal->Height - 1 );
	*p_data++ = GS_SCISSOR_2;

	*p_data++ = GS_SETREG_FRAME_1( gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer & 1] / 8192,
                                 gsGlobal->Width / 64, gsGlobal->PSM, 0 );
	*p_data++ = GS_FRAME_2;
}


/* Copy of gsKit_sync_flip, but without the 'sync' */
static inline void gsKit_flip(GSGLOBAL *gsGlobal)
{
   if (!gsGlobal->FirstFrame)
   {
      if (gsGlobal->DoubleBuffering == GS_SETTING_ON)
      {
         GS_SET_DISPFB2(gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer & 1] / 8192,
                        gsGlobal->Width / 64, gsGlobal->PSM, 0, 0);

         gsGlobal->ActiveBuffer ^= 1;
      }
   }

   gsKit_setactive_queue(gsGlobal);
}

static inline u32 lzw(u32 val)
{
	u32 res;
	__asm__ __volatile__ ("   plzcw   %0, %1    " : "=r" (res) : "r" (val));
	return(res);
}

static inline void gsKit_wait_finish(GSGLOBAL *gsGlobal)
{
	if (!GS_CSR_FINISH)
    	WaitSema(finish_sema_id);

   	while (PollSema(finish_sema_id) >= 0);
}

static inline void gsKit_set_tw_th(const GSTEXTURE *Texture, int *tw, int *th)
{
	*tw = 31 - (lzw(Texture->Width) + 1);
	if(Texture->Width > (1<<*tw))
		(*tw)++;

	*th = 31 - (lzw(Texture->Height) + 1);
	if(Texture->Height > (1<<*th))
		(*th)++;
}

static inline void gskit_prim_list_sprite_texture_uv_flat_color2(GSGLOBAL *gsGlobal, const GSTEXTURE *Texture, gs_rgbaq color, int count, const GSPRIMUVPOINTFLAT *vertices)
{
	u64* p_data;
	u64* p_store;
	int tw, th;

	int qsize = (count * 2) + 3;
	int bytes = count * sizeof(GSPRIMUVPOINTFLAT);

	gsKit_set_tw_th(Texture, &tw, &th);

	p_store = p_data = gsKit_heap_alloc(gsGlobal, qsize, (qsize*16), GIF_AD);

	if(p_store == gsGlobal->CurQueue->last_tag)
	{
		*p_data++ = GIF_TAG_AD(qsize);
		*p_data++ = GIF_AD;
	}

	if(Texture->VramClut == 0)
	{
		*p_data++ = GS_SETREG_TEX0(Texture->Vram/256, Texture->TBW, Texture->PSM,
			tw, th, gsGlobal->PrimAlphaEnable, 0,
			0, 0, 0, 0, GS_CLUT_STOREMODE_NOLOAD);
	}
	else
	{
		*p_data++ = GS_SETREG_TEX0(Texture->Vram/256, Texture->TBW, Texture->PSM,
			tw, th, gsGlobal->PrimAlphaEnable, 0,
			Texture->VramClut/256, Texture->ClutPSM, Texture->ClutStorageMode, 0, GS_CLUT_STOREMODE_LOAD);
	}
	*p_data++ = GS_TEX0_1 + gsGlobal->PrimContext;

	*p_data++ = GS_SETREG_PRIM( GS_PRIM_PRIM_SPRITE, 0, 1, gsGlobal->PrimFogEnable,
				gsGlobal->PrimAlphaEnable, gsGlobal->PrimAAEnable,
				1, gsGlobal->PrimContext, 0);

	*p_data++ = GS_PRIM;

	// Copy color
	memcpy(p_data, &color, sizeof(gs_rgbaq));
	p_data += 2; // Advance 2 u64, which is 16 bytes the gs_rgbaq struct size
	// Copy vertices
	memcpy(p_data, vertices, bytes);
}

static void *ps2_workFrame(void *data, enum WorkBuffer buffer)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;
	switch (buffer)
	{
		case SCRBITMAP:
			return(void *)ps2->scrbitmap->Vram;
		case TEX_SPR0:
			return ps2->spr0;
		case TEX_SPR1:
			return ps2->spr1;
		case TEX_SPR2:
			return ps2->spr2;
		case TEX_FIX:
			return ps2->fix;
	}

	return NULL;
}

static void ps2_flipScreen(void *data, bool vsync);

static void *ps2_init(void)
{
	ee_sema_t sema;

	ps2_video_t *ps2 = (ps2_video_t*)calloc(1, sizeof(ps2_video_t));
	GSGLOBAL *gsGlobal = gsKit_init_global();
	
	ps2->drawExtraInfo = false;

   	sema.init_count = 0;
   	sema.max_count  = 1;
   	sema.option     = 0;

   	finish_sema_id   = CreateSema(&sema);

	gsGlobal->Mode = GS_MODE_NTSC;
    gsGlobal->Height = 448;

	gsGlobal->PSM  = GS_PSM_CT16;
	gsGlobal->PSMZ = GS_PSMZ_16S;
	gsGlobal->ZBuffering = GS_SETTING_OFF;
	gsGlobal->DoubleBuffering = GS_SETTING_ON;
	gsGlobal->Dithering = GS_SETTING_OFF;
	gsGlobal->PrimAlphaEnable = GS_SETTING_ON;

	gsKit_set_test(gsGlobal, GS_ATEST_ON);

	// Do not draw pixels if they are fully transparent
	gsGlobal->Test->ATE  = GS_SETTING_ON;
	gsGlobal->Test->ATST = 4; // TEQUAL to AREF passes
	gsGlobal->Test->AREF = 0x00;

	dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
	dmaKit_chan_init(DMA_CHANNEL_GIF);


	gsKit_vram_clear(gsGlobal);

	gsKit_init_screen(gsGlobal);

	gsKit_mode_switch(gsGlobal, GS_ONESHOT);
    gsKit_clear(gsGlobal, GS_BLACK);
	ps2->gsGlobal = gsGlobal;

	// Original buffers containing clut indexes
	size_t textureSize = BUF_WIDTH * TEXTURE_HEIGHT;
	uint8_t *spr = (uint8_t*)malloc(textureSize * 3);
	ps2->spr = spr;
	ps2->spr0 = spr;
	ps2->spr1 = spr + textureSize;
	ps2->spr2 = spr + textureSize * 2;
	ps2->fix = (uint8_t*)malloc(textureSize);

	// Initialize textures
	ps2->scrbitmap = initializeRenderTexture(gsGlobal, RENDER_SCREEN_WIDTH, RENDER_SCREEN_HEIGHT);
	ps2->tex_spr0 = initializeTexture(gsGlobal, BUF_WIDTH, TEXTURE_HEIGHT, ps2->spr0);
	ps2->tex_spr1 = initializeTexture(gsGlobal, BUF_WIDTH, TEXTURE_HEIGHT, ps2->spr1);
	ps2->tex_spr2 = initializeTexture(gsGlobal, BUF_WIDTH, TEXTURE_HEIGHT, ps2->spr2);
	ps2->tex_fix = initializeTexture(gsGlobal, BUF_WIDTH, TEXTURE_HEIGHT, ps2->fix);

	// Initialize VRAM directly
	printf("BUF_WIDTH %i, SCR_HEIGHT %i, gsKit_texture_size %i\n", BUF_WIDTH, SCR_HEIGHT, gsKit_texture_size(BUF_WIDTH, SCR_HEIGHT, GS_PSM_T8));
	printf("BUF_WIDTH %i, TEXTURE_HEIGHT %i, gsKit_texture_size %i\n", BUF_WIDTH, TEXTURE_HEIGHT, gsKit_texture_size(BUF_WIDTH, TEXTURE_HEIGHT, GS_PSM_T8));
	printf("width %i, height %i, gsKit_texture_size %i\n", CLUT_WIDTH, CLUT_HEIGHT * CLUT_BANK_HEIGHT * CLUT_BANKS_COUNT, gsKit_texture_size(CLUT_WIDTH, CLUT_HEIGHT * CLUT_BANK_HEIGHT * CLUT_BANKS_COUNT, GS_PSM_CT16));

	uint32_t clut_vram_size = gsKit_texture_size(CLUT_WIDTH, CLUT_HEIGHT * CLUT_BANK_HEIGHT, GS_PSM_CT16);
	uint32_t all_clut_vram_size = clut_vram_size * CLUT_BANKS_COUNT;
	void *vram_cluts = (void *)gsKit_vram_alloc(gsGlobal, all_clut_vram_size, GSKIT_ALLOC_USERBUFFER);
	printf("vram_cluts %p\n", vram_cluts);
	ps2->clut_vram_size = clut_vram_size;
	ps2->vram_cluts = vram_cluts;

	ps2->vertexColor = color_to_RGBAQ(0x80, 0x80, 0x80, 0x80, 0);
	ps2->clearScreenColor = color_to_RGBAQ(0x00, 0x00, 0x00, 0x0, 0);

	ui_init();

	ps2->finish_callback_id = gsKit_add_finish_handler(finish_handler);
	ps2_flipScreen(ps2, true);

	return ps2;
}


/*--------------------------------------------------------
	Video Processing Termination (Common)
--------------------------------------------------------*/

static void ps2_exit(ps2_video_t *ps2) {
	gsKit_clear(ps2->gsGlobal, GS_BLACK);
	gsKit_vram_clear(ps2->gsGlobal);
	gsKit_deinit_global(ps2->gsGlobal);
	gsKit_remove_finish_handler(ps2->finish_callback_id);
	if (finish_sema_id >= 0)
    	DeleteSema(finish_sema_id);
	
	free(ps2->scrbitmap);
	ps2->scrbitmap = NULL;
	free(ps2->tex_spr0);
	ps2->tex_spr0 = NULL;
	free(ps2->tex_spr1);
	ps2->tex_spr1 = NULL;
	free(ps2->tex_spr2);
	ps2->tex_spr2 = NULL;
	free(ps2->tex_fix);
	ps2->tex_fix = NULL;

	free(ps2->spr);
	ps2->spr = NULL;
	ps2->spr0 = NULL;
	ps2->spr1 = NULL;
	ps2->spr2 = NULL;
	free(ps2->fix);
	ps2->fix = NULL;

	// We don't need to free vram, it's done with gsKit_vram_clear
}

static void ps2_free(void *data)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;
	
	ps2_exit(ps2);
	free(ps2);
}

static void ps2_setClutBaseAddr(void *data, uint16_t *clut_base)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;
	ps2->clut_base = clut_base;
}

/*--------------------------------------------------------
	Wait for VSYNC
--------------------------------------------------------*/

static void ps2_waitVsync(void *data)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;

	gsKit_sync_flip(ps2->gsGlobal);
}


/*--------------------------------------------------------
	Flip Screen
--------------------------------------------------------*/

static void ps2_flipScreen(void *data, bool vsync)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;

	gsKit_wait_finish(ps2->gsGlobal);
	gsKit_queue_exec(ps2->gsGlobal);

	if (vsync) {
		gsKit_sync_flip(ps2->gsGlobal);
	} else {
		gsKit_flip(ps2->gsGlobal);
	}
}


/*--------------------------------------------------------
	Get VRAM Address
--------------------------------------------------------*/

static void *ps2_frameAddr(void *data, void *frame, int x, int y)
{
	// TODO: FJTRUJY so far just used by the menu
// 		return (void *)(((uint32_t)frame | 0x44000000) + ((x + (y << 9)) << 1));
	return NULL;
}


/*--------------------------------------------------------
	Clear Draw/Display Frame
--------------------------------------------------------*/

static void ps2_clearScreen(void *data) {
	ps2_video_t *ps2 = (ps2_video_t*)data;
	gsKit_clear(ps2->gsGlobal, ps2->clearScreenColor.color.rgbaq);
}

/*--------------------------------------------------------
	Clear Specified Frame
--------------------------------------------------------*/

static void ps2_clearFrame(void *data, void *frame)
{
	ps2_clearScreen(data);
}


/*--------------------------------------------------------
	Fill Specified Frame
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
	Copy Rectangular Area
--------------------------------------------------------*/

static void ps2_startWorkFrame(void *data, uint32_t color) {
	ps2_video_t *ps2 = (ps2_video_t*)data;
	uint8_t alpha = color >> 24;
	uint8_t blue = color >> 16;
	uint8_t green = color >> 8;
	uint8_t red = color >> 0;
	gs_rgbaq ps2_color = color_to_RGBAQ(red, green, blue, alpha, 0);

	gsKit_set_texfilter(ps2->gsGlobal, ps2->scrbitmap->Filter);
	gsKit_renderToTexture(ps2->gsGlobal, ps2->scrbitmap);
	gsKit_clearRenderTexture(ps2->gsGlobal, ps2_color, ps2->scrbitmap);
}

static void ps2_transferWorkFrame(void *data, RECT *src_rect, RECT *dst_rect)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;

	uint8_t textureVertexCount = 2;
	GSPRIMUVPOINTFLAT textureVertex[textureVertexCount];
	textureVertex[0].xyz2 = vertex_to_XYZ2(ps2->gsGlobal, dst_rect->left - 0.5, dst_rect->top - 0.5, 0);
	textureVertex[0].uv = vertex_to_UV(ps2->scrbitmap, src_rect->left, src_rect->top);

	textureVertex[1].xyz2 = vertex_to_XYZ2(ps2->gsGlobal, dst_rect->right - 0.5, dst_rect->bottom - 0.5, 0);
	textureVertex[1].uv = vertex_to_UV(ps2->scrbitmap, src_rect->right, src_rect->bottom);

	gsKit_renderToScreen(ps2->gsGlobal);
	gsKit_set_texfilter(ps2->gsGlobal, ps2->scrbitmap->Filter);
	gskit_prim_list_sprite_texture_uv_flat_color2(ps2->gsGlobal, ps2->scrbitmap, ps2->vertexColor, textureVertexCount, textureVertex);



	// printf("transferWorkFrame %d\n", transfer_count);
	// // sleep(1);
    // if (transfer_count == 530) {
        // SleepThread();
    // }
	if (!ps2->drawExtraInfo) return;
	gs_rgbaq color = color_to_RGBAQ(0x80, 0x80, 0x80, 0x80, 0);

	// Choose texture to print
	GSTEXTURE *tex = ps2->tex_fix;

	#define LEFT 350
	#define TOP 20
	#define RIGHT (LEFT + tex->Width / 2)
	#define BOTTOM (TOP + tex->Height / 2)
	#define BORDER_LEFT LEFT - 1
	#define BORDER_TOP TOP - 1
	#define BORDER_RIGHT RIGHT + 1
	#define BORDER_BOTTOM BOTTOM + 1

	gsKit_prim_quad(ps2->gsGlobal, 
		BORDER_LEFT, BORDER_TOP, 
		BORDER_RIGHT, BORDER_TOP, 
		BORDER_LEFT, BORDER_BOTTOM, 
		BORDER_RIGHT, BORDER_BOTTOM, 
		0, GS_SETREG_RGBA(0x80, 0, 0, 0x80));
	gsKit_prim_quad(ps2->gsGlobal, 
		LEFT, TOP, 
		RIGHT, TOP, 
		LEFT, BOTTOM, 
		RIGHT, BOTTOM, 
		0, GS_SETREG_RGBA(0, 0, 0, 0x80));

	GSPRIMUVPOINTFLAT *verts2 = (GSPRIMUVPOINTFLAT *)malloc(sizeof(GSPRIMUVPOINTFLAT) * 2);
	verts2[0].xyz2 = vertex_to_XYZ2(ps2->gsGlobal, LEFT, TOP, 0);
	verts2[0].uv = vertex_to_UV(tex, 0, 0);

	verts2[1].xyz2 = vertex_to_XYZ2(ps2->gsGlobal, RIGHT, BOTTOM, 0);
	verts2[1].uv = vertex_to_UV(tex, tex->Width, tex->Height);

	gskit_prim_list_sprite_texture_uv_flat_color(ps2->gsGlobal, tex, color, 2, verts2);

	free(verts2);
}

static void ps2_copyRect(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
	// TODO: FJTRUJY so far just used by the menu, adhoc, and state
	// It is also used in the biosmenu but let's ignore it for now

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
	Copy Rectangular Area with Horizontal Flip
--------------------------------------------------------*/

static void ps2_copyRectFlip(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
	// TODO: FJTRUJY not used so far in MVS


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
	Copy Rectangular Area with 270-degree Rotation
--------------------------------------------------------*/

static void ps2_copyRectRotate(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
	// TODO: FJTRUJY not used so far in MVS (juat in state.c, but not used in the game)
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
	Draw Texture with Specified Rectangular Area
--------------------------------------------------------*/

static void ps2_drawTexture(void *data, uint32_t src_fmt, uint32_t dst_fmt, void *src, void *dst, RECT *src_rect, RECT *dst_rect)
{
	// TODO: FJTRUJY so far just used by the menu

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

static void *ps2_getNativeObjects(void *data, int index) {
	ps2_video_t *ps2 = (ps2_video_t*)data;
	switch (index) {
	case 0:
		return ps2->gsGlobal;
	case 1: 
		return ps2->scrbitmap;
	case 2:
		return ps2->tex_spr0;
	case 3:
		return ps2->tex_spr1;
	case 4:
		return ps2->tex_spr2;
	case 5:
		return ps2->tex_fix;
	default:
		return NULL;
	}
}

static GSTEXTURE *ps2_getTexture(void *data, enum WorkBuffer buffer) {
	ps2_video_t *ps2 = (ps2_video_t*)data;
	switch (buffer)
	{
		case SCRBITMAP:
			return ps2->scrbitmap;
		case TEX_SPR0:
			return ps2->tex_spr0;
		case TEX_SPR1:
			return ps2->tex_spr1;
		case TEX_SPR2:
			return ps2->tex_spr2;
		case TEX_FIX:
			return ps2->tex_fix;
		default:
			return NULL;
	}
}

static void ps2_uploadMem(void *data, enum WorkBuffer buffer) {
	ps2_video_t *ps2 = (ps2_video_t*)data;
	GSTEXTURE *tex = ps2_getTexture(data, buffer);
   	gsKit_texture_send_inline(ps2->gsGlobal, tex->Mem, tex->Width, tex->Height, tex->Vram, tex->PSM, tex->TBW, GS_CLUT_TEXTURE);
}

static void ps2_uploadClut(void *data, uint16_t *bank, uint8_t bank_index) {
	ps2_video_t *ps2 = (ps2_video_t*)data;
	void *vram = ps2_vramClutForBankIndex(data, bank_index);
   	gsKit_texture_send_inline(ps2->gsGlobal, (u32 *)bank, CLUT_WIDTH, CLUT_HEIGHT * CLUT_BANK_HEIGHT, (u32)vram, GS_PSM_CT16, 1, GS_CLUT_PALLETE);
}

static void ps2_blitTexture(void *data, enum WorkBuffer buffer, void *clut, uint8_t bank_index, uint32_t vertices_count, void *vertices) {
	ps2_video_t *ps2 = (ps2_video_t*)data;
	GSTEXTURE *tex = ps2_getTexture(data, buffer);

	tex->VramClut = (u32)ps2_vramClutForBankIndex(data, bank_index);
	gs_texclut texclut = ps2_textclutForParameters(data, clut, bank_index);

	if (ps2->currentTexclut.specification.cov != texclut.specification.cov) {
		ps2->currentTexclut = texclut;
		gsKit_set_texclut(ps2->gsGlobal, texclut);
	}
	gskit_prim_list_sprite_texture_uv_flat_color2(ps2->gsGlobal, tex, ps2->vertexColor, vertices_count, vertices);
}


video_driver_t video_ps2 = {
	"ps2",
	ps2_init,
	ps2_free,
	ps2_setClutBaseAddr,
	ps2_waitVsync,
	ps2_flipScreen,
	ps2_frameAddr,
	ps2_workFrame,
	ps2_clearScreen,
	ps2_clearFrame,
	ps2_fillFrame,
	ps2_startWorkFrame,
	ps2_transferWorkFrame,
	ps2_copyRect,
	ps2_copyRectFlip,
	ps2_copyRectRotate,
	ps2_drawTexture,
	ps2_getNativeObjects,
	ps2_uploadMem,
	ps2_uploadClut,
	ps2_blitTexture,
};