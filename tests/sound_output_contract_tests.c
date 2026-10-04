#include <assert.h>
#include <stdint.h>

#include "common/sound.h"

int main(void)
{
	struct sound_t classic_cps1 = {0};
	struct sound_t qsound = {0};

	classic_cps1.channels = 1;
	classic_cps1.samples = SOUND_SAMPLES_44100;
	assert(classic_cps1.channels == 1);
	assert(SOUND_OUTPUT_CHANNELS == 2);
	assert(sound_output_sample_count(&classic_cps1) ==
		(uint32_t)SOUND_SAMPLES_44100 * 2);
	assert(sound_output_buffer_bytes(&classic_cps1) ==
		(uint32_t)SOUND_SAMPLES_44100 * 2 * sizeof(int16_t));

	qsound.channels = 2;
	qsound.samples = SOUND_SAMPLES_24000;
	assert(sound_output_sample_count(&qsound) ==
		(uint32_t)SOUND_SAMPLES_24000 * 2);
	assert(sound_output_buffer_bytes(&qsound) ==
		(uint32_t)SOUND_SAMPLES_24000 * 2 * sizeof(int16_t));

	return 0;
}
