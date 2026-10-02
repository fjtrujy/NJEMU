#include "common/audio_producer_driver.h"

static bool cpu_init(void)
{
	return true;
}

static void cpu_shutdown(void)
{
}

static void cpu_reset(void)
{
}

static void cpu_render(audio_producer_render_fn cpu_render, int16_t *buffer)
{
	cpu_render(buffer);
}

static bool cpu_isAvailable(void)
{
	return true;
}

static const audio_producer_driver_t audio_producer_cpu = {
	"cpu",
	cpu_init,
	cpu_shutdown,
	cpu_reset,
	cpu_render,
	cpu_isAvailable,
};

const audio_producer_driver_t *const audio_producer_driver = &audio_producer_cpu;
