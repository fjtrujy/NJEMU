#ifndef PS2_AUDIO_PROFILE_H
#define PS2_AUDIO_PROFILE_H

#include <stdint.h>
#include "ps2/ps2_audio_submit.h"

void ps2_audio_profile_record_output(uint32_t requested_bytes, int wait_status,
	int submitted_bytes, int sampled_queue, int available_before,
	int queued_after);
void ps2_audio_profile_record_delivery(const ps2_audio_submit_result_t *result);

#endif /* PS2_AUDIO_PROFILE_H */
