#ifndef NJEMU_TEST_PS2_AUDSRV_H
#define NJEMU_TEST_PS2_AUDSRV_H

#define MAX_VOLUME 32767

struct audsrv_fmt_t {
	int bits;
	int freq;
	int channels;
};

int audsrv_set_format(struct audsrv_fmt_t *format);
int audsrv_set_volume(int volume);
int audsrv_stop_audio(void);
int audsrv_wait_audio(int bytes);
int audsrv_play_audio(const char *buffer, int bytes);

#endif
