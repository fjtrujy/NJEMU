#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common/audio_driver.h"
#include "ps2_audio_stubs/kernel.h"
#include "ps2_audio_stubs/audsrv.h"

extern audio_driver_t audio_ps2;

#define DECODED_SAMPLES 2944u

static void *game_audio;
static void *mp3_audio;
static int sema_signals;
static int sema_waits;
static void (*on_sema_wait)(void);

int CreateSema(ee_sema_t *sema)
{
	assert(sema->max_count == 1);
	return 7;
}

int DeleteSema(int id)
{
	assert(id == 7);
	return 0;
}

int SignalSema(int id)
{
	assert(id == 7);
	sema_signals++;
	return 0;
}

int WaitSema(int id)
{
	void (*callback)(void);
	assert(id == 7);
	assert(++sema_waits < 4);
	/* Deterministically model the consumer/canceller running while
	 * the producer waits. A second unhandled wait is a deadlock. */
	callback = on_sema_wait;
	on_sema_wait = NULL;
	assert(callback != NULL);
	callback();
	return 0;
}

int audsrv_set_format(struct audsrv_fmt_t *format)
{
	assert(format->channels == 2);
	return 0;
}

int audsrv_set_volume(int volume)
{
	(void)volume;
	return 0;
}

int audsrv_stop_audio(void)
{
	return 0;
}

int audsrv_wait_audio(int bytes)
{
	(void)bytes;
	return 0;
}

int audsrv_play_audio(const char *buffer, int bytes)
{
	(void)buffer;
	return bytes;
}

static void write_mp3(int16_t sample)
{
	int16_t decoded[DECODED_SAMPLES];
	uint32_t i;
	for (i = 0; i < DECODED_SAMPLES; ++i)
		decoded[i] = sample;
	audio_ps2.outputPannedBlocking(mp3_audio, MAX_VOLUME, MAX_VOLUME,
		decoded, sizeof(decoded));
}

static void check_mix(int16_t expected, uint32_t pairs)
{
	int16_t sound[DECODED_SAMPLES];
	uint32_t i;
	assert(pairs * 2u <= DECODED_SAMPLES);
	memset(sound, 0, sizeof(sound));
	audio_ps2.srcOutputBlocking(game_audio, MAX_VOLUME, sound,
		pairs * 2u * sizeof(int16_t));
	for (i = 0; i < pairs * 2u; i++)
		assert(sound[i] == expected);
}

static void on_consume(void)
{
	check_mix(100, DECODED_SAMPLES / 2u);
}

static void on_cancel(void)
{
	assert(audio_ps2.cancelPannedOutput != NULL);
	audio_ps2.cancelPannedOutput(mp3_audio);
}

static void on_pause(void)
{
	int16_t silence[DECODED_SAMPLES] = {0};
	audio_ps2.outputPannedBlocking(mp3_audio, 0, 0,
		silence, sizeof(silence));
}

static void reset_mp3(void)
{
	if (mp3_audio) {
		audio_ps2.release(mp3_audio);
		audio_ps2.free(mp3_audio);
	}
	mp3_audio = audio_ps2.init();
	assert(mp3_audio);
	assert(audio_ps2.chReserve(mp3_audio, DECODED_SAMPLES / 2u, 2));
	on_sema_wait = NULL;
	sema_waits = 0;
	sema_signals = 0;
}

static void test_normal_backpressure(void)
{
	int i;
	reset_mp3();
	for (i = 0; i < 5; i++)
		write_mp3(100);
	on_sema_wait = on_consume;
	write_mp3(100);
	assert(sema_waits > 0);
	assert(sema_signals > 0);
	check_mix(100, 128);
}

static void test_cancel_full_ring(void)
{
	int i;
	reset_mp3();
	for (i = 0; i < 5; i++)
		write_mp3(200);
	on_sema_wait = on_cancel;
	write_mp3(200);
	assert(sema_waits > 0);
	assert(sema_signals > 0);
	check_mix(0, 128);

	/* A new track can fill the same ring after cancellation. */
	write_mp3(300);
	check_mix(300, 128);
}

static void test_cancel_empty_ring(void)
{
	reset_mp3();
	audio_ps2.cancelPannedOutput(mp3_audio);
	check_mix(0, 128);
	write_mp3(400);
	check_mix(400, 128);
}

static void test_pause_cancels_pending_write(void)
{
	int i;
	reset_mp3();
	for (i = 0; i < 5; i++)
		write_mp3(500);
	on_sema_wait = on_pause;
	write_mp3(500);
	assert(sema_waits > 0);
	check_mix(0, 128);
	write_mp3(600);
	check_mix(600, 128);
}

int main(void)
{
	game_audio = audio_ps2.init();
	assert(game_audio);
	assert(audio_ps2.chSRCReserve(game_audio, DECODED_SAMPLES / 2u,
		44100, 2));
	test_normal_backpressure();
	test_cancel_full_ring();
	test_cancel_empty_ring();
	test_pause_cancels_pending_write();
	audio_ps2.release(mp3_audio);
	audio_ps2.free(mp3_audio);
	audio_ps2.release(game_audio);
	audio_ps2.free(game_audio);
	puts("PS2 CDDA ring cancellation tests passed");
	return 0;
}
