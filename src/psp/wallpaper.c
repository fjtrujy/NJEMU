/******************************************************************************

	wallpaper.c

	Wallpaper Data

******************************************************************************/

#include "psp.h"
#include <limits.h>

/******************************************************************************
	Global Variables
******************************************************************************/

uint8_t *wallpaper[NUM_WALLPAPERS];
uint32_t wallpaper_size[NUM_WALLPAPERS];


/******************************************************************************
	Local Variables
******************************************************************************/

static uint8_t *wp_user[NUM_WALLPAPERS];
static uint32_t wp_user_size[NUM_WALLPAPERS];

#define INCLUDE_WALLPAPER

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
#include "psp/wallpaper/cps.c"
#elif (EMU_SYSTEM == MVS)
#include "psp/wallpaper/mvs.c"
#elif (EMU_SYSTEM == NCDZ)
#include "psp/wallpaper/ncdz.c"
#endif

#undef INCLUDE_WALLPAPER


/******************************************************************************
	Global Functions
******************************************************************************/

/*------------------------------------------------------
	Initialize Wallpaper
------------------------------------------------------*/

void set_wallpaper(void)
{
	int i;

	wallpaper[WP_LOGO]     = wallpaper_logo;
	wallpaper[WP_FILER]    = wallpaper_filer;
	wallpaper[WP_GAMECFG]  = wallpaper_gamecfg;
	wallpaper[WP_KEYCFG]   = wallpaper_keycfg;
	wallpaper[WP_STATE]    = wallpaper_state;
	wallpaper[WP_COLORCFG] = wallpaper_colorcfg;
	wallpaper[WP_DIPSW]    = wallpaper_dipsw;
	wallpaper[WP_CMDLIST]  = wallpaper_cmdlist;

#define INCLUDE_WALLPAPER_SIZE

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
#include "psp/wallpaper/cps.c"
#elif (EMU_SYSTEM == MVS)
#include "psp/wallpaper/mvs.c"
#elif (EMU_SYSTEM == NCDZ)
#include "psp/wallpaper/ncdz.c"
#endif

#undef INCLUDE_WALLPAPER_SIZE

	if (bgimage_type != BG_DEFAULT)
	{
		for (i = 0; i < NUM_WALLPAPERS; i++)
		{
			if (wp_user[i])
			{
				wallpaper[i] = wp_user[i];
				wallpaper_size[i] = wp_user_size[i];
			}
		}
	}
}


/*------------------------------------------------------
	Load Wallpaper into Memory
------------------------------------------------------*/

void load_wallpaper(void)
{
	int i, size;
	char path[PATH_MAX];
	FILE *fp;
	const char *wp_name[NUM_WALLPAPERS] =
	{
		"logo",
		"filer",
		"gamecfg",
		"keycfg",
		"state",
		"colorcfg",
		"dipsw",
		"cmdlist"
	};

	for (i = 0; i < NUM_WALLPAPERS; i++)
	{
		sprintf(path, "%sdata/%s.png", launchDir, wp_name[i]);

		wp_user[i] = NULL;
		wp_user_size[i] = 0;

		if ((fp = fopen(path, "rb")) != NULL)
		{
			fseek(fp, 0, SEEK_END);
			size = ftell(fp);
			fseek(fp, 0, SEEK_SET);

			if ((wp_user[i] = (uint8_t *)malloc(size)) != NULL)
			{
				wp_user_size[i] = size;
				fread(wp_user[i], 1, size, fp);
			}

			fclose(fp);
		}
	}
	set_wallpaper();
}


/*------------------------------------------------------
	Free Wallpaper
------------------------------------------------------*/

void free_wallpaper(void)
{
	int i;

	for (i = 0; i < NUM_WALLPAPERS; i++)
	{
		if (wp_user[i])
		{
			free(wp_user[i]);
			wp_user[i] = NULL;
			wp_user_size[i] = 0;
		}
	}
}
