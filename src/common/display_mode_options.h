#ifndef NJEMU_DISPLAY_MODE_OPTIONS_H
#define NJEMU_DISPLAY_MODE_OPTIONS_H

/* Keep existing mode values stable in configuration files. Native Pixels
 * is an opt-in mode on PS2; other platforms keep their established menu. */
#ifdef PS2
#define DISPLAY_MENU_MAX 4
#define DISPLAY_MENU_VALUES { DISPLAY_ORIGINAL_SIZE, DISPLAY_ORIGINAL_ASPECT, \
    DISPLAY_4_3, DISPLAY_FULLSCREEN, DISPLAY_NATIVE_PIXELS }
#else
#define DISPLAY_MENU_MAX 3
#define DISPLAY_MENU_VALUES { DISPLAY_ORIGINAL_SIZE, DISPLAY_ORIGINAL_ASPECT, \
    DISPLAY_4_3, DISPLAY_FULLSCREEN }
#endif

#endif /* NJEMU_DISPLAY_MODE_OPTIONS_H */
