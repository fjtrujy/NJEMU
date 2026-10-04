/******************************************************************************

	config.c

	Application Settings File Management

******************************************************************************/

#include <limits.h>
#include <fcntl.h>
#include <stdarg.h>
#include <strings.h>
#include "emucfg.h"
#include "common/emulator_options.h"
#include "common/filer.h"
#include "common/input_driver.h"
#include "common/power_driver.h"
#include "common/runtime_paths.h"
#include "common/path_utils.h"
#include "common/ui_text_driver.h"

#ifdef ADHOC
#include "common/adhoc.h"
#endif
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "common/config.h"

/* Target-specific config fragments below reference target-owned settings and
 * input enums. Make that dependency explicit instead of relying on
 * emumain.h to inject the selected core umbrella header. */
#if (EMU_SYSTEM == CPS1)
#include "cps1/cps1.h"
#elif (EMU_SYSTEM == CPS2)
#include "cps2/cps2.h"
#elif (EMU_SYSTEM == MVS)
#include "mvs/mvs.h"
#elif (EMU_SYSTEM == NCDZ)
#include "common/mp3.h"
#include "ncdz/ncdz.h"
#endif

#define LINEBUF_SIZE	256

/* Write formatted string to a file descriptor */
static void fd_printf(int fd, const char *fmt, ...)
{
	char buf[512];
	va_list args;
	int n;
	va_start(args, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	if (n > 0) {
		ssize_t written = write(fd, buf, (size_t)n);
		(void)written;
	}
}

/* Read one line from fd into buf (up to size-1 chars); returns bytes read or 0 on EOF */
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


enum
{
	CFG_NONE = 0,
	CFG_INT,
	CFG_BOOL,
	CFG_PAD,
	CFG_STR,
	CFG_PERFORMANCE,
	CFG_VSYNC
};

enum
{
	PAD_NONE = 0,
	PAD_UP,
	PAD_DOWN,
	PAD_LEFT,
	PAD_RIGHT,
	PAD_BUTTON_1,
	PAD_BUTTON_2,
	PAD_BUTTON_3,
	PAD_BUTTON_4,
	PAD_SHOULDER_LEFT,
	PAD_SHOULDER_RIGHT,
	PAD_START,
	PAD_SELECT,
	PAD_MAX
};

typedef struct cfg_t
{
	int type;
	const char *name;
	int *value;
	int def;
	int max;
	uint32_t required_power_capabilities;
} cfg_type;

typedef struct cfg2_t
{
	int type;
	const char *name;
	char *value;
	int max_len;
} cfg2_type;


/******************************************************************************
	ローカル構造体/変数
******************************************************************************/

#define INIVERSION	23

static int ini_version;

#define INCLUDE_INIFILENAME

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
#include "common/config/cps.c"
#elif (EMU_SYSTEM == MVS)
#include "common/config/mvs.c"
#elif (EMU_SYSTEM == NCDZ)
#include "common/config/ncdz.c"
#endif

#undef INCLUDE_INIFILENAME

static cfg_type default_options[] =
{
	{ CFG_NONE,	"[System Settings]", 0, 0, 0, 0},
	{ CFG_INT,	"INIFileVersion",	&ini_version,	INIVERSION,		INIVERSION   , 0},
	{ CFG_NONE,	"[Video Backend]", 0, 0, 0, 0},
	{ CFG_INT,	"VideoBackend",	&option_video_backend,	VIDEO_BACKEND_AUTO,	VIDEO_BACKEND_COUNT - 1, 0},
	{ CFG_NONE,	"[FPS Overlay]", 0, 0, 0, 0},
	{ CFG_INT,	"FPSOffsetX",	&option_fps_offset_x,	0,	FPS_OVERLAY_OFFSET_MAX, 0},
	{ CFG_INT,	"FPSOffsetY",	&option_fps_offset_y,	0,	FPS_OVERLAY_OFFSET_MAX, 0},
#if USE_CACHE
	{ CFG_NONE,	"[Performance Settings]", 0, 0, 0, 0},
	{ CFG_INT,	"CacheReadSize",	&option_cache_read_size,	CACHE_READ_SIZE_AUTO,	CACHE_READ_SIZE_COUNT - 1, 0},
#endif
#ifdef PSP_ME_AUDIO
	{ CFG_NONE,	"[Audio Processing]", 0, 0, 0, 0},
	{ CFG_INT,	"AudioProcessor",	&option_audio_processor,	AUDIO_PROCESSOR_AUTO,	AUDIO_PROCESSOR_COUNT - 1, 0},
#endif
#ifdef PS2
	{ CFG_NONE,	"[Video Settings]", 0, 0, 0, 0},
	{ CFG_INT,	"VideoOutputMode",	&option_video_output_mode,	DEFAULT_VIDEO_OUTPUT_MODE,	VIDEO_OUTPUT_MODE_COUNT - 1, 0},
#endif
#if (EMU_SYSTEM == MVS)
	{ CFG_NONE,	"[Emulation Settings]", 0, 0, 0, 0},
	{ CFG_INT,	"NeogeoBIOS",		&neogeo_bios,	-1,	BIOS_MAX-1 , 0},
#elif (EMU_SYSTEM == NCDZ)
	{ CFG_NONE,	"[Emulation Settings]", 0, 0, 0, 0},
	{ CFG_INT,	"NeogeoRegion",		&neogeo_region,	1,	2	, 0},
#endif
	{ CFG_NONE, NULL, 0, 0, 0, 0}
};

static cfg2_type default_options2[] =
{
	{ CFG_NONE, NULL, NULL, 0}
};

#define INCLUDE_CONFIG_STRUCT

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
#include "common/config/cps.c"
#elif (EMU_SYSTEM == MVS)
#include "common/config/mvs.c"
#elif (EMU_SYSTEM == NCDZ)
#include "common/config/ncdz.c"
#endif

#undef INCLUDE_CONFIG_STRUCT

typedef struct padname_t
{
	int code;
	const char name[16];
} PADNAME;

static const PADNAME pad_name[13] =
{
	{ 0,					"NONE"		},
	{ PLATFORM_PAD_UP,			"UP"		},
	{ PLATFORM_PAD_DOWN,		"DOWN"		},
	{ PLATFORM_PAD_LEFT,		"LEFT"		},
	{ PLATFORM_PAD_RIGHT,		"RIGHT"		},
	{ PLATFORM_PAD_B2,		"BUTTON_2"		},
	{ PLATFORM_PAD_B1,		"BUTTON_1"	},
	{ PLATFORM_PAD_B3,		"BUTTON_3"	},
	{ PLATFORM_PAD_B4,	"BUTTON_4"	},
	{ PLATFORM_PAD_START,		"START"		},
	{ PLATFORM_PAD_SELECT,		"SELECT"	},
	{ PLATFORM_PAD_L,	"SHOULDER_LEFT"	},
	{ PLATFORM_PAD_R,	"SHOULDER_RIGHT"	}
};


/******************************************************************************
	ローカル関数
******************************************************************************/

/*------------------------------------------------------
	CFG_BOOLの値を読み込む
------------------------------------------------------*/

static int get_config_bool(char *str)
{
	if (!strcasecmp(str, "yes"))
		return 1;
	else
		return 0;
}

static int get_config_vsync(char *str)
{
	if (!strcasecmp(str, "adaptive"))
		return VSYNC_MODE_ADAPTIVE;
	if (!strcasecmp(str, "yes") || !strcasecmp(str, "on"))
		return VSYNC_MODE_ON;
	return VSYNC_MODE_OFF;
}


/*------------------------------------------------------
	CFG_INTの値を読み込む
------------------------------------------------------*/

static int get_config_int(char *str, int maxval)
{
	int value = atoi(str);

	if (value < 0) value = 0;
	if (value > maxval) value = maxval;
	return value;
}

static int get_config_performance_level(char *str)
{
	int value = atoi(str);

	if (value < PLATFORM_PERFORMANCE_LEVEL_LOWEST)
		return PLATFORM_PERFORMANCE_LEVEL_LOWEST;
	if (value > power_get_highest_performance_level())
		return power_get_highest_performance_level();
	return value;
}


/*------------------------------------------------------
	CFG_PADの値を読み込む
------------------------------------------------------*/

static int get_config_pad(char *str)
{
	int i;

	for (i = 0; i < PAD_MAX; i++)
	{
		if (strcmp(str, pad_name[i].name) == 0)
			return pad_name[i].code;
	}

	return pad_name[PAD_NONE].code;
}


/*------------------------------------------------------
	CFG_BOOLの値を保存する
------------------------------------------------------*/

static const char *set_config_bool(int value)
{
	if (value)
		return "yes";
	else
		return "no";
}

static const char *set_config_vsync(int value)
{
	if (value == VSYNC_MODE_ADAPTIVE)
		return "adaptive";
	return value == VSYNC_MODE_ON ? "yes" : "no";
}


/*------------------------------------------------------
	CFG_INTの値を保存する
------------------------------------------------------*/

static char *set_config_int(int value, int maxval)
{
	static char buf[16];

	if (value < 0) value = 0;
	if (value > maxval) value = maxval;

	sprintf(buf, "%d", value);

	return buf;
}


/*------------------------------------------------------
	CFG_PADの値を保存する
------------------------------------------------------*/

static const char *set_config_pad(int value)
{
	int i;

	for (i = 0; i < PAD_MAX; i++)
	{
		if (value == pad_name[i].code)
			return pad_name[i].name;
	}

	return pad_name[PAD_NONE].name;
}


/*------------------------------------------------------
	.iniファイルから設定を読み込む
------------------------------------------------------*/

static int load_inifile(const char *path, cfg_type *cfg, cfg2_type *cfg2)
{
	int fd;

	fd = open(path, O_RDONLY);
	if (fd >= 0)
	{
		int i;
		char linebuf[LINEBUF_SIZE];

		while (1)
		{
			char *name, *value;

			memset(linebuf, 0, LINEBUF_SIZE);
			if (fd_readline(fd, linebuf, LINEBUF_SIZE) == 0)
				break;

			if (linebuf[0] == ';' || linebuf[0] == '[')
				continue;

			name = strtok(linebuf, " =\r\n");
			if (name == NULL)
				continue;

			value = strtok(NULL, " =\r\n");
			if (value == NULL)
				continue;

			/* check name and value */
			for (i = 0; cfg[i].name; i++)
			{
				if (cfg[i].required_power_capabilities != 0 &&
					!power_has_capability(cfg[i].required_power_capabilities))
					continue;

				if (!strcmp(name, cfg[i].name))
				{
					switch (cfg[i].type)
					{
					case CFG_INT:   *cfg[i].value = get_config_int(value, cfg[i].max); break;
					case CFG_BOOL:  *cfg[i].value = get_config_bool(value); break;
					case CFG_VSYNC: *cfg[i].value = get_config_vsync(value); break;
					case CFG_PAD:   *cfg[i].value = get_config_pad(value); break;
					case CFG_PERFORMANCE: *cfg[i].value = get_config_performance_level(value); break;
					}
				}
			}

		}

		if (cfg2)
		{
			lseek(fd, 0, SEEK_SET);

			while (1)
			{
				char *name, *value;
				char *p1, *p2, temp[LINEBUF_SIZE];

				memset(linebuf, 0, LINEBUF_SIZE);
				if (fd_readline(fd, linebuf, LINEBUF_SIZE) == 0)
					break;

				strcpy(temp, linebuf);

				if (linebuf[0] == ';' || linebuf[0] == '[')
					continue;

				name = strtok(linebuf, " =\r\n");
				if (name == NULL)
					continue;

				value = strtok(NULL, " =\r\n");
				if (value == NULL)
					continue;

				p1 = strchr(temp, '\"');
				if (p1)
				{
					p2 = strchr(p1 + 1, '\"');
					if (p2)
					{
						value = p1 + 1;
						*p2 = '\0';
					}
				}

				/* check name and value */
				for (i = 0; cfg2[i].name; i++)
				{
					if (!strcmp(name, cfg2[i].name))
					{
						if (cfg2[i].type == CFG_STR)
						{
							memset(cfg2[i].value, 0, cfg2[i].max_len);
							strncpy(cfg2[i].value, value, cfg2[i].max_len - 1);
						}
					}
				}
			}
		}

		close(fd);

		return 1;
	}

	return 0;
}


/*------------------------------------------------------
	.iniファイルに設定を保存
------------------------------------------------------*/

static int save_inifile(const char *path, cfg_type *cfg, cfg2_type *cfg2)
{
	int fd;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0)
	{
		int i;

		fd_printf(fd, ";-------------------------------------------\r\n");
		fd_printf(fd, "; " APPNAME_STR " " VERSION_STR "\r\n");
		fd_printf(fd, ";-------------------------------------------\r\n");

		for (i = 0; cfg[i].name; i++)
		{
			if (cfg[i].required_power_capabilities != 0 &&
				!power_has_capability(cfg[i].required_power_capabilities))
				continue;

			switch (cfg[i].type)
			{
			case CFG_NONE: if (cfg[i].name) fd_printf(fd, "\r\n%s\r\n", cfg[i].name); break;
			case CFG_INT:   fd_printf(fd, "%s = %s\r\n", cfg[i].name, set_config_int(*cfg[i].value, cfg[i].max)); break;
			case CFG_BOOL:  fd_printf(fd, "%s = %s\r\n", cfg[i].name, set_config_bool(*cfg[i].value)); break;
			case CFG_VSYNC: fd_printf(fd, "%s = %s\r\n", cfg[i].name, set_config_vsync(*cfg[i].value)); break;
			case CFG_PAD:   fd_printf(fd, "%s = %s\r\n", cfg[i].name, set_config_pad(*cfg[i].value)); break;
			case CFG_PERFORMANCE: fd_printf(fd, "%s = %s\r\n", cfg[i].name,
				set_config_int(*cfg[i].value, power_get_highest_performance_level())); break;
			}
		}

		if (cfg2)
		{
			for (i = 0; cfg2[i].name; i++)
			{
				switch (cfg2[i].type)
				{
				case CFG_NONE: if (cfg2[i].name) fd_printf(fd, "\r\n%s\r\n", cfg2[i].name); break;
				case CFG_STR:  fd_printf(fd, "%s = \"%s\"\r\n", cfg2[i].name, cfg2[i].value); break;
				}
			}
		}

		close(fd);

		return 1;
	}

	return 0;
}


/******************************************************************************
	グローバル関数
******************************************************************************/

/*------------------------------------------------------
	Load Application Settings
------------------------------------------------------*/

void load_settings(void)
{
	int i;
	char path[PATH_MAX];

	for (i = 0; default_options[i].name; i++)
	{
		if (default_options[i].value)
			*default_options[i].value = default_options[i].def;
	}
#if (EMU_SYSTEM == NCDZ)
	if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_JAPANESE)
	{
		for (i = 0; default_options[i].name; i++)
		{
			if (strcmp(default_options[i].name, "NeogeoRegion") == 0)
			{
				*default_options[i].value = 0;
				break;
			}
		}
	}
#endif

	if (!path_format(path, sizeof(path), "%s%s", launchDir, inifile_name)) return;

	if (load_inifile(path, default_options, default_options2) == 0)
	{
		save_settings();
	}
	else if (ini_version != INIVERSION)
	{
		char inipath[PATH_MAX];

		for (i = 0; default_options[i].name; i++)
		{
			if (default_options[i].value)
				*default_options[i].value = default_options[i].def;
		}
#if (EMU_SYSTEM == NCDZ)
		if (ui_text_driver->getLanguage(ui_text_data) == UI_LANG_JAPANESE)
		{
			for (i = 0; default_options[i].name; i++)
			{
				if (strcmp(default_options[i].name, "NeogeoRegion") == 0)
				{
					*default_options[i].value = 0;
					break;
				}
			}
		}
#endif


		if (!path_format(inipath, sizeof(inipath), "%s%s", launchDir, inifile_name)) return;
		remove(inipath);
		delete_files("nvram", "nv");
		delete_files("config", "ini");

		save_settings();
	}
}


/*------------------------------------------------------
	Save Application Settings
------------------------------------------------------*/

void save_settings(void)
{
	char path[PATH_MAX];

	if (!path_format(path, sizeof(path), "%s%s", launchDir, inifile_name)) return;

	save_inifile(path, default_options, default_options2);
}


/*------------------------------------------------------
	ゲームの設定を読み込む
------------------------------------------------------*/

void load_gamecfg(const char *name)
{
	int i;
	char path[PATH_MAX];
	cfg_type *gamecfg;

	if (!path_format(path, sizeof(path), "%sconfig/%s.ini", launchDir, name)) return;

	memset(input_map, 0, sizeof(input_map));

#define INCLUDE_SETUP_CONFIG_STRUCT

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
#include "common/config/cps.c"
#elif (EMU_SYSTEM == MVS)
#include "common/config/mvs.c"
#elif (EMU_SYSTEM == NCDZ)
#include "common/config/ncdz.c"
#endif

#undef INCLUDE_SETUP_CONFIG_STRUCT

	for (i = 0; gamecfg[i].name; i++)
	{
		if (gamecfg[i].value)
			*gamecfg[i].value = gamecfg[i].def;
	}

#define INCLUDE_SETUP_DIPSWITCH

#if (EMU_SYSTEM == CPS1)
#include "common/config/cps.c"
#elif (EMU_SYSTEM == MVS)
#include "common/config/mvs.c"
#endif

#undef INCLUDE_SETUP_DIPSWITCH

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
	if (!machine_screen_type) cps_rotate_screen = 0;
#endif

	if (load_inifile(path, gamecfg, NULL) == 0)
	{
#ifdef ADHOC
		if (adhoc_enable)
#endif
			save_gamecfg(name);
	}
}


/*------------------------------------------------------
	ゲームの設定を保存する
------------------------------------------------------*/

void save_gamecfg(const char *name)
{
	char path[PATH_MAX];
	cfg_type *gamecfg;

	if (!path_format(path, sizeof(path), "%sconfig/%s.ini", launchDir, name)) return;

#define INCLUDE_SETUP_CONFIG_STRUCT

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
#include "common/config/cps.c"
#elif (EMU_SYSTEM == MVS)
#include "common/config/mvs.c"
#elif (EMU_SYSTEM == NCDZ)
#include "common/config/ncdz.c"
#endif

#undef INCLUDE_SETUP_CONFIG_STRUCT

	save_inifile(path, gamecfg, NULL);
}
