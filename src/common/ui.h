/******************************************************************************

	ui.h

	User Interface Functions

******************************************************************************/

#ifndef COMMON_UI_H
#define COMMON_UI_H

#define UI_FULL_REFRESH		1
#define UI_PARTIAL_REFRESH	2

/*------------------------------------------------------
	Background Display
------------------------------------------------------*/




/*------------------------------------------------------
	Battery Status Display
------------------------------------------------------*/

void draw_scrollbar(int sx, int sy, int ex, int ey, int disp_lines, int total_lines, int current_line);


/*------------------------------------------------------
	Format Floating Display
------------------------------------------------------*/

void msg_set_text_color(uint32_t color);



/*------------------------------------------------------
	Progress Box Display
------------------------------------------------------*/

void init_progress(int total, const char *text);
void update_progress(void);
void show_progress(const char *text);


/*--------------------------------------------------------
	Message Box Display
--------------------------------------------------------*/

enum
{
	MB_STARTEMULATION = 0,
#ifdef ADHOC
	MB_STARTEMULATION_ADHOC,
#endif
	MB_EXITEMULATION,
	MB_RETURNTOFILEBROWSER,
	MB_RESETEMULATION,
	MB_RESTARTEMULATION,
#if (EMU_SYSTEM != NCDZ)
	MB_GAMENOTWORK,
#endif
	MB_SETSTARTUPDIR,
#ifdef LARGE_MEMORY
	MB_PSPVERSIONERROR,
#endif
#ifdef SAVE_STATE
	MB_STARTSAVESTATE,
	MB_FINISHSAVESTATE,
	MB_STARTLOADSTATE,
	MB_FINISHLOADSTATE,
	MB_DELETESTATE,
#endif
#if (EMU_SYSTEM == NCDZ)
	MB_STARTEMULATION_NOMP3,
	MB_BOOTBIOS,
	MB_BIOSNOTFOUND,
	MB_BIOSINVALID,
#endif
	MB_NUM_MAX
};

int messagebox(int number);


/*--------------------------------------------------------
	Help Display
--------------------------------------------------------*/


#endif /* COMMON_UI_H */
