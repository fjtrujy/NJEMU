#ifndef COMMON_EMULATOR_OPTIONS_H
#define COMMON_EMULATOR_OPTIONS_H

extern int option_showfps;
extern int option_autoframeskip;
extern int option_frameskip;
extern int option_speedlimit;
extern int option_vsync;
extern int option_display_mode;
extern int option_video_output_mode;

enum
{
	VIDEO_OUTPUT_240P = 0,
	VIDEO_OUTPUT_480I,
	VIDEO_OUTPUT_480P,
	VIDEO_OUTPUT_MODE_COUNT
};

extern int option_sound_enable;
extern int option_samplerate;
extern int option_sound_volume;

extern int machine_driver_type;
extern int machine_input_type;
extern int machine_init_type;
extern int machine_screen_type;
extern int machine_sound_type;

#endif /* COMMON_EMULATOR_OPTIONS_H */
