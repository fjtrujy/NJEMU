/******************************************************************************

	mp3.h

	COMMON MP3 Thread

******************************************************************************/

#ifndef COMMON_MP3_H
#define COMMON_MP3_H

#include <limits.h>

enum
{
	MP3_STOP = 0,
	MP3_SEEK,
	MP3_PAUSE,
	MP3_PLAY,
	MP3_SLEEP
};

extern char mp3_dir[PATH_MAX];
extern int option_mp3_enable;
extern int option_mp3_volume;

int mp3_thread_start(void);
void mp3_thread_stop(void);
void mp3_set_volume(void);

int mp3_play(const char *name);
void mp3_stop(void);
void mp3_pause(int pause);
void mp3_seek_set(const char *fname, uint32_t frame);
void mp3_seek_start(void);

uint32_t mp3_get_current_frame(void);
int mp3_get_status(void);

#endif /* COMMON_MP3_H */
