#include <stddef.h>
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

static void cpu_suspend(void)
{
}

static void cpu_resume(void)
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

static bool cpu_canRunJobs(void)
{
	return false;
}

static void *cpu_acquireJobBuffer(uint32_t size, uint32_t alignment)
{
	(void)size;
	(void)alignment;
	return NULL;
}

static bool cpu_submitJob(audio_producer_job_fn job, void *data, uint32_t size)
{
	(void)size;
	job(data);
	return true;
}

static void cpu_waitJob(void)
{
}

const audio_producer_driver_t audio_producer_cpu = {
	"cpu",
	cpu_init,
	cpu_shutdown,
	cpu_reset,
	cpu_suspend,
	cpu_resume,
	cpu_render,
	cpu_isAvailable,
	cpu_canRunJobs,
	cpu_acquireJobBuffer,
	cpu_submitJob,
	cpu_waitJob,
};
