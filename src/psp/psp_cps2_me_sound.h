#ifndef PSP_CPS2_ME_SOUND_H
#define PSP_CPS2_ME_SOUND_H

#include <stdbool.h>
#include "psp/psp_me_qsound_worker.h"

bool psp_cps2_me_sound_sync_init(void);
void psp_cps2_me_sound_sync_shutdown(void);
bool psp_cps2_me_sound_sync_ready(void);
bool psp_cps2_me_sound_running(void);
bool psp_cps2_me_sound_bootstrap(const psp_me_qsound_worker_dispatch_t *dispatch);
void psp_cps2_me_sound_stop(void);
bool psp_cps2_me_sound_reset_generation(void);

#endif /* PSP_CPS2_ME_SOUND_H */
