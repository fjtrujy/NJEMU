#ifndef PSP_NEOGEO_ME_SOUND_H
#define PSP_NEOGEO_ME_SOUND_H

#include <stdbool.h>
#include "psp/psp_me_sound_worker.h"

typedef bool (*psp_neogeo_me_sound_available_fn)(void *opaque);

bool psp_neogeo_me_sound_sync_init(psp_neogeo_me_sound_available_fn available,
	void *opaque);
void psp_neogeo_me_sound_sync_shutdown(void);
bool psp_neogeo_me_sound_sync_ready(void);
bool psp_neogeo_me_sound_running(void);
bool psp_neogeo_me_sound_bootstrap(const psp_me_sound_worker_dispatch_t *dispatch);
void psp_neogeo_me_sound_stop(void);
bool psp_neogeo_me_sound_reset_generation(void);

#endif /* PSP_NEOGEO_ME_SOUND_H */
