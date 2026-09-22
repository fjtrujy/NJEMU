/******************************************************************************

	ui.c

	User Interface Processing

******************************************************************************/

#include "emumain.h"
#include "stdarg.h"
#include "common/ui_draw_driver.h"
#include "common/ui_layout.h"
#include "common/ui.h"
#include "common/ui_draw.h"


/******************************************************************************
	背景描画
******************************************************************************/

/*------------------------------------------------------
	背景画像読み込み
------------------------------------------------------*/

void load_background(int number)
{
	(void)number;

	video_driver->beginFrame(video_data);
	ui_fill_frame(UI_PAL_BG2);

#if defined(PSP)
	/* PSP always presents the original 480x272 logical canvas 1:1.  Cache the
	 * static chrome with the background as the legacy renderer did, instead of
	 * rebuilding its gradients and shadows on every list-selection redraw. */
	draw_bar_shadow();
	boxfill_alpha(0, 0, ui_layout_right(0), 23, UI_COLOR(UI_PAL_BG1), 10);
	hline_alpha(0, ui_layout_right(0), 23, UI_COLOR(UI_PAL_FRAME), 12);
	hline_alpha(0, ui_layout_right(0), 24, UI_COLOR(UI_PAL_FRAME), 10);
#endif

	video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, COMMON_GRAPHIC_OBJECTS_SCREEN_BITMAP, &full_rect, &full_rect);
	video_driver->endFrame(video_data);
}


/*------------------------------------------------------
	背景画像表示
------------------------------------------------------*/

void show_background(void)
{
	int output_width;
	int output_height;
	const ui_layout_metrics_t *layout;
	RECT viewport;
	const uint32_t black = 0xff000000;

	ui_draw_driver->getOutputSize(ui_draw_data, &output_width, &output_height);
	ui_layout_init_responsive(output_width, output_height);
	layout = ui_layout_get();
	viewport.left = layout->viewport_x;
	viewport.top = layout->viewport_y;
	viewport.right = layout->viewport_x + layout->viewport_width;
	viewport.bottom = layout->viewport_y + layout->viewport_height;

	video_driver->transferWorkFrame(video_data, &full_rect, &viewport);

	/* transferWorkFrame() leaves the physical presentation target active on all
	 * GUI backends. Native-layout platforms cover the full output; the fallback
	 * bar fill remains useful if a future backend chooses an inset viewport. */
	if (ui_layout_uses_output_transform()) {
		if (viewport.top > 0)
			ui_draw_driver->fillRect(ui_draw_data, 0, 0,
				layout->output_width, viewport.top, black);
		if (viewport.bottom < layout->output_height)
			ui_draw_driver->fillRect(ui_draw_data, 0, viewport.bottom,
				layout->output_width, layout->output_height - viewport.bottom, black);
		if (viewport.left > 0)
			ui_draw_driver->fillRect(ui_draw_data, 0, viewport.top,
				viewport.left, layout->viewport_height, black);
		if (viewport.right < layout->output_width)
			ui_draw_driver->fillRect(ui_draw_data, viewport.right, viewport.top,
				layout->output_width - viewport.right, layout->viewport_height, black);
	}

#if !defined(PSP)
	/* Native-size backends can resize or use a non-identity viewport, so their
	 * chrome must remain output-relative and is intentionally not cached. */
#if !defined(PS2)
	draw_bar_shadow();
#endif
#if defined(PS2)
	/* PS2 uses a crisp native-pixel UI. Keep the title bar opaque rather than
	 * blending the old PSP chrome over the background. */
	boxfill(0, 0, ui_layout_right(0), 23, UI_COLOR(UI_PAL_BG1));
#else
	boxfill_alpha(0, 0, ui_layout_right(0), 23, UI_COLOR(UI_PAL_BG1), 10);
#endif
	hline_alpha(0, ui_layout_right(0), 23, UI_COLOR(UI_PAL_FRAME), 12);
	hline_alpha(0, ui_layout_right(0), 24, UI_COLOR(UI_PAL_FRAME), 10);
#endif
}


/******************************************************************************
	バッテリー状態表示
******************************************************************************/

/*------------------------------------------------------
	バッテリー状態表示
------------------------------------------------------*/

int draw_battery_status(int draw)
{
#if !defined(PSP)
	/* Battery status is a PSP-only UI feature. Other platforms use power
	 * driver shims for common clock/config code, but should not render a fake
	 * battery indicator from those placeholder values. */
	(void)draw;
	return 0;
#else
	static uint32_t counter = 0;
	static int prev_bat = 0, prev_charging = 0;
	int width, icon, update = 0;
	int bat = power_driver->batteryLifePercent(power_data);
	int charging = power_driver->isBatteryCharging(power_data);
	char bat_left[4];

	counter++;

	if (draw)
	{
		if (bat > 66)		icon = ICON_BATTERY1;
		else if (bat > 33)	icon = ICON_BATTERY2;
		else if (bat >= 10) icon = ICON_BATTERY3;
		else				icon = ICON_BATTERY4;

		if ((charging && (counter % 60 < 40)) || !charging)
			small_icon_shadow(407, 3, UI_COLOR(UI_PAL_TITLE), icon);

		uifont_print_shadow(432, 5, UI_COLOR(UI_PAL_TITLE), "[");
		uifont_print_shadow(462, 5, UI_COLOR(UI_PAL_TITLE), "%]");

		if (bat >= 0 && bat <= 100)
			sprintf(bat_left, "%3d", bat);
		else
			strcpy(bat_left, "---");

		width = uifont_get_string_width(bat_left);
		uifont_print_shadow(462 - width, 5, UI_COLOR(UI_PAL_TITLE), bat_left);

		if (!charging && (bat < 10) && (counter % 120 < 40))
		{
			int sx, sy, ex, ey;
			char message[128];

			sprintf(message, TEXT(WARNING_BATTERY_IS_LOW_PLEASE_CHARGE_BATTERY), bat);
			width = uifont_get_string_width(message);

				sx = (ui_layout_get()->logical_width - width) >> 1;
				sy = (ui_layout_get()->logical_height - FONTSIZE) >> 1;
			ex = sx + width;
			ey = sy + FONTSIZE;

			draw_dialog(sx - FONTSIZE/2, sy - FONTSIZE/2, ex + FONTSIZE/2, ey + FONTSIZE/2);

			uifont_print_shadow_center(sy, UI_COLOR(UI_PAL_WARNING), message);
		}
	}

	update |= (charging != prev_charging);
	update |= (bat != prev_bat);

	if (charging)
		update |= ((counter % 60 == 39) || (counter % 60 == 59));
	else
		update |= ((bat < 10) && ((counter % 120 == 39) || (counter % 120 == 119)));

	prev_bat = bat;
	prev_charging = charging;

	return update;
#endif
}


/******************************************************************************
	メインボリューム表示
******************************************************************************/

/*------------------------------------------------------
	メインボリューム表示
------------------------------------------------------*/

int draw_volume_status(int draw)
{
	if (platform_driver->getDevkitVersion(platform_data) >= 0x03050210 && systembuttons_available)
	{
		static uint64_t disp_end = 0;
		int volume = readMainVolume();
		int update = 0;

		if (volume < 0 || volume > 30)
			return 0;

		if (readVolumeButtons())
		{
			disp_end = ticker_driver->currentUs(ticker_data) + 2 * CLOCKS_PER_SEC;
			update = UI_FULL_REFRESH;
			draw = 1;
		}

		if (disp_end != 0)
		{
			if (ticker_driver->currentUs(ticker_data) < disp_end)
			{
				if (draw)
					draw_volume(volume);
					update = UI_FULL_REFRESH;
			}
			else
			{
				disp_end = 0;
				update |= UI_FULL_REFRESH;
			}
		}
		return update;
	}
	return 0;
}


/******************************************************************************
	ダイアログボックス表示
******************************************************************************/

/*------------------------------------------------------
	ダイアログボックス表示
------------------------------------------------------*/

void draw_dialog(int sx, int sy, int ex, int ey)
{
	draw_box_shadow(sx, sy, ex, ey);

	hline_alpha(sx, ex - 1, sy, UI_COLOR(UI_PAL_FRAME), 10);
	vline_alpha(ex, sy, ey - 1, UI_COLOR(UI_PAL_FRAME), 10);
	hline_alpha(sx + 1, ex, ey, UI_COLOR(UI_PAL_FRAME), 10);
	vline_alpha(sx, sy + 1, ey, UI_COLOR(UI_PAL_FRAME), 10);

	sx++;
	ex--;
	sy++;
	ey--;

	hline_alpha(sx, ex - 1, sy, UI_COLOR(UI_PAL_FRAME), 12);
	vline_alpha(ex, sy, ey - 1, UI_COLOR(UI_PAL_FRAME), 12);
	hline_alpha(sx + 1, ex, ey, UI_COLOR(UI_PAL_FRAME), 12);
	vline_alpha(sx, sy + 1, ey, UI_COLOR(UI_PAL_FRAME), 12);

	sx++;
	ex--;
	sy++;
	ey--;

#if defined(PS2)
	/* The PSP dialog intentionally lets the wallpaper bleed through. At native
	 * PS2 resolution that reads as banding/noise, especially on interlaced
	 * output. Use an opaque panel while retaining the outer shadow/border. */
	boxfill(sx, sy, ex, ey, UI_COLOR(UI_PAL_BG1));
#else
	boxfill_alpha(sx, sy, ex, ey, UI_COLOR(UI_PAL_BG1), 10);
#endif
}


/******************************************************************************
	スクロールバー表示
******************************************************************************/

/*------------------------------------------------------
	スクロールバー表示
------------------------------------------------------*/

void draw_scrollbar(int sx, int sy, int ex, int ey, int disp_lines, int total_lines, int current_line)
{
	int height = (ey - sy) + 1;

	if (total_lines > disp_lines)
	{
		int line_height;
		int bar_size, bar_blank, bar_top;

		boxfill_alpha(sx, sy, ex, sy + 9, UI_COLOR(UI_PAL_FRAME), 14);
		boxfill_alpha(sx, sy + 10, ex, ey - 10, UI_COLOR(UI_PAL_FRAME), 6);
		boxfill_alpha(sx, ey - 9, ex, ey, UI_COLOR(UI_PAL_FRAME), 14);

		uifont_print(sx - 2, sy - 2, UI_COLOR(UI_PAL_SELECT), FONT_UPTRIANGLE);
		uifont_print(sx - 2, ey - 11, UI_COLOR(UI_PAL_SELECT), FONT_DOWNTRIANGLE);

		height -= 23;//height -= 23
		sy += 11;

		if (total_lines > 4)//total_lines > 4
			bar_size = height >> 2;// 最低限必要なサイズheight >> 2
		else
			bar_size = height / total_lines;
		line_height = (height - bar_size) / (total_lines - 1);
		bar_blank = (total_lines - 1) * line_height;
		bar_size  = height - bar_blank;
		bar_top = line_height * current_line;

		sy = sy + bar_top;
		ey = sy + bar_size;

		hline_alpha(sx, ex - 1, sy, UI_COLOR(UI_PAL_FRAME), 12);
		vline_alpha(ex, sy, ey - 1, UI_COLOR(UI_PAL_FRAME), 12);
		hline_alpha(sx + 1, ex, ey, UI_COLOR(UI_PAL_FRAME), 12);
		vline_alpha(sx, sy + 1, ey, UI_COLOR(UI_PAL_FRAME), 12);

		sx++;
		ex--;
		sy++;
		ey--;

		boxfill_alpha(sx, sy, ex, ey, UI_COLOR(UI_PAL_BG1), 12);
	}
}


/******************************************************************************
	プログレスバー表示
******************************************************************************/

static int progress_total;
static int progress_current;
static char progress_message[64];

/*--------------------------------------------------------
	プログレスバー初期化
--------------------------------------------------------*/

void init_progress(int total, const char *text)
{
	int cx = ui_layout_center_x();
	int cy = ui_layout_center_y();

	progress_current = 0;
	progress_total   = total;
	strcpy(progress_message, text);

	video_driver->beginFrame(video_data);
	draw_dialog(cx - 158, cy - 26, cx + 158, cy + 26);
	boxfill(cx - 151, cy + 4, cx + 151, cy + 16, 0, 0, 0);

	uifont_print_shadow_center(cy - 18, 255,255,255, text);
	draw_battery_status(1);
	video_driver->endFrame(video_data);

	video_driver->flipScreen(video_data, 1);
}


/*--------------------------------------------------------
	プログレスバー更新
--------------------------------------------------------*/

void update_progress(void)
{
	int cx = ui_layout_center_x();
	int cy = ui_layout_center_y();
	int width = (++progress_current * 100 / progress_total) * 3;

	video_driver->beginFrame(video_data);
	show_background();

	draw_dialog(cx - 158, cy - 26, cx + 158, cy + 26);
	boxfill(cx - 151, cy + 4, cx + 151, cy + 16, 0, 0, 0);

	uifont_print_shadow_center(cy - 18, 255,255,255, progress_message);
	draw_battery_status(1);

	boxfill(cx - 150, cy + 5, cx - 150 + width - 1, cy + 15, 128, 128, 128);
	video_driver->endFrame(video_data);

	video_driver->flipScreen(video_data, 1);
}


/*--------------------------------------------------------
	プログレスバー結果表示
--------------------------------------------------------*/

void show_progress(const char *text)
{
	int cx = ui_layout_center_x();
	int cy = ui_layout_center_y();

	video_driver->beginFrame(video_data);
	show_background();

	draw_dialog(cx - 158, cy - 26, cx + 158, cy + 26);
	boxfill(cx - 151, cy + 4, cx + 151, cy + 16, 0, 0, 0);

	uifont_print_shadow_center(cy - 18, 255,255,255, text);
	draw_battery_status(1);

	if (progress_current)
	{
		int width = (progress_current * 100 / progress_total) * 3;
		boxfill(cx - 150, cy + 5, cx - 150 + width - 1, cy + 15, 128, 128, 128);
	}
	video_driver->endFrame(video_data);

	video_driver->flipScreen(video_data, 1);
}


/******************************************************************************
	ポップアップメッセージ
******************************************************************************/

static int ui_popup_updated = 0;
static int ui_popup_counter = 0;
static int ui_popup_prev_counter = 0;
static char ui_popup_message[128];


/*--------------------------------------------------------
	ポップアップメッセージをリセット
--------------------------------------------------------*/

void ui_popup_reset(void)
{
	memset(ui_popup_message, 0, sizeof(ui_popup_message));
	ui_popup_updated = 0;
	ui_popup_counter = 0;
	ui_popup_prev_counter = 0;
}

/*--------------------------------------------------------
	ポップアップメッセージを登録
--------------------------------------------------------*/

void ui_popup(const char *text, ...)
{
	va_list arg;

	va_start(arg, text);
	vsprintf(ui_popup_message, text, arg);
	va_end(arg);

	ui_popup_counter = 2 * 60;
	ui_popup_updated = 1;
}


/*--------------------------------------------------------
	ポップアップメッセージを表示
--------------------------------------------------------*/

int ui_show_popup(int draw)
{
	int update = ui_popup_updated;

	ui_popup_updated = 0;

	if (ui_popup_counter > 0)
	{
		if (ui_popup_prev_counter == 0)
			update = 1;

		if (draw)
		{
			int sx, sy, ex, ey;
			int width = uifont_get_string_width(ui_popup_message);

				sx = (ui_layout_get()->logical_width - width) >> 1;
				sy = (ui_layout_get()->logical_height - FONTSIZE) >> 1;
			ex = sx + width;
			ey = sy + (FONTSIZE - 1);

			draw_dialog(sx - FONTSIZE/2, sy - FONTSIZE/2, ex + FONTSIZE/2, ey + FONTSIZE/2);
			uifont_print_center(sy, COLOR_WHITE, ui_popup_message);
		}

		ui_popup_counter--;
		ui_popup_prev_counter = ui_popup_counter;

		if (update || !ui_popup_counter) return UI_FULL_REFRESH;
	}
	return update;
}


/******************************************************************************
	簡易書式付文字列表示
******************************************************************************/

#define MAX_LINES	32
#define MIN_X		24
#define MIN_Y		47
#define INC_Y		16

static int cy;
static int linefeed;
static int text_r = 0xff;
static int text_g = 0xff;
static int text_b = 0xff;
static int msg_icon;
static char msg_title[128];
static char msg_lines[MAX_LINES][128];
static int msg_r[MAX_LINES];
static int msg_g[MAX_LINES];
static int msg_b[MAX_LINES];

static int msg_visible_lines(void)
{
	int rows;
	int text_bottom = ui_layout_bottom(12);

	/* Keep the historical 13 rows on the 480x272 PSP canvas, but consume
	 * extra logical height on larger outputs (24 rows at PS2 640x448). */
	rows = ((text_bottom - MIN_Y - FONTSIZE) / INC_Y) + 1;
	if (rows < 1)
		rows = 1;
	if (rows > MAX_LINES)
		rows = MAX_LINES;
	return rows;
}


/*--------------------------------------------------------
	メッセージ初期化
--------------------------------------------------------*/

void msg_screen_init(int wallpaper, int icon, const char *title)
{
	cy = 0;
	linefeed = 1;
	msg_icon = icon;
	strncpy(msg_title, title, sizeof(msg_title) - 1);
	msg_title[sizeof(msg_title) - 1] = '\0';
	memset(msg_lines, 0, sizeof(msg_lines));

	load_background(wallpaper);
}


/*--------------------------------------------------------
	メッセージ消去
--------------------------------------------------------*/

void msg_screen_clear(void)
{
	cy = 0;
	linefeed = 1;
}


/*--------------------------------------------------------
	テキストカラー設定
--------------------------------------------------------*/

void msg_set_text_color(uint32_t color)
{
	text_r = (color >>  0) & 0xff;
	text_g = (color >>  8) & 0xff;
	text_b = (color >> 16) & 0xff;
}


/*--------------------------------------------------------
	メッセージ表示
--------------------------------------------------------*/

void msg_printf(const char *text, ...)
{
	int y;
	int visible_lines;
	char buf[128];
	va_list arg;

	va_start(arg, text);
	vsprintf(buf, text, arg);
	va_end(arg);
	visible_lines = msg_visible_lines();

	if (linefeed)
	{
		if (cy >= visible_lines)
		{
			int keep = visible_lines - 1;
			int first = cy - keep;

			for (y = 0; y < keep; y++)
			{
				strcpy(msg_lines[y], msg_lines[first + y]);
				msg_r[y] = msg_r[first + y];
				msg_g[y] = msg_g[first + y];
				msg_b[y] = msg_b[first + y];
			}
			cy = keep;
		}
		strcpy(msg_lines[cy], buf);
	}
	else
	{
		strcat(msg_lines[cy], buf);
	}

	msg_r[cy] = text_r;
	msg_g[cy] = text_g;
	msg_b[cy] = text_b;

	video_driver->beginFrame(video_data);
	show_background();
	small_icon_shadow(6, 3, UI_COLOR(UI_PAL_TITLE), msg_icon);
	uifont_print_shadow(32, 5, UI_COLOR(UI_PAL_TITLE), msg_title);
	draw_dialog(14, 37, ui_layout_right(14), ui_layout_bottom(12));
	draw_battery_status(1);
	draw_volume_status(1);

	for (y = 0; y <= cy && y < visible_lines; y++)
		uifont_print(MIN_X, MIN_Y + y * 16, msg_r[y], msg_g[y], msg_b[y], msg_lines[y]);
	video_driver->endFrame(video_data);

	if (buf[strlen(buf) - 1] == '\n')
	{
		linefeed = 1;
		cy++;
	}
	else
	{
		if (buf[strlen(buf) - 1] == '\r')
			msg_lines[cy][0] = '\0';
		linefeed = 0;
	}

	video_driver->flipScreen(video_data, 1);
}


/******************************************************************************
	メッセージボックス
******************************************************************************/

#define MB_MES_MAX	8

enum
{
	MBT_OKONLY = 0,
	MBT_YESNO,
	MBT_MAX
};


#define MB_SET_TYPE(n)	mb.type = n;

#define MB_SET_TEXT(col, n)								\
	mb.mes[count].r = &ui_palette[UI_PAL_##col].r;		\
	mb.mes[count].g = &ui_palette[UI_PAL_##col].g;		\
	mb.mes[count].b = &ui_palette[UI_PAL_##col].b;		\
	mb.mes[count++].text = TEXT(n);

#define MB_SET_BLANK()									\
	mb.mes[count].r = &ui_palette[UI_PAL_NORMAL].r;		\
	mb.mes[count].g = &ui_palette[UI_PAL_NORMAL].g;		\
	mb.mes[count].b = &ui_palette[UI_PAL_NORMAL].b;		\
	mb.mes[count++].text = TEXT(LF);

#define MB_END											\
	mb.mes[count].r = &ui_palette[UI_PAL_NORMAL].r;		\
	mb.mes[count].g = &ui_palette[UI_PAL_NORMAL].g;		\
	mb.mes[count].b = &ui_palette[UI_PAL_NORMAL].b;		\
	mb.mes[count].text = TEXT(EOM);


typedef struct message_t
{
	int *r, *g, *b;
	const char *text;
} UI_MESSAGE;

typedef struct dialog_t
{
	int type;
	UI_MESSAGE mes[MB_MES_MAX];
} UI_MESSAGEBOX;


/*--------------------------------------------------------
	メッセージボックス初期化
--------------------------------------------------------*/

static UI_MESSAGEBOX *messagebox_init(int number)
{
	static UI_MESSAGEBOX mb;
	int count = 0;

	switch (number)
	{
	case MB_STARTEMULATION:
		MB_SET_TYPE(MBT_YESNO)
		MB_SET_TEXT(INFO, START_EMULATION)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, LAUNCH_OR_CANCEL)
		MB_END
		break;

#ifdef ADHOC
	case MB_STARTEMULATION_ADHOC:
		MB_SET_TYPE(MBT_YESNO)
		MB_SET_TEXT(INFO, START_EMULATION_ADHOC)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, LAUNCH_OR_CANCEL)
		MB_END
		break;
#endif

	case MB_EXITEMULATION:
		MB_SET_TYPE(MBT_YESNO)
		MB_SET_TEXT(INFO, EXIT_EMULATION)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, CONFIRM_OR_CANCEL)
		MB_END
		break;
		
	case MB_RETURNTOFILEBROWSER:
		MB_SET_TYPE(MBT_YESNO)
		MB_SET_TEXT(INFO, RETURN_TO_FILE_BROWSER)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, CONFIRM_OR_CANCEL)
		MB_END
		break;

	case MB_RESETEMULATION:
		MB_SET_TYPE(MBT_YESNO)
		MB_SET_TEXT(INFO, RESET_EMULATION)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, CONFIRM_OR_CANCEL)
		MB_END
		break;

	case MB_RESTARTEMULATION:
		MB_SET_TYPE(MBT_OKONLY)
		MB_SET_TEXT(INFO, NEED_TO_RESTART_EMULATION)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, PRESS_ANY_BUTTON)
		MB_END
		break;

#if (EMU_SYSTEM != NCDZ)
	case MB_GAMENOTWORK:
		MB_SET_TYPE(MBT_OKONLY)
		MB_SET_TEXT(INFO, THIS_GAME_DOESNT_WORK)
		MB_SET_BLANK()
		MB_SET_TEXT(NORMAL, YOU_WONT_BE_ABLE_TO_MAKE_IT_WORK_CORRECTRY)
		MB_SET_TEXT(NORMAL, DONT_BOTHER)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, PRESS_ANY_BUTTON)
		MB_END
		break;
#endif

	case MB_SETSTARTUPDIR:
		MB_SET_TYPE(MBT_YESNO)
		MB_SET_TEXT(INFO, DO_YOU_MAKE_THIS_DIRECTORY_THE_STARTUP_DIRECTORY)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, CONFIRM_OR_CANCEL)
		MB_END
		break;

#ifdef SAVE_STATE
	case MB_STARTSAVESTATE:
		MB_SET_TYPE(MBT_YESNO)
		MB_SET_TEXT(INFO, START_SAVESTATE)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, CONFIRM_OR_CANCEL)
		MB_END
		break;

	case MB_FINISHSAVESTATE:
		MB_SET_TYPE(MBT_YESNO)
		MB_SET_TEXT(INFO, COMPLETE)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, GAME_OR_MENU)
		MB_END
		break;

	case MB_STARTLOADSTATE:
		MB_SET_TYPE(MBT_YESNO)
		MB_SET_TEXT(INFO, START_LOADSTATE)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, CONFIRM_OR_CANCEL)
		MB_END
		break;

	case MB_FINISHLOADSTATE:
		MB_SET_TYPE(MBT_OKONLY)
		MB_SET_TEXT(INFO, COMPLETE)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, PRESS_ANY_BUTTON)
		MB_END
		break;

	case MB_DELETESTATE:
		MB_SET_TYPE(MBT_YESNO)
		MB_SET_TEXT(INFO, DELETE_STATE_FILE)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, CONFIRM_OR_CANCEL)
		MB_END
		break;
#endif
#if (EMU_SYSTEM == NCDZ)
	case MB_STARTEMULATION_NOMP3:
		MB_SET_TYPE(MBT_YESNO)
		MB_SET_TEXT(INFO, START_EMULATION)
		MB_SET_BLANK()
		MB_SET_TEXT(WARNING, MP3_NOT_FOUND)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, LAUNCH_OR_CANCEL)
		MB_END
		break;

	case MB_BOOTBIOS:
		MB_SET_TYPE(MBT_YESNO)
		MB_SET_TEXT(INFO, BOOT_NEOCD_BIOS)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, CONFIRM_OR_CANCEL)
		MB_END
		break;

	case MB_BIOSNOTFOUND:
		MB_SET_TYPE(MBT_OKONLY)
		MB_SET_TEXT(INFO, NEOGEO_CDZ_BIOS_NOT_FOUND)
		MB_SET_BLANK()
		MB_SET_TEXT(WARNING, CANNOT_LAUNCH_GAME)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, PRESS_ANY_BUTTON)
		MB_END
		break;

	case MB_BIOSINVALID:
		MB_SET_TYPE(MBT_OKONLY)
		MB_SET_TEXT(INFO, INVALID_NEOGEO_CDZ_BIOS_FOUND)
		MB_SET_BLANK()
		MB_SET_TEXT(WARNING, CANNOT_LAUNCH_GAME)
		MB_SET_BLANK()
		MB_SET_TEXT(SELECT, PRESS_ANY_BUTTON)
		MB_END
		break;
#endif
	}

	return &mb;
};


/*--------------------------------------------------------
	メッセージボックス表示
--------------------------------------------------------*/

int messagebox(int number)
{
	int i, w, lines, width, height;
	int sx, sy, ex, ey, res = 0;
	const UI_MESSAGEBOX *mb;

	if (number > MB_NUM_MAX) return 0;

	mb = messagebox_init(number);

	video_driver->beginFrame(video_data);
	video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, &full_rect, &full_rect);

	boxfill_alpha(0, 0, ui_layout_right(0), ui_layout_bottom(0), COLOR_BLACK, 8);

	lines = 0;
	width = 0;

	while (mb->mes[lines].text[0] != '\0' && lines < MB_MES_MAX)
		lines++;

	for (i = 0; i < lines; i++)
	{
		w = uifont_get_string_width(mb->mes[i].text);
		if (width < w) width = w;
	}

	width >>= 1;			// width / 2
	height = lines << 3;	// (line * (FONTSIZE + 2)) / 2

	sx = ui_layout_center_x() - width;
	ex = ui_layout_center_x() + width;
	sy = ui_layout_center_y() - height;
	ey = ui_layout_center_y() + height;

	draw_dialog(sx - 21, sy - 21, ex + 21, ey + 21);

	sy++;

	for (i = 0; i < lines; i++)
	{
		int r = *mb->mes[i].r;
		int g = *mb->mes[i].g;
		int b = *mb->mes[i].b;

		uifont_print_shadow_center(sy + (i << 4), r, g, b, mb->mes[i].text);
	}

	video_driver->endFrame(video_data);
	video_driver->flipScreen(video_data, 1);
	pad_wait_clear();

	if (mb->type)
	{
		do
		{
			video_driver->waitVsync(video_data);
			pad_update();

			if (pad_pressed(PLATFORM_PAD_B1))
			{
				res = 1;
				pad_wait_clear();
				break;
			}

			if (Loop == LOOP_EXIT) break;

		} while (!pad_pressed(PLATFORM_PAD_B2));
	}
	else
	{
		while (1)
		{
			video_driver->waitVsync(video_data);
			pad_update();

			if (pad_pressed_any())
			{
				res = 1;
				pad_wait_clear();
				break;
			}

			if (Loop == LOOP_EXIT) break;
		}
	}

	pad_wait_clear();
	video_driver->flipScreen(video_data, 1);

	return res;
}


/******************************************************************************
	ヘルプ
******************************************************************************/

#define HELP_MES_MAX	11

#define HELP_SET_NAME(n)	help.menu_name = TEXT(n);

#define HELP_ENABLE(n)										\
	help.mes[count].r = &ui_palette[UI_PAL_SELECT].r;		\
	help.mes[count].g = &ui_palette[UI_PAL_SELECT].g;		\
	help.mes[count].b = &ui_palette[UI_PAL_SELECT].b;		\
	help.mes[count++].text2 = TEXT(n);

#define HELP_DISABLE()										\
	help.mes[count].r = &ui_palette[UI_PAL_NORMAL].r;		\
	help.mes[count].g = &ui_palette[UI_PAL_NORMAL].g;		\
	help.mes[count].b = &ui_palette[UI_PAL_NORMAL].b;		\
	help.mes[count++].text2 = TEXT(HELP_NOT_USE);

#define HELP_END											\
	help.mes[count].r = &ui_palette[UI_PAL_NORMAL].r;		\
	help.mes[count].g = &ui_palette[UI_PAL_NORMAL].g;		\
	help.mes[count].b = &ui_palette[UI_PAL_NORMAL].b;		\
	help.mes[count].text2 = TEXT(EOM);


typedef struct help_message_t
{
	int *r, *g, *b;
	const char *text1;
	const char *text2;
} UI_HELPMESSAGE;

typedef struct help_t
{
	const char *menu_name;
	UI_HELPMESSAGE mes[HELP_MES_MAX];
} UI_HELP;


static UI_HELP *help_init(int number)
{
	static UI_HELP help;
	int count = 0;

	help.mes[0].text1 = FONT_UPARROW FONT_DOWNARROW;
	help.mes[1].text1 = FONT_LEFTARROW FONT_RIGHTARROW;
	help.mes[2].text1 = FONT_CIRCLE;
	help.mes[3].text1 = FONT_CROSS;
	help.mes[4].text1 = FONT_SQUARE;
	help.mes[5].text1 = FONT_TRIANGLE;
	help.mes[6].text1 = FONT_LTRIGGER;
	help.mes[7].text1 = FONT_RTRIGGER;
	help.mes[8].text1 = TEXT(PSP_BUTTON_NAME11);
	help.mes[9].text1 = TEXT(PSP_BUTTON_NAME12);
	help.mes[10].text1 = NULL;

	switch (number)
	{
	case HELP_FILEBROWSER:
		HELP_SET_NAME(FILE_BROWSER)
		HELP_ENABLE(SCROLL)
		HELP_ENABLE(SCROLL_1_PAGE)
		HELP_ENABLE(LAUNCH_GAME)
		HELP_DISABLE()
#ifdef ADHOC
		HELP_ENABLE(LAUNCH_GAME_ADHOC)
#else
		HELP_DISABLE()
#endif
		HELP_ENABLE(EXIT_EMULATOR)
		HELP_DISABLE()
#if (EMU_SYSTEM == MVS)
		HELP_ENABLE(SHOW_BIOS_SELECT_MENU)
#elif (EMU_SYSTEM == NCDZ)
		HELP_ENABLE(BOOT_BIOS)
#else
		HELP_DISABLE()
#endif
		HELP_ENABLE(SET_SELECTED_DIRECTORY_AS_STARTUP)
		HELP_ENABLE(SHOW_THIS_HELP)
		HELP_END
		break;

	case HELP_MAINMENU:
		HELP_SET_NAME(MAIN_MENU)
		HELP_ENABLE(SCROLL)
		HELP_DISABLE()
		HELP_ENABLE(SELECT)
		HELP_ENABLE(MENU_RETURN_TO_GAME)
		HELP_DISABLE()
		HELP_DISABLE()
		HELP_DISABLE()
		HELP_ENABLE(SAVE_LOAD_STATE)
		HELP_DISABLE()
		HELP_ENABLE(SHOW_THIS_HELP)
		HELP_END
		break;

#if (EMU_SYSTEM == MVS)
	case HELP_SELECTBIOS:
		HELP_SET_NAME(BIOS_SELECT_MENU)
		HELP_ENABLE(SCROLL)
		HELP_DISABLE()
		HELP_ENABLE(SELECT)
		HELP_ENABLE(RETURN_TO_FILE_BROWSER)
		HELP_DISABLE()
		HELP_DISABLE()
		HELP_ENABLE(RETURN_TO_FILE_BROWSER)
		HELP_DISABLE()
		HELP_DISABLE()
		HELP_ENABLE(SHOW_THIS_HELP)
		HELP_END
		break;
#endif

	case HELP_GAMECONFIG:
		HELP_SET_NAME(GAME_CONFIGURATION_MENU)
		HELP_ENABLE(SCROLL)
		HELP_ENABLE(CHANGE_VALUE)
		HELP_ENABLE(SELECT)
		HELP_ENABLE(RETURN_TO_MAIN_MENU)
		HELP_DISABLE()
		HELP_DISABLE()
		HELP_ENABLE(SCROLL_1_PAGE)
		HELP_ENABLE(SCROLL_1_PAGE)
		HELP_DISABLE()
		HELP_ENABLE(SHOW_THIS_HELP)
		HELP_END
		break;

	case HELP_KEYCONFIG:
		HELP_SET_NAME(KEY_CONFIGURATION_MENU)
		HELP_ENABLE(SCROLL)
		HELP_ENABLE(CHANGE_VALUE)
		HELP_ENABLE(SELECT)
		HELP_ENABLE(RETURN_TO_MAIN_MENU)
		HELP_DISABLE()
		HELP_DISABLE()
		HELP_ENABLE(SCROLL_1_PAGE)
		HELP_ENABLE(SCROLL_1_PAGE)
		HELP_DISABLE()
		HELP_ENABLE(SHOW_THIS_HELP)
		HELP_END
		break;

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == MVS)
	case HELP_DIPSWITCH:
		HELP_SET_NAME(DIP_SWITCH_SETTINGS_MENU)
		HELP_ENABLE(SCROLL)
		HELP_ENABLE(CHANGE_VALUE)
		HELP_ENABLE(SELECT)
		HELP_ENABLE(RETURN_TO_MAIN_MENU)
		HELP_DISABLE()
		HELP_DISABLE()
		HELP_ENABLE(SCROLL_1_PAGE)
		HELP_ENABLE(SCROLL_1_PAGE)
		HELP_DISABLE()
		HELP_ENABLE(SHOW_THIS_HELP)
		HELP_END
		break;
#endif
#ifdef SAVE_STATE
	case HELP_STATE:
		HELP_SET_NAME(SAVE_LOAD_STATE)
		HELP_ENABLE(SELECT_SLOT)
		HELP_ENABLE(CHANGE_FUNCTION)
		HELP_ENABLE(EXECUTE_FUNCTION)
		HELP_ENABLE(RETURN_TO_MAIN_MENU)
		HELP_DISABLE()
		HELP_DISABLE()
		HELP_ENABLE(RETURN_TO_MAIN_MENU)
		HELP_DISABLE()
		HELP_DISABLE()
		HELP_ENABLE(SHOW_THIS_HELP)
		HELP_END
		break;
#endif
#ifdef COMMAND_LIST
	case HELP_COMMANDLIST:
		HELP_SET_NAME(_COMMAND_LIST)
		HELP_ENABLE(SCROLL_SELECT_ITEM)
		HELP_ENABLE(SCROLL_1_PAGE)
		HELP_ENABLE(OPEN_CLOSE_ITEM_MENU)
		HELP_ENABLE(RETURN_TO_MAIN_MENU_GAME)
		HELP_DISABLE()
		HELP_DISABLE()
		HELP_ENABLE(SCROLL_1_PAGE)
		HELP_ENABLE(SCROLL_1_PAGE)
		HELP_DISABLE()
		HELP_ENABLE(SHOW_THIS_HELP)
		HELP_END
		break;
#endif
		case HELP_CHEATCONFIG:
		HELP_SET_NAME(CHEAT_CONFIGURATION_MENU)
		HELP_ENABLE(SCROLL)
		HELP_ENABLE(CHANGE_VALUE)
		HELP_DISABLE()
		HELP_ENABLE(RETURN_TO_MAIN_MENU)
		HELP_DISABLE()
		HELP_DISABLE()
		HELP_ENABLE(SCROLL_1_PAGE)
		HELP_ENABLE(SCROLL_1_PAGE)
		HELP_DISABLE()
		HELP_ENABLE(SHOW_THIS_HELP)
		HELP_END
		break;
	}

	return &help;
};


/*--------------------------------------------------------
	ヘルプ表示
--------------------------------------------------------*/

int help(int number)
{
	int i;
	int help_left;
	char title[256];
	const UI_HELP *help;

	if (number > HELP_NUM_MAX) return 0;

	help = help_init(number);

	video_driver->beginFrame(video_data);
	video_driver->copyRect(video_data, COMMON_GRAPHIC_OBJECTS_SHOW_FRAME_BUFFER, COMMON_GRAPHIC_OBJECTS_DRAW_FRAME_BUFFER, &full_rect, &full_rect);

	boxfill_alpha(0, 0, ui_layout_right(0), ui_layout_bottom(0), COLOR_BLACK, 8);
	{
		const int dialog_width = 360;
		const int dialog_height = ui_layout_get()->logical_height - 42;
		const int sx = ui_layout_center_x() - dialog_width / 2;
		const int sy = 34;

		draw_dialog(sx, sy, sx + dialog_width, sy + dialog_height);
		help_left = sx + 14;
	}

	sprintf(title, TEXT(HELP_TITLE), help->menu_name);
	uifont_print_shadow_center(43, UI_COLOR(UI_PAL_INFO), title);

	for (i = 0; help->mes[i].text2[0]; i++)
	{
		int r = *help->mes[i].r;
		int g = *help->mes[i].g;
		int b = *help->mes[i].b;

		uifont_print(help_left, 70 + (i << 4), r, g, b, help->mes[i].text1);
		uifont_print(help_left + 70, 70 + (i << 4), r, g, b, help->mes[i].text2);
	}

	uifont_print_shadow_center(ui_layout_bottom(32), UI_COLOR(UI_PAL_SELECT),
		TEXT(PRESS_ANY_BUTTON_TO_RETURN_TO_MENU));

	video_driver->endFrame(video_data);
	video_driver->flipScreen(video_data, 1);
	pad_wait_press(PAD_WAIT_INFINITY);
	video_driver->flipScreen(video_data, 1);

	return 0;
}
