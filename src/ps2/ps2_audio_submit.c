#include <limits.h>
#include <stddef.h>

#include "ps2/ps2_audio_submit.h"

/* Keep each call inside one audsrv EE RPC payload (16,380 bytes in PS2SDK).
 * That way the returned byte count is an accepted prefix, never a sum
 * with an unreported hole between two internal RPC packets. */
#define PS2_AUDIO_SUBMIT_CHUNK_MAX 8192u
#define PS2_AUDIO_SUBMIT_MAX_ZERO_PROGRESS 3u

ps2_audio_submit_result_t ps2_audio_submit_buffer(const void *buffer,
	uint32_t bytes, ps2_audio_submit_mode_t mode,
	int (*wait_audio)(int), int (*play_audio)(const char *, int))
{
	ps2_audio_submit_result_t result = {0};
	const char *pcm = (const char *)buffer;
	uint32_t consecutive_zero = 0;

	result.requested_bytes = bytes;
	result.first_wait_status = 0;
	result.first_submit_bytes = -1;
	if (buffer == NULL || wait_audio == NULL || play_audio == NULL || bytes > INT_MAX ||
		mode > PS2_AUDIO_SUBMIT_RETRY_DIRECT)
	{
		result.error = PS2_AUDIO_SUBMIT_INVALID_REPLY;
		return result;
	}

	while (result.accepted_bytes < bytes)
	{
		uint32_t remaining = bytes - result.accepted_bytes;
		uint32_t chunk = remaining > PS2_AUDIO_SUBMIT_CHUNK_MAX ?
			PS2_AUDIO_SUBMIT_CHUNK_MAX : remaining;
		int wait_result = 0;
		int sent;

		if (mode != PS2_AUDIO_SUBMIT_RETRY_DIRECT || consecutive_zero != 0)
		{
			wait_result = wait_audio((int)chunk);
			result.wait_calls++;
			if (result.submit_calls == 0)
				result.first_wait_status = wait_result;
			if (wait_result != 0)
			{
				result.error = PS2_AUDIO_SUBMIT_WAIT_ERROR;
				break;
			}
		}

		sent = play_audio(pcm + result.accepted_bytes, (int)chunk);
		result.submit_calls++;
		if (result.submit_calls == 1)
			result.first_submit_bytes = sent;
		if (sent < 0)
		{
			result.error = PS2_AUDIO_SUBMIT_PLAY_ERROR;
			break;
		}
		if ((uint32_t)sent > chunk)
		{
			result.error = PS2_AUDIO_SUBMIT_INVALID_REPLY;
			break;
		}
		if ((uint32_t)sent < chunk)
			result.partial_calls++;

		result.accepted_bytes += (uint32_t)sent;
		if (sent == 0)
		{
			result.zero_progress_calls++;
			if (++consecutive_zero >= PS2_AUDIO_SUBMIT_MAX_ZERO_PROGRESS)
			{
				result.error = PS2_AUDIO_SUBMIT_STALLED;
				break;
			}
		}
		else
			consecutive_zero = 0;

		if (mode == PS2_AUDIO_SUBMIT_ONCE)
			break;
	}
	return result;
}
