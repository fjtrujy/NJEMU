/******************************************************************************

	cmdlist.c

	MAME Plus! Format Command List Viewer

******************************************************************************/

#include <limits.h>
#include <stdarg.h>
#include "emumain.h"
#include "common/ui.h"
#include "common/ui_draw.h"
#include "common/ui_layout.h"

static void fd_printf(int fd, const char *fmt, ...)
{
	char buf[512];
	va_list args;
	int n;
	va_start(args, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if (n > 0) write(fd, buf, n);
}

static ssize_t fd_readline(int fd, char *buf, size_t size)
{
	size_t i = 0;
	char c;
	while (i < size - 1) {
		if (read(fd, &c, 1) <= 0) break;
		buf[i++] = c;
		if (c == '\n') break;
	}
	buf[i] = '\0';
	return (ssize_t)i;
}


/******************************************************************************
	Constants/Macros
******************************************************************************/

#define gbk1(c)	(((c) >= 0x81 && (c) <= 0xfe))
#define gbk2(c)	((c) >= 0x40 && (c) <= 0xfe && (c) != 0x7f && (c) != 0xff)

#define _CR		0x0d
#define _LF		0x0a
#define _CRLF	"\x0d\x0a"

enum
{
	LF_WIN = 0,
	LF_MAC,
	LF_UNIX,
	LF_MAX
};

enum
{
	TAG_INFO = 0,
	TAG_CMD,
	TAG_END,
	TAG_CHARSET,
	TAG_MAX
};


/******************************************************************************
	Local Structures/Variables
******************************************************************************/

typedef struct cmdlist_t
{
	int lines;
	char **line;
} CMDLIST;

static CMDLIST **cmd;

static const char *tag_str[TAG_MAX] =
{
	"info",
	"cmd",
	"end",
	"charset"
};

static int lf_code;
static int charset;

static char *cmdbuf;
static char **cmdline;
static int num_items, show_items, rows_item, sel_item, prev_item, top_item;
static int num_lines, show_lines, rows_line, sel_line, prev_line;
static int item_sx;
static int menu_open;


/******************************************************************************
	Command List Viewer
******************************************************************************/

/*--------------------------------------------------------
	Get Tag from Line Buffer
--------------------------------------------------------*/

static int cmdlist_get_tag(char *buf)
{
	int i;

	for (i = 0; i < TAG_MAX; i++)
		if (strncasecmp(&buf[1], tag_str[i], strlen(tag_str[i])) == 0)
			return i;

	return -1;
}


/*--------------------------------------------------------
	Get Value from Line Buffer
--------------------------------------------------------*/

static char *cmdlist_get_value(char *buf)
{
	if (strtok(buf, " =\r\n\t") != NULL)
		return strtok(NULL, " =\r\n\t");

	return NULL;
}


/*--------------------------------------------------------
	Detect Text Line Feed Code
--------------------------------------------------------*/

static int check_linefeed_code(const char *buf)
{
	if (strstr(buf, _CRLF) != NULL)
	{
		// Windows / MS-DOS
		return LF_WIN;
	}
	else if (strchr(buf, _CR) != NULL)
	{
		// Mac
		return LF_MAC;
	}

	// Unix
	return LF_UNIX;
}


/*--------------------------------------------------------
	Detect Text Character Encoding
--------------------------------------------------------*/

static int check_text_encode(char *buf, int size)
{
	int i;
	int count1, count2, per;
	uint8_t *p = (uint8_t *)buf;

	count1 = 0;
	count2 = 0;

	for (i= 0; i < size;)
	{
		if (p[i] <= 0x20)
		{
			count2++;
			i++;
		}
		else if (p[i] == 0x97 && p[i + 1] == 0x97)
		{
			count2 += 2;
			i += 2;
		}
		else if (gbk1(p[i]) && gbk2(p[i + 1]))
		{
			count1++;
			i += 2;
		}
		else i++;
	}

	per = (count1 * 100) / (size - count2);

	if (per > 20)
		return CHARSET_GBK;
	else
		return CHARSET_LATIN1;
}


/*--------------------------------------------------------
	Load Command List
--------------------------------------------------------*/

void load_commandlist(const char *game_name, const char *parent_name)
{
	int fd;
	off_t file_size;
	char path[PATH_MAX];
	char lf, *p, *buf, linebuf[512];//256
	const char *name = game_name;
	int i, found, line, item;
	int size, start, end, pos;

	cmdbuf    = NULL;
	cmdline   = NULL;
	cmd       = NULL;
	charset   = CHARSET_DEFAULT;
	num_lines = 0;
	num_items = 0;

	sprintf(path, "%scommand.dat", launchDir);

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return;

	file_size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_SET);

	if (file_size <= 0 || (buf = (char *)malloc((size_t)file_size)) == NULL)
	{
		close(fd);
		return;
	}
	size = (int)file_size;

	read(fd, buf, (size_t)size);
	close(fd);

	// Line feed code check
	lf_code = check_linefeed_code(linebuf);
	if (lf_code == LF_MAC)
		lf = _CR;
	else
		lf = _LF;

	// Start command parsing
retry:
	found = 0;
	line  = 0;
	start = 0;
	end   = 0;
	pos   = 0;

	i = 0;
	while (i < size)
	{
		p = &buf[i];

		while (buf[i] != lf && buf[i] != '\0' && buf[i] != EOF)
			i++;

		buf[i++] = '\0';

		strcpy(linebuf, p);
		strcat(linebuf, "\n");

		if (linebuf[0] == '$')
		{
			switch (cmdlist_get_tag(linebuf))
			{
			case TAG_INFO:
				if (found && start && end)
				{
					// Normal termination
					found = 2;
				}
				else if (found)
				{
					// No command or abnormal format
					found = 0;
				}
				else if ((p = cmdlist_get_value(linebuf)) != NULL)
				{
					char *name2 = strtok(p, ",");

					do
					{
						if (strcasecmp(name2, name) == 0)
						{
							found = 1;
							break;
						}
					} while ((name2 = strtok(NULL, ",")) != NULL);
				}
				break;

			case TAG_CMD:
				if (found)
				{
					if (!start) start = pos;
					num_items++;
				}
				break;

			case TAG_END:
				if (start)
				{
					end = i;
					num_lines = line + 1;
				}
				break;

			case TAG_CHARSET:
				if ((p = cmdlist_get_value(linebuf)) != NULL)
				{
					if (strcasecmp(p, "GBK") == 0)
					{
						charset = CHARSET_GBK;
					}
					else if (strcasecmp(p, "Shift_JIS") == 0)
					{
						charset = CHARSET_SHIFTJIS;
					}
					else if (strcasecmp(p, "ISO-8859-1") == 0)
					{
						charset = CHARSET_ISO8859_1;
					}
					else if (strcasecmp(p, "Latin1") == 0)
					{
						charset = CHARSET_ISO8859_1;
					}
				}
				break;
			}

			if (found == 2) break;
		}

		if (start) line++;
		else pos = i;
	}

	if (!found)
	{
		if (parent_name && name != parent_name)
		{
			name = parent_name;
			goto retry;
		}

		// Exit if not found
		free(buf);
		return;
	}

	free(buf);

	// Allocate buffer and read
	size = end - start;

	if ((cmdbuf = calloc(1, size)) == NULL)
		return;

	fd = open(path, O_RDONLY);
	lseek(fd, start, SEEK_SET);
	read(fd, cmdbuf, size);
	close(fd);

	// Character code check
	if (charset == CHARSET_DEFAULT)
		charset = check_text_encode(cmdbuf, size);

	// Allocate memory for line splitting
	if ((cmdline = calloc(num_lines, sizeof(char *))) == NULL)
		goto error;

	// Split buffer into lines
	p = cmdbuf;
	for (line = 0; line < num_lines; line++)
	{
		cmdline[line] = p;

		switch (lf_code)
		{
		case LF_WIN:
			p = strstr(cmdline[line], _CRLF);
			*p++ = '\0';
			*p++ = '\0';
			break;

		case LF_MAC:
			p = strchr(cmdline[line], _CR);
			*p++ = '\0';
			break;

		case LF_UNIX:
			p = strchr(cmdline[line], _LF);
			*p++ = '\0';
			break;
		}
	}

	// Allocate command list structure
	if ((cmd = (CMDLIST **)calloc(num_items, sizeof(CMDLIST *))) == NULL)
		goto error;

	for (i = 0; i < num_items; i++)
	{
		if ((cmd[i] = (CMDLIST *)calloc(1, sizeof(CMDLIST))) == NULL)
			goto error;
	}

	// Register after checking line count of each item
	for (i = 0; i < 2; i++)
	{
		int item_line = 0;

		line  = 0;
		item  = 0;
		found = 0;

		while (line < num_lines && item < num_items)
		{
			strcpy(linebuf, cmdline[line]);

			if (linebuf[0] == '$')
			{
				switch (cmdlist_get_tag(linebuf))
				{
				case TAG_CMD:
					found = 1;
					item_line = 0;
					break;

				case TAG_END:
					found = 0;
					item++;
					break;
				}
			}
			else if (found)
			{
				if (i == 0)
					cmd[item]->lines++;
				else
					cmd[item]->line[item_line++] = cmdline[line];
			}
			line++;
		}

		if (i == 0)
		{
			for (item = 0; item < num_items; item++)
			{
				if ((cmd[item]->line = calloc(1, sizeof(char *) * cmd[item]->lines)) == NULL)
					goto error;
			}
		}
	}

	free(cmdline);
	cmdline = NULL;

	// Initialize display information
	sel_line   = 0;
	prev_line  = 0;
	rows_line  = (ui_layout_get()->logical_height - 48) /
		((charset & CHARSET_GBK) ? 14 : 16);
	if (rows_line < 1)
		rows_line = 1;
	show_lines = rows_line;
	num_lines  = cmd[0]->lines;
	if (num_lines < show_lines) show_lines = num_lines;

	top_item   = 0;
	sel_item   = 0;
	prev_item  = 0;
	rows_item  = (ui_layout_get()->logical_height - 64) / 16;
	if (rows_item < 1)
		rows_item = 1;
	show_items = rows_item;
	if (num_items < show_items) show_items = num_items;

	// Calculate item menu width
	item_sx = ui_layout_get()->logical_width;
	for (item = 0; item < num_items; item++)
	{
		int x;

		x = ui_layout_get()->logical_width -
			(strlen(cmd[item]->line[0]) * 7 + 16);
		if (item_sx > x) item_sx = x;
	}

	menu_open = 1;

	return;

error:
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

	if (cmdline)
	{
		free(cmdline);
		cmdline = NULL;
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
	int x, y, alpha;
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
		sound_thread_enable(0);
		power_driver->setLowestCpuClock(power_data);
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

			if (charset & CHARSET_GBK)
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

		power_driver->setCpuClock(power_data, platform_cpuclock);

		autoframeskip_reset();
		blit_clear_all_sprite();

		sound_thread_set_volume();
		sound_thread_enable(1);

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

#define INFO_SEEK	0
#define CMD_SEEK	1
#define END_SEEK	2

#if (EMU_SYSTEM == CPS1)
#define EXT		"cps1"
#elif (EMU_SYSTEM == CPS2)
#define EXT		"cps2"
#elif (EMU_SYSTEM == MVS)
#define EXT		"mvs"
#endif

int commandlist_size_reduction(void)
{
	int fd_zip, fd_cmd, fd_out;
	char path[PATH_MAX], path2[PATH_MAX];
	char *p, linebuf[512], rom_name[512][16];//256
	int i, j, l, found = 0, total_roms = 0;
	int num_games, charset, progress;
	int header_end, body_start, body_end;
	int line = 0, line2 = 0, num_cmd;
	int org_size, new_size;
	char *textbuf = NULL, **line_ptr = NULL;

#if (EMU_SYSTEM == NCDZ)
	for (i = 0; i < 97; i++)
	{
		strcpy(rom_name[i], games[i].name);
	}
	total_roms = 97;
#else
	sprintf(path, "%szipname." EXT, launchDir);
	fd_zip = open(path, O_RDONLY);
	if (fd_zip < 0)
	{
		sprintf(path, "%szipnamej." EXT, launchDir);
		fd_zip = open(path, O_RDONLY);
		if (fd_zip < 0)
		{
			return 0;
		}
	}

	while (fd_readline(fd_zip, linebuf, 512) > 0)
	{
		char *name = strtok(linebuf, ",");
		strcpy(rom_name[total_roms++], name);
	}
	close(fd_zip);
#endif

	sprintf(path, "%scommand.dat", launchDir);
	fd_cmd = open(path, O_RDONLY);
	if (fd_cmd < 0)
		return 0;

	pad_wait_clear();
	ui_popup_reset();
	msg_screen_init(WP_CMDLIST, ICON_COMMANDDAT, TEXT(COMMAND_DAT_SIZE_REDUCTION));

	num_games  = 0;
	header_end = -1;
	body_start = -1;
	body_end   = -1;
	charset    = CHARSET_DEFAULT;

	msg_printf(TEXT(CMDLIST_MESSAGE1));
	msg_printf(TEXT(CMDLIST_MESSAGE2));
	msg_printf(TEXT(CMDLIST_MESSAGE3));
	msg_printf(TEXT(CMDLIST_MESSAGE4));

	i = 0;
	do
	{
		video_driver->waitVsync(video_data);
		pad_update();

		if (pad_pressed(PLATFORM_PAD_B1))
		{
			i = 1;
			break;
		}

		if (Loop == LOOP_EXIT) break;

	} while (!pad_pressed(PLATFORM_PAD_B2));

	pad_wait_clear();

	if (!i)
	{
		close(fd_cmd);
		goto cancel;
	}

	org_size = (int)lseek(fd_cmd, 0, SEEK_END);
	lseek(fd_cmd, 0, SEEK_SET);

	msg_printf("\n");
	msg_printf(TEXT(CHECKING_COMMAND_DAT_FORMAT));

	// Check number of registered games
	while (fd_readline(fd_cmd, linebuf, 512) > 0)
	{
		if (strrchr(linebuf, '\r') == NULL)
		{
			if (strrchr(linebuf, '\n') == NULL)
			{
				linebuf[509] = '\r';//253
				linebuf[510] = '\n';//254
			}
		}

		if (linebuf[0] == '$')
		{
			p = strtok(linebuf, "\r\n");

			if (p && linebuf[0] == '$')
			{
				if (!strncasecmp(linebuf, "$charset", 8) && strchr(linebuf, '=') != NULL)
				{
					char *type;

					strtok(linebuf, " =");
					if ((type = strtok(NULL, " =")) != NULL)
					{
						if (!strcasecmp(type, "GBK"))
						{
							charset = CHARSET_GBK;
						}
						else if (!strcasecmp(type, "Shift_JIS"))
						{
							charset = CHARSET_SHIFTJIS;
						}
						else if (!strcasecmp(type, "ISO-8859-1") || !strcasecmp(type, "Latin1"))
						{
							charset = CHARSET_LATIN1;
						}
					}
				}
				else if (!strncmp(linebuf, "$info", 5) && strchr(linebuf, '=') != NULL)
				{
					strtok(linebuf, " =");
					if (strtok(NULL, " =") != NULL)
					{
						if (body_start == -1)
						{
							// Record first $info
							body_start = line;
						}
						num_games++;
						found = 1;
					}
				}
				else if (!strncmp(linebuf, "$end", 4))
				{
					// Record last $end
					body_end = line;
				}
			}
		}
		else if (body_start == -1)
		{
			if (!strcmp(linebuf, "\r\n")) header_end = line;
		}

		line++;
	}

	if (!found)
	{
		msg_printf(TEXT(UNKNOWN_FORMAT));
		close(fd_cmd);
		goto error;
	}
	if (line == 0)
	{
		msg_printf(TEXT(EMPTY_FILE));
		close(fd_cmd);
		goto error;
	}

	if ((line_ptr = (char **)malloc(sizeof(char *) * line)) == NULL)
	{
		msg_printf(TEXT(COULD_NOT_ALLOCATE_MEMORY));
		goto error;
	}
	if ((textbuf = (char *)malloc(org_size + 1)) == NULL)
	{
		msg_printf(TEXT(COULD_NOT_ALLOCATE_MEMORY));
		goto error;
	}
	memset(textbuf, 0, org_size + 1);

	lseek(fd_cmd, 0, SEEK_SET);
	read(fd_cmd, textbuf, org_size);
	close(fd_cmd);

	if (charset == CHARSET_DEFAULT)
	{
		charset = check_text_encode(textbuf, org_size);
	}

	// Save line positions
	line_ptr[0] = strtok(textbuf, "\n");

	i = 1;
	while (1)
	{
		if ((p = strtok(NULL, "\n")) == NULL) break;
		line_ptr[i++] = p;
	}

	for (i = 0; i < line; i++)
	{
		if ((p = strrchr(line_ptr[i], '\r')) != NULL)
			*p = '\0';
	}

	// Create backup filename
	sprintf(path2, "%scommand.org", launchDir);

	remove(path2);

	// Rename and backup (backup filename: command.org)
	if (rename(path, path2) < 0)
	{
		msg_printf(TEXT(COULD_NOT_RENAME_FILE));
		goto error;
	}

	//------------------------------------------------------------------
	// Start reduction process
	//------------------------------------------------------------------
	fd_out = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fd_out < 0)
	{
		msg_printf(TEXT(COULD_NOT_CREATE_OUTPUT_FILE));
		goto error;
	}

	l = 0;
	line2 = 0;
	num_cmd = 0;
	progress = INFO_SEEK;

	msg_printf("\n");

	if (charset != CHARSET_DEFAULT)
	{
		if (charset == CHARSET_GBK)
			fd_printf(fd_out, "$charset=gbk\r\n");
		else if (charset == CHARSET_SHIFTJIS)
			fd_printf(fd_out, "$charset=shift_jis\r\n");
		else
			fd_printf(fd_out, "$charset=latin1\r\n");

		line2++;
	}

	// Copy header
	if (header_end != -1)
	{
		for (; l <= header_end; l++)
		{
			strcpy(linebuf, line_ptr[l]);

			if (strncasecmp(linebuf, "$charset", 8) != 0)
			{
				fd_printf(fd_out, "%s\r\n", linebuf);
				line2++;
			}
		}
	}

	for (; l <= body_end; l++)
	{
		strcpy(linebuf, line_ptr[l]);

		switch (progress)
		{
		case INFO_SEEK:
			if (linebuf[0] == '$')
			{
				// Command list start
				if (!strncasecmp(linebuf, "$info", 5) && strchr(linebuf, '=') != NULL)
				{
					char *name;

					strtok(linebuf, " =");
					if ((name = strtok(NULL, " =\r\n")) != NULL)
					{
						found = 0;

						if (strchr(name, ','))
						{
							char linebuf2[512], *name2;//256

							strcpy(linebuf2, name);
							name2 = strtok(linebuf2, ",");

							do
							{
#if (EMU_SYSTEM == NCDZ)
								if (!strcasecmp(name2, "trally")) strcpy(name2, "rallych");
#endif
								for (i = 0; i < total_roms; i++)
								{
									if (!strcasecmp(name2, rom_name[i]))
									{
										found = 2;
										break;
									}
								}
								if (found) break;

							} while ((name2 = strtok(NULL, ",")) != NULL);
						}
						else
						{
#if (EMU_SYSTEM == NCDZ)
							if (!strcasecmp(name, "trally")) strcpy(name, "rallych");
#endif
							for (i = 0; i < total_roms; i++)
							{
								if (!strcasecmp(name, rom_name[i]))
								{
									found = 1;
									break;
								}
							}
						}

						if (found)
						{
							if (l != 0)
							{
								j = l;
								while (j > body_start)
								{
									if (line_ptr[j - 1][0] == '#') j--;
									else break;
								}

								while (j < l)
								{
									fd_printf(fd_out, "%s\r\n", line_ptr[j]);
									j++;
								}
							}

							msg_printf(TEXT(COPYING_x), rom_name[i]);
							fd_printf(fd_out, "$info=%s\r\n", name);
							progress = CMD_SEEK;
							num_cmd++;
							line2++;
						}
					}
				}
			}
			break;

		case CMD_SEEK:
			if (!strncasecmp(linebuf, "$cmd", 4))
			{
				// Command start
				progress = END_SEEK;
				fd_printf(fd_out, "$cmd\r\n");
				line2++;
			}
			else if (!strncasecmp(linebuf, "$info", 5))
			{
				// Next command - go back 1 line
				fd_printf(fd_out, "\r\n");
				progress = INFO_SEEK;
				l--;
			}
			break;

		case END_SEEK:
			if (!strncasecmp(linebuf, "$end", 4))
			{
				// Command end
				progress = CMD_SEEK;
				fd_printf(fd_out, "$end\r\n");
				line2++;
			}
			else
			{
				// Command list contents - output as-is
				fd_printf(fd_out, "%s\r\n", linebuf);
				line2++;
			}
			break;
		}
	}

	if (body_end < line - 1)
	{
		if (strlen(line_ptr[l]) == 0)
			l++;

		for (; l < line; l++)
		{
			strcpy(linebuf, line_ptr[l]);
			fd_printf(fd_out, "%s\r\n", linebuf);
			line2++;
		}
	}

	close(fd_out);

	{
		int tmp_fd = open(path, O_RDONLY);
		new_size = (int)lseek(tmp_fd, 0, SEEK_END);
		close(tmp_fd);
	}

	msg_printf("\n");
	msg_printf(TEXT(REDUCTION_RESULT), org_size, new_size, 100.0 - ((float)new_size / (float)org_size) * 100.0);

	msg_printf("\n");
	msg_printf(TEXT(COMPLETE));

error:
	if (textbuf) free(textbuf);
	if (line_ptr) free(line_ptr);

	msg_printf("\n");
	msg_printf(TEXT(PRESS_ANY_BUTTON2));
	pad_wait_press(PAD_WAIT_INFINITY);

cancel:
	pad_wait_clear();
	ui_popup_reset();

	load_background(WP_FILER);
	return 1;
}
