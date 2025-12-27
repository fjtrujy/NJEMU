/******************************************************************************

	wallpaper.c

	NCDZPSP Wallpaper Data

******************************************************************************/

#ifndef PSP_WALLPAPER_H
#define PSP_WALLPAPER_H


extern uint8_t *wallpaper[NUM_WALLPAPERS];
extern uint32_t wallpaper_size[NUM_WALLPAPERS];

void set_wallpaper(void);
void load_wallpaper(void);
void free_wallpaper(void);

#endif /* PSP_WALLPAPER_H */
