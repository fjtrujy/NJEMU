#ifndef COMMON_UI_MENU_H
#define COMMON_UI_MENU_H

void showmenu(void);
#if defined(PS2) && defined(GUI) && (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
/* Reconcile the GS framebuffer after loading a per-game display mode. */
int ui_sync_ps2_native_scanout(int previous_display_mode);
#endif

#endif /* COMMON_UI_MENU_H */
