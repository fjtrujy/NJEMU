#ifndef PSP_AUDIO_BACKEND_H
#define PSP_AUDIO_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

#include "common/audio_producer_driver.h"

typedef bool (*psp_audio_backend_available_fn)(void *opaque);

typedef enum psp_audio_backend_reset_action
{
	PSP_AUDIO_BACKEND_RESET_KEEP_CPU = 0,
	PSP_AUDIO_BACKEND_RESET_KEEP_AVAILABLE,
	PSP_AUDIO_BACKEND_RESET_REENABLE
} psp_audio_backend_reset_action_t;

void psp_audio_backend_init(psp_audio_backend_available_fn available,
	void *opaque);
void psp_audio_backend_shutdown(void);
bool psp_audio_backend_enable(const char *context, const char *mode_name);
psp_audio_backend_reset_action_t psp_audio_backend_reset(
	bool mode_enabled, bool available, bool suspended);
void psp_audio_backend_suspend(void);
bool psp_audio_backend_resume_after_enable(void);

bool psp_audio_backend_can_run_jobs(bool available);
void *psp_audio_backend_acquire_job_buffer(uint32_t size, uint32_t alignment);
bool psp_audio_backend_submit_job(audio_producer_job_fn job, void *data,
	uint32_t size);
void psp_audio_backend_wait_job(void);

#endif /* PSP_AUDIO_BACKEND_H */
