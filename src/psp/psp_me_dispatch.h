#ifndef PSP_ME_DISPATCH_H
#define PSP_ME_DISPATCH_H

#include <stdbool.h>
#include <stdint.h>

#include "common/audio_producer_driver.h"

void psp_me_dispatch_reset_state(void);
void psp_me_dispatch_shutdown(void);
bool psp_me_dispatch_enable(const char *context, const char *mode_name);

bool psp_me_dispatch_worker_start(void (*task)(void *), void *data,
	uint32_t size, void *opaque);
void psp_me_dispatch_worker_wait(void *opaque);

void *psp_me_dispatch_acquire_job_buffer(uint32_t size, uint32_t alignment);
bool psp_me_dispatch_submit_job(audio_producer_job_fn job, void *data,
	uint32_t size);
void psp_me_dispatch_wait_job(void);

#endif /* PSP_ME_DISPATCH_H */
