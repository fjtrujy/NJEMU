#include "common/audio_producer_driver.h"

/* PSP_ME_AUDIO initially selects this integration boundary while the existing
 * CPU producer remains the complete fallback/reference implementation. */
static bool psp_audio_producer_init(void)
{
	return true;
}

static void psp_audio_producer_shutdown(void)
{
}

static void psp_audio_producer_reset(void)
{
}

static void psp_audio_producer_render(audio_producer_render_fn cpu_render,
	int16_t *buffer)
{
	cpu_render(buffer);
}

static bool psp_audio_producer_isAvailable(void)
{
	return false;
}

static const audio_producer_driver_t audio_producer_psp = {
	"psp-me-fallback",
	psp_audio_producer_init,
	psp_audio_producer_shutdown,
	psp_audio_producer_reset,
	psp_audio_producer_render,
	psp_audio_producer_isAvailable,
};

const audio_producer_driver_t *const audio_producer_driver = &audio_producer_psp;
