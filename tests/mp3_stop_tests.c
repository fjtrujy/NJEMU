#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

#include "common/audio_driver.h"
#include "common/thread_driver.h"
#include "common/ui_text_driver.h"

static void fake_wakeup(void *thread);
static void fake_resume(void *thread);
static void fake_cancel(void *audio);

/* Include the real decoder implementation to exercise its static lifecycle
 * state without introducing test-only production entry points. */
#include "../src/common/mp3.c"

volatile int Sleep;
volatile int cdda_playing;
volatile int cdda_autoloop;
volatile int cdda_command_ack;
void *ui_text_data;

static const char *fake_get_text(void *data, ui_text_id_t id)
{
	(void)data;
	(void)id;
	return "test";
}

ui_text_driver_t ui_text_common = {
	.getText = fake_get_text,
};

void ui_popup(const char *text, ...)
{
	(void)text;
	assert(!"unexpected UI popup");
}

void fatalerror(const char *text, ...)
{
	(void)text;
	assert(!"unexpected fatal error");
}

static int wakeup_count;
static int resume_count;
static int cancel_count;
static int unblock_on_wakeup;

static void fake_wakeup(void *thread)
{
	assert(thread == (void *)(uintptr_t)1);
	wakeup_count++;
	if (unblock_on_wakeup)
		mp3_running = 0;
}

static void fake_resume(void *thread)
{
	assert(thread == (void *)(uintptr_t)1);
	resume_count++;
}

static void fake_cancel(void *audio)
{
	assert(audio == (void *)(uintptr_t)2);
	cancel_count++;
	if (!unblock_on_wakeup)
		mp3_running = 0;
}

static thread_driver_t mock_thread = {
	.wakeupThread = fake_wakeup,
	.resumeThread = fake_resume,
};
thread_driver_t *const thread_driver = &mock_thread;

static audio_driver_t mock_audio = {
	.cancelPannedOutput = fake_cancel,
};
audio_driver_t *const audio_driver = &mock_audio;

static void prepare(int status, int wake_resolves)
{
	mp3_thread = (void *)(uintptr_t)1;
	mp3_handle = (void *)(uintptr_t)2;
	mp3_active = 1;
	mp3_running = 1;
	mp3_status = status;
	wakeup_count = 0;
	resume_count = 0;
	cancel_count = 0;
	unblock_on_wakeup = wake_resolves;
}

static void test_stop_blocked_output(void)
{
	prepare(MP3_PLAY, 0);
	mp3_stop();
	assert(mp3_status == MP3_STOP && mp3_running == 0);
	assert(wakeup_count == 1 && resume_count == 0);
	assert(cancel_count == 2);
}

static void test_stop_sleeping_seek(void)
{
	prepare(MP3_SLEEP, 1);
	mp3_stop();
	assert(mp3_status == MP3_STOP && mp3_running == 0);
	assert(wakeup_count == 1 && resume_count == 0);
	assert(cancel_count == 2);
}

static void test_stop_paused_thread(void)
{
	prepare(MP3_PAUSE, 0);
	mp3_stop();
	assert(mp3_status == MP3_STOP && mp3_running == 0);
	assert(resume_count == 1 && wakeup_count == 1);
	assert(cancel_count == 2);
}

static void test_stop_while_menu_sleeping(void)
{
	prepare(MP3_STOP, 0);
	Sleep = 1;
	mp3_fd = -1;
	assert(MP3SleepCheck() == 1);
	Sleep = 0;
}

static void test_failed_read_stops_decoder(void)
{
	prepare(MP3_PLAY, 0);
	mp3_fd = -1;
	Sleep = 0;
	cdda_playing = CDDA_PLAY;
	MP3Update();
	assert(mp3_status == MP3_STOP);
	assert(cdda_playing == CDDA_STOP);
}

static void test_empty_read_stops_decoder(void)
{
	prepare(MP3_PLAY, 0);
	mp3_fd = open("/dev/null", O_RDONLY);
	assert(mp3_fd >= 0);
	Sleep = 0;
	cdda_playing = CDDA_PLAY;
	MP3Update();
	assert(mp3_status == MP3_STOP);
	assert(cdda_playing == CDDA_STOP);
	assert(mp3_fd == -1);
}

int main(void)
{
	test_stop_blocked_output();
	test_stop_sleeping_seek();
	test_stop_paused_thread();
	test_stop_while_menu_sleeping();
	test_failed_read_stops_decoder();
	test_empty_read_stops_decoder();
	puts("MP3 stop and failed-read tests passed");
	return 0;
}
