/******************************************************************************

	ui.c

	��`�����󥿥ե��`���I��

******************************************************************************/

#ifndef PSP_UI_H
#define PSP_UI_H

#define UI_FULL_REFRESH		1
#define UI_PARTIAL_REFRESH	2

/*------------------------------------------------------
	���������ʾ
------------------------------------------------------*/




/*------------------------------------------------------
	�������`��Щ`��ʾ
------------------------------------------------------*/

void draw_scrollbar(int sx, int sy, int ex, int ey, int disp_lines, int total_lines, int current_line);


/*------------------------------------------------------
	��ʽ�������б�ʾ
------------------------------------------------------*/

void msg_set_text_color(uint32_t color);



/*------------------------------------------------------
	�ץ����쥹�Щ`��ʾ
------------------------------------------------------*/

void init_progress(int total, const char *text);
void update_progress(void);
void show_progress(const char *text);


/*--------------------------------------------------------
	��å��`���ܥå�����ʾ
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
	�إ�ױ�ʾ
--------------------------------------------------------*/


#endif /* PSP_UI_H */
