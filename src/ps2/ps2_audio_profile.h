#ifndef PS2_AUDIO_PROFILE_H
#define PS2_AUDIO_PROFILE_H

#include <stdint.h>

void ps2_audio_profile_record_output(uint32_t requested_bytes, int wait_status,
	int submitted_bytes, int sampled_queue, int available_before,
	int queued_after);

#endif /* PS2_AUDIO_PROFILE_H */
