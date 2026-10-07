#include <stdint.h>
#include <stddef.h>

/* Sound context/worker tests compile the production save-state helpers when
 * SAVE_STATE is enabled, but they do not link the full emulator state module. */
uint8_t *state_buffer;

/* MVS full-feature test builds also compile the cached PCM branches in
 * ym2610.c without linking the emulator cache implementation. */
int pcm_cache_enable;

uint8_t *pcm_cache_read(uint16_t block)
{
	(void)block;
	return NULL;
}
