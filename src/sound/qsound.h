/*****************************************************************************

	qsound.h

	CAPCOM QSound Emulator (CPS1/CPS2)

******************************************************************************/

#ifndef QSOUND_H
#define QSOUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "emucfg.h"
#include "common/state.h"
#include "include/memory.h"

#define QSOUND_CHANNELS 16

typedef struct qsound_channel_state
{
	int bank;
	int address;
	int pitch;
	int loop;
	int end;
	int vol;
	int pan;
	int key;
	int lvol;
	int rvol;
	int lastdt;
	int offset;
} qsound_channel_state_t;

typedef struct qsound_context
{
	qsound_channel_state_t channel[QSOUND_CHANNELS];
	const int8_t *sample_rom;
	int data;
	int volume_shift;
} qsound_context_t;

void qsound_sh_start(void);
void qsound_sh_stop(void);
void qsound_sh_reset(void);

size_t qsound_context_size(void);
bool qsound_default_clone_for_worker(qsound_context_t *destination);
bool qsound_default_restore_from_worker(const qsound_context_t *source);
void qsound_context_update(qsound_context_t *context, int32_t **buffer, int length);
void qsound_context_data_h_w(qsound_context_t *context, uint8_t data);
void qsound_context_data_l_w(qsound_context_t *context, uint8_t data);
void qsound_context_cmd_w(qsound_context_t *context, uint8_t data);

WRITE8_HANDLER( qsound_data_h_w );
WRITE8_HANDLER( qsound_data_l_w );
WRITE8_HANDLER( qsound_cmd_w );
READ8_HANDLER( qsound_status_r );

#ifdef SAVE_STATE
STATE_SAVE( qsound );
STATE_LOAD( qsound );
#endif

#endif /* QSOUND_H */
