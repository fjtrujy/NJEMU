#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "ps2/ps2_audio_submit.h"

#define TEST_BYTES 21000
#define SCRIPT_LENGTH 16

static unsigned char sample_data[TEST_BYTES];
static int scripted_results[SCRIPT_LENGTH];
static int script_count;
static int script_index;
static int wait_count;
static int wait_failure_at;
static uint32_t accepted;

static void prepare(const int *script, int count)
{
	int i;
	for (i = 0; i < TEST_BYTES; i++)
		sample_data[i] = (unsigned char)((i * 7 + 3) & 255);
	memset(scripted_results, 0, sizeof(scripted_results));
	for (i = 0; i < count; i++)
		scripted_results[i] = script[i];
	script_count = count;
	script_index = 0;
	wait_count = 0;
	wait_failure_at = 0;
	accepted = 0;
}

static int fake_wait(int bytes)
{
	assert(bytes > 0 && bytes <= 8192);
	wait_count++;
	return wait_count == wait_failure_at ? 5 : 0;
}

static int fake_play(const char *chunk, int bytes)
{
	int result;
	assert(script_index < script_count);
	assert(bytes > 0 && bytes <= 8192);
	result = scripted_results[script_index++];
	if (result >= 0 && result <= bytes)
	{
		assert(memcmp(chunk, sample_data + accepted, (size_t)result) == 0);
		accepted += (uint32_t)result;
	}
	return result;
}

static void test_full_submission(void)
{
	const int script[] = {5888};
	ps2_audio_submit_result_t result;
	prepare(script, 1);
	result = ps2_audio_submit_buffer(sample_data, 5888, 1, fake_wait, fake_play);
	assert(result.error == PS2_AUDIO_SUBMIT_OK);
	assert(result.accepted_bytes == 5888);
	assert(result.wait_calls == 1 && result.submit_calls == 1);
	assert(result.partial_calls == 0 && result.first_submit_bytes == 5888);
}

static void test_partial_and_zero_recovery(void)
{
	const int script[] = {40, 0, 1800, 4048};
	ps2_audio_submit_result_t result;
	prepare(script, 4);
	result = ps2_audio_submit_buffer(sample_data, 5888, 1, fake_wait, fake_play);
	assert(result.error == PS2_AUDIO_SUBMIT_OK);
	assert(result.accepted_bytes == 5888 && accepted == 5888);
	assert(result.wait_calls == 4 && result.submit_calls == 4);
	assert(result.first_submit_bytes == 40);
	assert(result.partial_calls == 3 && result.zero_progress_calls == 1);
}

static void test_stalled_producer_is_bounded(void)
{
	const int script[] = {0, 0, 0};
	ps2_audio_submit_result_t result;
	prepare(script, 3);
	result = ps2_audio_submit_buffer(sample_data, 5888, 1, fake_wait, fake_play);
	assert(result.error == PS2_AUDIO_SUBMIT_STALLED);
	assert(result.accepted_bytes == 0);
	assert(result.wait_calls == 3 && result.submit_calls == 3);
}

static void test_wait_error_preserves_accepted_prefix(void)
{
	const int script[] = {100};
	ps2_audio_submit_result_t result;
	prepare(script, 1);
	wait_failure_at = 2;
	result = ps2_audio_submit_buffer(sample_data, 5888, 1, fake_wait, fake_play);
	assert(result.error == PS2_AUDIO_SUBMIT_WAIT_ERROR);
	assert(result.accepted_bytes == 100);
	assert(result.wait_calls == 2 && result.submit_calls == 1);
}

static void test_negative_and_invalid_play_results(void)
{
	const int negative[] = {-7};
	const int oversized[] = {6000};
	ps2_audio_submit_result_t result;
	prepare(negative, 1);
	result = ps2_audio_submit_buffer(sample_data, 5888, 1, fake_wait, fake_play);
	assert(result.error == PS2_AUDIO_SUBMIT_PLAY_ERROR);
	assert(result.accepted_bytes == 0);
	prepare(oversized, 1);
	result = ps2_audio_submit_buffer(sample_data, 5888, 1, fake_wait, fake_play);
	assert(result.error == PS2_AUDIO_SUBMIT_INVALID_REPLY);
	assert(result.accepted_bytes == 0);
}

static void test_legacy_one_shot(void)
{
	const int script[] = {40};
	ps2_audio_submit_result_t result;
	prepare(script, 1);
	result = ps2_audio_submit_buffer(sample_data, 5888, 0, fake_wait, fake_play);
	assert(result.error == PS2_AUDIO_SUBMIT_OK);
	assert(result.accepted_bytes == 40);
	assert(result.submit_calls == 1 && result.partial_calls == 1);
}

static void test_large_buffer_chunking(void)
{
	const int script[] = {8192, 8192, 4616};
	ps2_audio_submit_result_t result;
	prepare(script, 3);
	result = ps2_audio_submit_buffer(sample_data, TEST_BYTES, 1, fake_wait, fake_play);
	assert(result.error == PS2_AUDIO_SUBMIT_OK);
	assert(result.accepted_bytes == TEST_BYTES);
	assert(result.submit_calls == 3 && result.partial_calls == 0);
}

int main(void)
{
	test_full_submission();
	test_partial_and_zero_recovery();
	test_stalled_producer_is_bounded();
	test_wait_error_preserves_accepted_prefix();
	test_negative_and_invalid_play_results();
	test_legacy_one_shot();
	test_large_buffer_chunking();
	return 0;
}
