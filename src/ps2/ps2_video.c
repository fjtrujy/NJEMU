/******************************************************************************

	video.c

	PS2 Video Control Functions

******************************************************************************/

#include "emumain.h"

#include <stdlib.h>
#include <assert.h>
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

/******************************************************************************
 * PS2 CLUT (Color Look-Up Table) Architecture
 * ============================================
 *
 * The PS2 GS uses CLUT for indexed textures (T4/T8). For T8 textures (8-bit
 * indexed), each pixel value (0-255) indexes into a 256-color palette row.
 *
 * VRAM Layout:
 * -----------
 * We allocate CLUT_BANKS_COUNT banks in VRAM, each bank containing
 * CLUT_WIDTH × CLUT_BANK_HEIGHT colors (256 × 16 = 4096 colors per bank).
 *
 *   Bank 0: [Row 0: 256 colors][Row 1: 256 colors]...[Row 15: 256 colors]
 *   Bank 1: [Row 0: 256 colors][Row 1: 256 colors]...[Row 15: 256 colors]
 *
 * Upload Strategy:
 * ---------------
 * The CLUT is uploaded once per frame via uploadClut(). This avoids
 * re-uploading during the frame when palette data hasn't changed.
 * - MVS: Uses 2 palette banks (4096 colors each), uploads to matching CLUT bank
 * - CPS1: Uses single flat palette (3072 colors), uploads to bank 0
 *
 * Addressing with TEXCLUT (CSM2 mode):
 * -----------------------------------
 * When drawing indexed textures, we use the GS TEXCLUT register to select
 * which 256-color row within the CLUT bank to use:
 *
 *   gs_texclut contains:
 *   - CBW: CLUT buffer width (CLUT_WIDTH >> 6 = 4)
 *   - COU: Column offset (always 0)
 *   - COV: Row offset (0-15) - selects which row of 256 colors to use
 *
 * For a pixel value P in a T8 texture with TEXCLUT COV=N:
 *   CLUT lookup = Bank base + (N * 256) + P
 *
 * Example - CPS1 SCROLL3 with palette index 5:
 *   - Texture pixels encoded as 0x50-0x5F (via color_table[5])
 *   - current_clut = &clut[96 << 4] = &clut[1536]
 *   - COV = 1536 / 256 = 6
 *   - Pixel 0x55 → CLUT entry 6*256 + 0x55 = 1621
 *   - This matches video_palette[(96+5)*16 + 5] = video_palette[1621]
 *
 * Target Requirements:
 * -------------------
 * - MVS/NCDZ: 2 banks × 4096 colors = 8192 total (matches video_palettebank)
 * - CPS1: 1 bank × 3072 colors (192 palettes × 16) - current config uploads
 *         4096 but only 3072 are valid, needs adjustment
 *
 *****************************************************************************/

#define CLUT_WIDTH 256
#define CLUT_HEIGHT 1
#define CLUT_BANK_HEIGHT 16
#define CLUT_BANKS_COUNT 2
#define CLUT_CBW (CLUT_WIDTH >> 6)

/* Render buffer dimensions - must accommodate all emulator source clips:
 * - MVS/NCDZ: src_clip right edge = 328 (24 + 304)
 * - CPS1:     src_clip right edge = 448 (64 + 384)
 * Using BUF_WIDTH (512) ensures all targets fit.
 */
#define RENDER_SCREEN_WIDTH BUF_WIDTH
#define RENDER_SCREEN_HEIGHT 264

typedef struct texture_layer {
	GSTEXTURE *texture;
} texture_layer_t;

typedef struct ps2_video {
	gs_rgbaq vertexColor;
	gs_rgbaq clearScreenColor;
	gs_texclut currentTexclut;
	GSGLOBAL *gsGlobal;
	bool drawExtraInfo;

	GSTEXTURE *scrbitmap;

	/* CLUT configuration from emu_clut_info */
	uint16_t *clut_base;
	uint16_t clut_entries_per_bank;
	uint8_t clut_bank_count;
	uint8_t clut_bank_height;  /* entries_per_bank / CLUT_WIDTH */

	uint8_t *texturesMem;

	texture_layer_t *tex_layers;
	uint8_t tex_layers_count;

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

static GSTEXTURE *initializeTexture(GSGLOBAL *gsGlobal, int width, int height, uint8_t bytes_per_pixel, void *mem) {
	GSTEXTURE *tex = (GSTEXTURE *)calloc(1, sizeof(GSTEXTURE));
	uint32_t psm = bytes_per_pixel == 1 ? GS_PSM_T8 : GS_PSM_CT16;
	tex->Width = width;
	tex->Height = height;
	tex->PSM = psm;
	if (bytes_per_pixel == 1) {
		tex->ClutPSM = GS_PSM_CT16;
		tex->ClutStorageMode = GS_CLUT_STORAGE_CSM2;
	}
	tex->Filter = GS_FILTER_NEAREST;
	tex->Mem = mem;
	tex->Vram = gsKit_vram_alloc(gsGlobal, gsKit_texture_size(width, height, psm), GSKIT_ALLOC_USERBUFFER);

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
	uint8_t cov = offset / CLUT_WIDTH - (bank_index * ps2->clut_bank_height);

	gs_texclut texclut = postion_to_TEXCLUT(CLUT_CBW, 0, cov);
	return texclut;
}

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
void gsKit_custom_clear(GSGLOBAL *gsGlobal, gs_rgbaq color, uint16_t width, uint16_t height)
{
	u8 PrevZState;
	u8 strips;
	u8 remain;
	u8 index;
	u32 pos;
	u8 slices = (width + 63)/ 64;
	u32 count = (slices * 2) + 1;
	u128 flat_content[count];

	PrevZState = gsGlobal->Test->ZTST;
	gsKit_set_test(gsGlobal, GS_ZTEST_OFF);

	flat_content[0] = color.rgbaq;
	for (index = 0; index < slices; index++)
	{
		flat_content[index * 2 + 1] = vertex_to_XYZ2(gsGlobal, index * 64, 0, 0).xyz2;
		flat_content[index * 2 + 2] = (u128)vertex_to_XYZ2(gsGlobal, MIN((index + 1) * 64, width) , height, 0).xyz2;
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

static void *ps2_workFrame(void *data)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;
	return (void *)ps2->scrbitmap->Vram;
}

static void *ps2_textureLayer(void *data, uint8_t layerIndex)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;
	return ps2->tex_layers[layerIndex].texture->Mem;
}

static void ps2_flipScreen(void *data, bool vsync);

static void *ps2_init(layer_texture_info_t *layer_textures, uint8_t layer_textures_count, clut_info_t *clut_info)
{
	ee_sema_t sema;
	ps2_video_t *ps2 = (ps2_video_t*)calloc(1, sizeof(ps2_video_t));
	GSGLOBAL *gsGlobal = gsKit_init_global();

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
	size_t totalTextureSize = 0;
	for (int i = 0; i < layer_textures_count; i++) {
		totalTextureSize += layer_textures[i].width * layer_textures[i].height;
	}
	uint8_t *textures = (uint8_t*)malloc(totalTextureSize);
	ps2->texturesMem = textures;

	// Initialize textures
	ps2->tex_layers = (texture_layer_t *)calloc(layer_textures_count, sizeof(texture_layer_t));
	ps2->tex_layers_count = layer_textures_count;

	ps2->scrbitmap = initializeRenderTexture(gsGlobal, RENDER_SCREEN_WIDTH, RENDER_SCREEN_HEIGHT);
	size_t texOffset = 0;
	for (int i = 0; i < layer_textures_count; i++) {
		ps2->tex_layers[i].texture = initializeTexture(gsGlobal, layer_textures[i].width, layer_textures[i].height, layer_textures[i].bytes_per_pixel, textures + texOffset);
		texOffset += layer_textures[i].width * layer_textures[i].height;
	}

	/* Store CLUT configuration from target.
	 * Bank height is entries_per_bank / CLUT_WIDTH, rounded up to ensure full coverage. */
	ps2->clut_base = clut_info->base;
	ps2->clut_entries_per_bank = clut_info->entries_per_bank;
	ps2->clut_bank_count = clut_info->bank_count;
	ps2->clut_bank_height = (clut_info->entries_per_bank + CLUT_WIDTH - 1) / CLUT_WIDTH;

	/* Allocate CLUT VRAM based on target's requirements */
	uint32_t clut_vram_size = gsKit_texture_size(CLUT_WIDTH, CLUT_HEIGHT * ps2->clut_bank_height, GS_PSM_CT16);
	uint32_t all_clut_vram_size = clut_vram_size * ps2->clut_bank_count;
	void *vram_cluts = (void *)gsKit_vram_alloc(gsGlobal, all_clut_vram_size, GSKIT_ALLOC_USERBUFFER);
	printf("CLUT VRAM: %p (banks=%d, entries/bank=%d, height=%d, size/bank=%u)\n",
		   vram_cluts, ps2->clut_bank_count, ps2->clut_entries_per_bank, ps2->clut_bank_height, clut_vram_size);
	ps2->clut_vram_size = clut_vram_size;
	ps2->vram_cluts = vram_cluts;

	ps2->vertexColor = color_to_RGBAQ(0x80, 0x80, 0x80, 0x80, 0);
	ps2->clearScreenColor = color_to_RGBAQ(0x00, 0x00, 0x00, 0x0, 0);

	ui_init();

	ps2->finish_callback_id = gsKit_add_finish_handler(finish_handler);

	video_driver->clearFrame(ps2, COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER);
	video_driver->clearFrame(ps2, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER);
	video_driver->clearFrame(ps2, COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP);
	ps2_flipScreen(ps2, true);

	ps2->drawExtraInfo = false;

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

	free(ps2->texturesMem);
	ps2->texturesMem = NULL;
	for (int i = 0; i < ps2->tex_layers_count; i++) {
		ps2->tex_layers[i].texture->Mem = NULL;
		free(ps2->tex_layers[i].texture);
		ps2->tex_layers[i].texture = NULL;
	}
	free(ps2->tex_layers);
	ps2->tex_layers = NULL;
	// We don't need to free vram, it's done with gsKit_vram_clear
}

static void ps2_free(void *data)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;
	
	ps2_exit(ps2);
	free(ps2);
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

static void ps2_scissor(void *data, uint16_t left, uint16_t top, uint16_t right, uint16_t bottom)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;
	gsKit_set_scissor(ps2->gsGlobal, GS_SETREG_SCISSOR(left, right, top, bottom));
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

static void ps2_clearFrame(void *data, int index)
{
	ps2_video_t *ps2 = (ps2_video_t*)data;
	uint32_t buffer_width;
	uint32_t buffer_height;
	uint32_t fbp = 0;
	uint8_t psm = GS_PSM_CT16;
	switch (index) {
	case COMMON_GRAPHIC_OBJECTS_GLOBAL_CONTEXT:
		assert("Cannot clear global context");
		break;
	case COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER:
		fbp = ps2->gsGlobal->ScreenBuffer[!(ps2->gsGlobal->ActiveBuffer & 1)];
		buffer_width = ps2->gsGlobal->Width;
		buffer_height = ps2->gsGlobal->Height;
		psm = ps2->gsGlobal->PSM;
		break;
	case COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER:
		fbp = ps2->gsGlobal->ScreenBuffer[ps2->gsGlobal->ActiveBuffer & 1];
		buffer_width = ps2->gsGlobal->Width;
		buffer_height = ps2->gsGlobal->Height;
		psm = ps2->gsGlobal->PSM;
	case COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP:
		fbp = ps2->scrbitmap->Vram;
		buffer_width = ps2->scrbitmap->Width;
		buffer_height = ps2->scrbitmap->Height;
		psm = ps2->scrbitmap->PSM;
		break;
	default:	
		assert("Shouldn't clear texture layers");
		break;
	}

	// gsKit_setRegFrame(ps2->gsGlobal, fbp, buffer_width, buffer_height, psm);
	// gsKit_custom_clear(ps2->gsGlobal, ps2->clearScreenColor, buffer_width, buffer_height);
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

	/* Reset TEXCLUT state so first blit call triggers gsKit_set_texclut.
	   Without this, if the first blit has COV=0, the comparison would fail
	   (0 != 0 is false) and TEXCLUT register would never be set with correct CBW. */
	ps2->currentTexclut.specification.cov = 0xFF;

	gsKit_set_texfilter(ps2->gsGlobal, ps2->scrbitmap->Filter);
	gsKit_renderToTexture(ps2->gsGlobal, ps2->scrbitmap);
	gsKit_custom_clear(ps2->gsGlobal, ps2_color, ps2->scrbitmap->Width, ps2->scrbitmap->Height);
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
	GSTEXTURE *tex = ps2->tex_layers[0].texture;

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
	case COMMON_GRAPHIC_OBJECTS_GLOBAL_CONTEXT:
		return ps2->gsGlobal;
	case COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER:
		return (void *)ps2->gsGlobal->ScreenBuffer[!(ps2->gsGlobal->ActiveBuffer & 1)];
	case COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER:
		return (void *)ps2->gsGlobal->ScreenBuffer[ps2->gsGlobal->ActiveBuffer & 1];
	case COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP:
		return ps2->scrbitmap;
	default:
		return ps2->tex_layers[index - COMMON_GRAPHIC_OBJECTS_INITIAL_TEXTURE_LAYER].texture;
	}
}

static void ps2_uploadMem(void *data, uint8_t textureIndex) {
	ps2_video_t *ps2 = (ps2_video_t*)data;
	GSTEXTURE *tex = ps2->tex_layers[textureIndex].texture;
   	gsKit_texture_send_inline(ps2->gsGlobal, tex->Mem, tex->Width, tex->Height, tex->Vram, tex->PSM, tex->TBW, GS_CLUT_TEXTURE);
}

static void ps2_uploadClut(void *data, uint16_t *clut, uint8_t bank_index) {
	ps2_video_t *ps2 = (ps2_video_t*)data;
	void *vram = ps2_vramClutForBankIndex(data, bank_index);
	/* Upload CLUT using target-specific dimensions */
   	gsKit_texture_send_inline(ps2->gsGlobal, (u32 *)clut, CLUT_WIDTH, CLUT_HEIGHT * ps2->clut_bank_height, (u32)vram, GS_PSM_CT16, 1, GS_CLUT_PALLETE);
}

static void ps2_blitTexture(void *data, uint8_t textureIndex, void *clut, uint8_t bank_index, uint32_t vertices_count, void *vertices) {
	ps2_video_t *ps2 = (ps2_video_t*)data;
	texture_layer_t *layer = &ps2->tex_layers[textureIndex];
	GSTEXTURE *tex = layer->texture;
	bool is_indexed = (tex->PSM == GS_PSM_T8);

	/* Only set CLUT for indexed (8-bit) textures */
	if (is_indexed && clut != NULL) {
		tex->VramClut = (u32)ps2_vramClutForBankIndex(data, bank_index);
		gs_texclut texclut = ps2_textclutForParameters(data, clut, bank_index);

		if (ps2->currentTexclut.specification.cov != texclut.specification.cov) {
			ps2->currentTexclut = texclut;
			gsKit_set_texclut(ps2->gsGlobal, texclut);
		}
	} else {
		/* For non-indexed textures, clear CLUT usage */
		tex->VramClut = 0;
	}

	gskit_prim_list_sprite_texture_uv_flat_color2(ps2->gsGlobal, tex, ps2->vertexColor, vertices_count, vertices);
}

static void ps2_blitPoints(void *data, uint32_t points_count, void *vertices) {
	// TODO: Implement PS2 point drawing for stars
}

static void ps2_flushCache(void *data, void *addr, size_t size) {
	// No cache to flush on PS2
}

video_driver_t video_ps2 = {
	"ps2",
	ps2_init,
	ps2_free,
	ps2_waitVsync,
	ps2_flipScreen,
	ps2_frameAddr,
	ps2_workFrame,
	ps2_textureLayer,
	ps2_scissor,
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
	ps2_blitPoints,
	ps2_flushCache
};