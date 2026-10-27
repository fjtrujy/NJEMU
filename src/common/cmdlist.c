/******************************************************************************

	cmdlist.c

	MAME Plus! Format Command List Viewer

******************************************************************************/

#include <limits.h>
#include "emucfg.h"
#include "common/cmdlist.h"
#include "common/command_data.h"
#include "common/ui_utf8.h"
#include "common/emulator_runtime.h"
#if (EMU_SYSTEM == CPS2)
#include "common/game_database.h"
#else
#include "common/game_metadata.h"
#endif
#include "common/input_driver.h"
#include "common/power_driver.h"
#include "common/runtime_paths.h"
#include "common/path_utils.h"
#include "common/sound.h"
#include "common/ui_defs.h"
#include "common/ui_text_driver.h"
#include "common/video_driver.h"

#if (EMU_SYSTEM == NCDZ)
#include "common/mp3.h"
#endif
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "common/ui.h"
#include "common/ui_draw.h"
#include "common/ui_layout.h"

#if (EMU_SYSTEM == CPS1)
#include "cps1/sprite.h"
#elif (EMU_SYSTEM == CPS2)
#include "cps2/sprite.h"
#elif (EMU_SYSTEM == MVS)
#include "mvs/sprite.h"
#elif (EMU_SYSTEM == NCDZ)
#include "ncdz/sprite.h"
#endif

/******************************************************************************
	Constants/Macros
******************************************************************************/

typedef struct cmdlist_t
{
	int lines;
	char **line;
} CMDLIST;

static CMDLIST **cmd;

static int charset;

static char *cmdbuf;
static int num_items, show_items, rows_item, sel_item, prev_item, top_item;
static int num_lines, show_lines, rows_line, sel_line, prev_line;
static int item_sx;
static int menu_open;

static int command_title_width(const char *title, int encoding)
{
    const unsigned char *p = (const unsigned char *)title;
    int width = 0;
    while (*p != 0) {
        uint32_t codepoint = 0;
        size_t consumed = encoding == CHARSET_UTF8 && *p >= 0x80
            ? ui_utf8_decode(p, &codepoint) : 0;
        if (consumed > 0) {
            if (codepoint != 0xfeffu)
                width += (codepoint <= 0xffu || codepoint == 0x30fbu) ? 7 : 14;
            p += consumed;
        } else {
            width += 7;
            ++p;
        }
    }
    return width;
}



/******************************************************************************
	Command List Viewer
******************************************************************************/

void load_commandlist(const char *game_name, const char *parent_name)
{
    char path[PATH_MAX];
    command_data_document_t parsed = {0};
    int fd, i;

    if (cmdbuf != NULL)
        free_commandlist();
    num_lines = num_items = 0;
    charset = CHARSET_DEFAULT;
    if (!path_format(path, sizeof(path), "%scommand.dat", launchDir))
        return;
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return;
    /* A fixed-size scan finds the game; only its section enters heap RAM. */
    i = command_data_load_fd(fd, game_name, parent_name, &parsed, NULL);
    close(fd);
    if (!i)
        return;

    num_items = (int)parsed.item_count;
    charset = parsed.encoding;
    cmdbuf = parsed.storage;
    parsed.storage = NULL;
    cmd = calloc(parsed.item_count, sizeof(*cmd));
    if (cmd == NULL)
        goto error;
    for (i = 0; i < num_items; ++i) {
        cmd[i] = calloc(1, sizeof(*cmd[i]));
        if (cmd[i] == NULL)
            goto error;
        cmd[i]->lines = (int)parsed.line_counts[i];
        cmd[i]->line = parsed.items[i];
        parsed.items[i] = NULL;
    }
    command_data_free(&parsed);

    sel_line = prev_line = 0;
    rows_line = (ui_layout_get()->logical_height - 48) /
        ((charset == CHARSET_GBK || charset == CHARSET_SHIFTJIS ||
          charset == CHARSET_UTF8) ? 14 : 16);
    if (rows_line < 1) rows_line = 1;
    show_lines = rows_line;
    num_lines = cmd[0]->lines;
    if (num_lines < show_lines) show_lines = num_lines;

    top_item = sel_item = prev_item = 0;
    rows_item = (ui_layout_get()->logical_height - 64) / 16;
    if (rows_item < 1) rows_item = 1;
    show_items = rows_item;
    if (num_items < show_items) show_items = num_items;
    item_sx = ui_layout_get()->logical_width;
    for (i = 0; i < num_items; ++i) {
        int x = ui_layout_get()->logical_width -
            (command_title_width(cmd[i]->line[0], charset) + 16);
        if (item_sx > x) item_sx = x;
    }
    if (item_sx < ui_layout_get()->logical_width / 4)
        item_sx = ui_layout_get()->logical_width / 4;
    menu_open = 1;
    return;
error:
    command_data_free(&parsed);
    free_commandlist();
}


/*--------------------------------------------------------
	Free Command List
--------------------------------------------------------*/

void free_commandlist(void)
{
	int i;

	if (cmd)
	{
		for (i = 0; i < num_items; i++)
		{
			if (cmd[i])
			{
				if (cmd[i]->line) free(cmd[i]->line);
				free(cmd[i]);
			}
		}
		free(cmd);
		cmd = NULL;
	}

	if (cmdbuf)
	{
		free(cmdbuf);
		cmdbuf = NULL;
	}
}


/*--------------------------------------------------------
	Display Command List
--------------------------------------------------------*/

void commandlist(int flag)
{
	int x, y, alpha = 0;
	int update = 1, menu_counter = 0;
#if (EMU_SYSTEM == NCDZ)
	int mp3_paused = 0;
#endif
	char title[80], temp[32];

	if (cmdbuf == NULL)
	{
		ui_popup(TEXT(COMMAND_LIST_FOR_THIS_GAME_NOT_FOUND));
		return;
	}

	if (flag)
	{
#if (EMU_SYSTEM == NCDZ)
		if (mp3_get_status() == MP3_PLAY)
		{
			mp3_pause(1);
			mp3_paused = 1;
		}
#endif
		sound_thread_pause(1);
		sound_thread_enable(0);
		power_set_lowest_performance_level();
	}

	pad_wait_clear();
	load_background(WP_CMDLIST);
	ui_popup_reset();

	sprintf(title, TEXT(COMMAND_LIST_TITLE), game_name);

	do
	{
		if (update)
		{
			update = 0;

			video_driver->beginFrame(video_data);
			show_background();

			small_icon_shadow(8, 3, UI_COLOR(UI_PAL_TITLE), ICON_CMDLIST);
			uifont_print_shadow(36, 5, UI_COLOR(UI_PAL_TITLE), title);
			draw_battery_status(1);

			if (charset == CHARSET_GBK || charset == CHARSET_SHIFTJIS || charset == CHARSET_UTF8)
			{
				for (y = 0; y < show_lines; y++)
					textfont_print(6, 37 + 14 * y, UI_COLOR(UI_PAL_SELECT), cmd[sel_item]->line[y + sel_line], charset);
			}
			else
			{
				for (y = 0; y < show_lines; y++)
					textfont_print(6, 37 + 16 * y, UI_COLOR(UI_PAL_SELECT), cmd[sel_item]->line[y + sel_line], charset);
			}

			x = ui_layout_get()->logical_width;
			if (menu_open)
			{
				alpha = 14;
				x = item_sx;
			}
			else if (menu_counter > 0)
			{
				alpha = 14 - ((4 - menu_counter) << 1);
				x = item_sx +
					((ui_layout_get()->logical_width - item_sx) >> 2) *
					(4 - menu_counter);
			}
			if (x < ui_layout_get()->logical_width)
			{
				boxfill_alpha(x, 25, ui_layout_right(0), ui_layout_bottom(0),
					UI_COLOR(UI_PAL_BG1), alpha);

				for (y = 0; y < rows_item; y++)
				{
					if (top_item + y >= num_items) break;

					if (top_item + y == sel_item)
					{
						uifont_print(x, 37 + 16 * y, UI_COLOR(UI_PAL_SELECT), FONT_RIGHTTRIANGLE);
						textfont_print(x + 14, 37 + 16 * y, UI_COLOR(UI_PAL_SELECT), cmd[top_item + y]->line[0], charset);
					}
					else
						textfont_print(x + 14, 37 + 16 * y, UI_COLOR(UI_PAL_NORMAL), cmd[top_item + y]->line[0], charset);
				}

				sprintf(temp, TEXT(COMMAND_LIST_ITEMS), sel_item + 1, num_items);
				x = uifont_get_string_width(temp);
				uifont_print(ui_layout_right(4) - x, ui_layout_bottom(21),
					UI_COLOR(UI_PAL_SELECT), temp);
			}
			else
			{
				if (num_lines > rows_line)
					draw_scrollbar(ui_layout_right(10), 26, ui_layout_right(0),
						ui_layout_bottom(1), 0, num_lines - rows_line + 1, sel_line);
			}

			update |= ui_show_popup(1);
			video_driver->endFrame(video_data);
			video_driver->flipScreen(video_data, 1);
		}
		else
		{
			update = ui_show_popup(0);
			video_driver->waitVsync(video_data);
		}

		update |= ui_output_update();

		if (menu_counter)
		{
			update = 1;
			menu_counter--;
		}

		prev_item = sel_item;
		prev_line = sel_line;
		pad_update();

		if (pad_pressed(PLATFORM_PAD_UP))
		{
			if (menu_open)
			{
				if (sel_item > 0)
				{
					sel_item--;
					show_lines = rows_line;
					num_lines = cmd[sel_item]->lines;
					if (num_lines < show_lines)
						show_lines = num_lines;
					sel_line = 0;
				}
			}
			else
			{
				if (sel_line > 0) sel_line--;
			}
		}
		else if (pad_pressed(PLATFORM_PAD_DOWN))
		{
			if (menu_open)
			{
				if (sel_item < num_items - 1)
				{
					sel_item++;
					show_lines = rows_line;
					num_lines = cmd[sel_item]->lines;
					if (num_lines < show_lines)
						show_lines = num_lines;
					sel_line = 0;
				}
			}
			else
			{
				if (sel_line + show_lines < num_lines) sel_line++;
			}
		}
		else if (pad_pressed(PLATFORM_PAD_L))
		{
			if (menu_open)
			{
				if (sel_item > rows_item)
				{
					sel_item -= rows_item;
				}
				else
				{
					sel_item = 0;
				}
					show_lines = rows_line;
					num_lines = cmd[sel_item]->lines;
					if (num_lines < show_lines)
						show_lines = num_lines;
					sel_line = 0;
			}
			else
			{
				if (sel_line > 0)
				{
					sel_line -= rows_line;
					if (sel_line < 0) sel_line = 0;
				}
			}
		}
		else if (pad_pressed(PLATFORM_PAD_R))
		{
			if (menu_open)
			{
				if (sel_item < num_items - rows_item)
				{
					sel_item += rows_item;
				}
				else
				{
					sel_item = num_items-1;
				}
					show_lines = rows_line;
					num_lines = cmd[sel_item]->lines;
					if (num_lines < show_lines)
						show_lines = num_lines;
					sel_line = 0;
			}
			else
			{
				if (sel_line + show_lines < num_lines)
				{
					sel_line += rows_line;
					if (sel_line + show_lines > num_lines)
					sel_line = num_lines - show_lines;
				}
			}
		}
		else if (pad_pressed(PLATFORM_PAD_LEFT))
		{
			if (sel_line > 0)
			{
				sel_line -= rows_line;
				if (sel_line < 0) sel_line = 0;
			}
		}
		else if (pad_pressed(PLATFORM_PAD_RIGHT))
		{
			if (sel_line + show_lines < num_lines)
			{
				sel_line += rows_line;
				if (sel_line + show_lines > num_lines)
					sel_line = num_lines - show_lines;
			}
		}
		else if (pad_pressed(PLATFORM_PAD_B1))
		{
			menu_open ^= 1;
			update = 1;
			if (menu_open == 0) menu_counter = 4;
			pad_wait_clear();
		}
		else if (pad_pressed(PLATFORM_PAD_SELECT))
		{
			help(HELP_COMMANDLIST);
			update = 1;
		}

		if (menu_open)
		{
			if (top_item > num_items - rows_item) top_item = num_items - rows_item;
			if (top_item < 0) top_item = 0;
			if (sel_item >= num_items) sel_item = 0;
			if (sel_item < 0) sel_item = num_items - 1;
			if (sel_item >= top_item + rows_item) top_item = sel_item - rows_item + 1;
			if (sel_item < top_item) top_item = sel_item;
		}

		if ((sel_line != prev_line) || (sel_item != prev_item))
			update = 1;

	} while (!pad_pressed(PLATFORM_PAD_B2));

	pad_wait_clear();

	if (flag)
	{
		ui_popup_reset();

		power_set_performance_level(platform_performance_level);

		autoframeskip_reset();
		blit_clear_all_sprite();

		sound_thread_set_volume();
		sound_thread_enable(1);
		sound_thread_pause(0);

#if (EMU_SYSTEM == NCDZ)
		mp3_set_volume();
		if (mp3_paused) mp3_pause(0);
#endif
	}
	else
	{
		ui_popup_reset();
		load_background(WP_LOGO);
	}
}


/******************************************************************************
	Command List Size Reduction Process
******************************************************************************/

int commandlist_size_reduction(void)
{
#if (EMU_SYSTEM == CPS2)
    game_database_t database = {0};
    game_database_game_t database_game;
#else
    game_metadata_t metadata = {0};
    game_metadata_entry_t metadata_entry;
#endif
    char path[PATH_MAX], backup[PATH_MAX], rom_name[512][16];
    const char *names[512];
    unsigned char *input = NULL, *output = NULL;
    size_t read_bytes = 0, output_size = 0, written = 0;
    off_t input_size;
    int count = 0, fd = -1, out_fd = -1, i, accepted = 0;
    int backup_moved = 0, output_created = 0;

#if (EMU_SYSTEM == CPS2)
    if (!path_format(path, sizeof(path), "%s%s", launchDir, game_database_filename()))
        return 0;
    if (game_database_open(&database, path, GAME_DATABASE_CORE_CPS2) != GAME_DATABASE_OK ||
        game_database_count(&database) > 512) {
        game_database_close(&database);
        return 0;
    }
    count = (int)game_database_count(&database);
    for (i = 0; i < count; ++i) {
        if (game_database_get_game(&database, (uint32_t)i, &database_game) != GAME_DATABASE_OK) {
            game_database_close(&database);
            return 0;
        }
        snprintf(rom_name[i], sizeof(rom_name[i]), "%s", database_game.name);
        names[i] = rom_name[i];
    }
    game_database_close(&database);
#else
    if (!path_format(path, sizeof(path), "%s%s", launchDir, game_metadata_filename()))
        return 0;
    if (game_metadata_load(&metadata, path, game_metadata_current_core()) != GAME_METADATA_OK ||
        game_metadata_count(&metadata) > 512) {
        game_metadata_unload(&metadata);
        return 0;
    }
    count = (int)game_metadata_count(&metadata);
    for (i = 0; i < count; ++i) {
        if (!game_metadata_get(&metadata, (uint32_t)i, &metadata_entry)) {
            game_metadata_unload(&metadata);
            return 0;
        }
        snprintf(rom_name[i], sizeof(rom_name[i]), "%s", metadata_entry.name);
        names[i] = rom_name[i];
    }
    game_metadata_unload(&metadata);
#endif
    if (!path_format(path, sizeof(path), "%scommand.dat", launchDir) ||
        !path_format(backup, sizeof(backup), "%scommand.org", launchDir))
        return 0;
    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    pad_wait_clear();
    ui_popup_reset();
    msg_screen_init(WP_CMDLIST, ICON_COMMANDDAT, TEXT(COMMAND_DAT_SIZE_REDUCTION));
    msg_printf(TEXT(CMDLIST_MESSAGE1));
    msg_printf(TEXT(CMDLIST_MESSAGE2));
    msg_printf(TEXT(CMDLIST_MESSAGE3));
    msg_printf(TEXT(CMDLIST_MESSAGE4));
    do {
        video_driver->waitVsync(video_data);
        pad_update();
        if (pad_pressed(PLATFORM_PAD_B1)) {
            accepted = 1;
            break;
        }
        if (Loop == LOOP_EXIT) break;
    } while (!pad_pressed(PLATFORM_PAD_B2));
    pad_wait_clear();
    if (!accepted)
        goto cancel;

    input_size = lseek(fd, 0, SEEK_END);
    if (input_size <= 0 || input_size > 16 * 1024 * 1024 ||
        lseek(fd, 0, SEEK_SET) != 0)
        goto error;
    input = malloc((size_t)input_size);
    if (input == NULL)
        goto error;
    while (read_bytes < (size_t)input_size) {
        ssize_t n = read(fd, input + read_bytes, (size_t)input_size - read_bytes);
        if (n <= 0)
            goto error;
        read_bytes += (size_t)n;
    }
    close(fd);
    fd = -1;
    msg_printf("\n");
    msg_printf(TEXT(CHECKING_COMMAND_DAT_FORMAT));
    if (!command_data_reduce(input, read_bytes, names, (size_t)count,
                             &output, &output_size)) {
        msg_printf(TEXT(UNKNOWN_FORMAT));
        goto error;
    }
    /* Do not destroy an existing backup. Roll back if output creation fails. */
    fd = open(backup, O_RDONLY);
    if (fd >= 0) {
        close(fd);
        fd = -1;
        goto error;
    }
    if (rename(path, backup) != 0)
        goto error;
    backup_moved = 1;
    out_fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (out_fd < 0)
        goto error;
    output_created = 1;
    while (written < output_size) {
        ssize_t n = write(out_fd, output + written, output_size - written);
        if (n <= 0)
            goto error;
        written += (size_t)n;
    }
    if (close(out_fd) != 0) {
        out_fd = -1;
        goto error;
    }
    out_fd = -1;
    backup_moved = 0;
    msg_printf("\n");
    msg_printf(TEXT(REDUCTION_RESULT), (int)read_bytes, (int)output_size,
               100.0 - ((float)output_size / (float)read_bytes) * 100.0);
    msg_printf("\n");
    msg_printf(TEXT(COMPLETE));
    goto finish;
error:
    msg_printf("\n");
    msg_printf(TEXT(UNKNOWN_FORMAT));
    if (out_fd >= 0) close(out_fd);
    if (backup_moved) {
        if (output_created) remove(path);
        rename(backup, path);
    }
finish:
    msg_printf("\n");
    msg_printf(TEXT(PRESS_ANY_BUTTON2));
    pad_wait_press(PAD_WAIT_INFINITY);
cancel:
    if (fd >= 0) close(fd);
    free(input);
    free(output);
    pad_wait_clear();
    ui_popup_reset();
    load_background(WP_FILER);
    return 1;
}
