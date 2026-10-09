#ifndef PS2_AUDIO_SUBMIT_H
#define PS2_AUDIO_SUBMIT_H

#include <stdint.h>

typedef struct ps2_audio_submit_result
{
	uint32_t requested_bytes;
	uint32_t accepted_bytes;
	uint32_t wait_calls;
	uint32_t submit_calls;
	uint32_t partial_calls;
	uint32_t zero_progress_calls;
	int first_wait_status;
	int first_submit_bytes;
	int error;
} ps2_audio_submit_result_t;

enum
{
	PS2_AUDIO_SUBMIT_OK = 0,
	PS2_AUDIO_SUBMIT_WAIT_ERROR,
	PS2_AUDIO_SUBMIT_PLAY_ERROR,
	PS2_AUDIO_SUBMIT_INVALID_REPLY,
	PS2_AUDIO_SUBMIT_STALLED
};

ps2_audio_submit_result_t ps2_audio_submit_buffer(const void *buffer,
	uint32_t bytes, int retry_partial,
	int (*wait_audio)(int), int (*play_audio)(const char *, int));

#endif /* PS2_AUDIO_SUBMIT_H */
