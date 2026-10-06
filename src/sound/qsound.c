/*****************************************************************************

	qsound.c

	CAPCOM QSound Emulator (CPS1/CPS2)

******************************************************************************/

#include <string.h>
#include "sound/qsound.h"
#include "common/capcom_driver_info.h"
#include "common/sound.h"
#if defined(AUDIO_PRODUCER_JOBS)
#include "common/audio_producer_driver.h"
#include "common/audio_profile.h"
#include "common/qsound_mix_job.h"
#endif
#if (EMU_SYSTEM == CPS1)
#include "cps1/memintrf.h"
#elif (EMU_SYSTEM == CPS2)
#include "cps2/memintrf.h"
#endif

#define QSOUND_CHANNELS 16

typedef int8_t  QSOUND_SRC_SAMPLE;
typedef int16_t QSOUND_SAMPLE;
typedef int32_t QSOUND_SAMPLE_MIX;


/******************************************************************************
	Local Variables/Structures
******************************************************************************/

typedef struct
{
	int bank;			/* bank (x16)	*/
	int address;		/* start address */
	int pitch;			/* pitch */
	int loop;			/* loop address */
	int end;			/* end address */
	int vol;			/* master volume */
	int pan;			/* Pan value */

	/* Work variables */
	int key;			/* Key on / key off */

	int lvol;			/* left volume */
	int rvol;			/* right volume */
	int lastdt;			/* last sample value */
	int offset;			/* current offset counter */
} QSOUND_CHANNEL;

static QSOUND_CHANNEL ALIGN16_DATA qsound_channel[QSOUND_CHANNELS];

static QSOUND_SRC_SAMPLE *qsound_sample_rom;

static int qsound_data;
static int qsound_volume_shift;
#if defined(AUDIO_PRODUCER_JOBS)
static qsound_mix_job_t *qsound_mix_job;
#endif

static const int ALIGN16_DATA qsound_pan_table[33] =
{
	  0, 45, 64, 78, 90,101,110,119,
	128,135,143,150,156,163,169,175,
	181,186,191,197,202,207,212,217,
	221,226,230,235,239,243,247,251,
	256
};

#if defined(AUDIO_PRODUCER_JOBS)
static bool qsound_update_me(int32_t **buffer, int length)
{
	uint64_t wait_start;
	int ch;

	if (!buffer || !buffer[0] || !buffer[1] || length <= 0 ||
		(uint32_t)length > QSOUND_MIX_JOB_MAX_SAMPLES ||
		!audio_producer_driver->canRunJobs())
		return false;

	if (!qsound_mix_job)
	{
		qsound_mix_job = audio_producer_driver->acquireJobBuffer(
			sizeof(*qsound_mix_job), 64);
		if (!qsound_mix_job)
			return false;
	}

	qsound_mix_job->samples = (uint32_t)length;
	qsound_mix_job->error = 0;
	for (ch = 0; ch < QSOUND_CHANNELS; ch++)
	{
		QSOUND_CHANNEL *channel = &qsound_channel[ch];
		qsound_mix_channel_job_t *job_channel = &qsound_mix_job->channel[ch];
		int i;

		job_channel->left_gain =
			(channel->lvol * channel->vol) >> qsound_volume_shift;
		job_channel->right_gain =
			(channel->rvol * channel->vol) >> qsound_volume_shift;
		memset(job_channel->sample, 0, (size_t)length);
		if (!channel->key)
			continue;

		for (i = 0; i < length; i++)
		{
			int count = channel->offset >> 16;

			channel->offset &= 0xffff;
			if (count)
			{
				const QSOUND_SRC_SAMPLE *source = qsound_sample_rom + channel->bank;

				channel->address += count;
				if (channel->address >= channel->end)
				{
					if (!channel->loop)
					{
						channel->key = 0;
						break;
					}
					channel->address = (channel->end - channel->loop) & 0xffff;
				}
				channel->lastdt = source[channel->address];
			}
			job_channel->sample[i] = (int8_t)channel->lastdt;
			channel->offset += channel->pitch;
		}
	}

	if (!audio_producer_driver->submitJob(qsound_mix_job_run,
			qsound_mix_job, sizeof(*qsound_mix_job)))
	{
		/* State has already advanced to this block boundary.  Running the same
		 * pure mixer locally preserves correctness if dispatch fails. */
		qsound_mix_job_run(qsound_mix_job);
	}
	else
	{
		wait_start = audio_profile_now_us();
		audio_producer_driver->waitJob();
		audio_profile_add(AUDIO_PROFILE_ME_JOB_WAIT,
			audio_profile_now_us() - wait_start);
	}

	if (qsound_mix_job->error)
		return false;
	for (ch = 0; ch < length; ch++)
	{
		buffer[0][ch] += qsound_mix_job->left[ch];
		buffer[1][ch] += qsound_mix_job->right[ch];
	}
	return true;
}
#endif


/******************************************************************************
	Local Functions
******************************************************************************/

/*--------------------------------------------------------
	Sound Stream Generation
--------------------------------------------------------*/

static void qsound_update(int32_t **buffer, int length)
{
	int ch;

#if defined(AUDIO_PRODUCER_JOBS)
	if (qsound_update_me(buffer, length))
		return;
#endif

	for (ch = 0; ch < QSOUND_CHANNELS; ch++)
	{
		QSOUND_CHANNEL *pC = &qsound_channel[ch];

		if (pC->key)
		{
			int i;
			QSOUND_SRC_SAMPLE *pST  = qsound_sample_rom + pC->bank;
			QSOUND_SAMPLE_MIX *bufL = buffer[0];
			QSOUND_SAMPLE_MIX *bufR = buffer[1];
			QSOUND_SAMPLE_MIX lvol  = (pC->lvol * pC->vol) >> qsound_volume_shift;
			QSOUND_SAMPLE_MIX rvol  = (pC->rvol * pC->vol) >> qsound_volume_shift;

			for (i = 0; i < length; i++)
			{
				int count = (pC->offset) >> 16;

				pC->offset &= 0xffff;

				if (count)
				{
					pC->address += count;

					if (pC->address >= pC->end)
					{
						if (!pC->loop)
						{
							pC->key = 0;
							break;
						}
						pC->address = (pC->end - pC->loop) & 0xffff;
					}

					pC->lastdt = pST[pC->address];
				}

				*bufL++ += (pC->lastdt * lvol) >> 6;
				*bufR++ += (pC->lastdt * rvol) >> 6;
				pC->offset += pC->pitch;
			}
		}
	}
}


/******************************************************************************
	QSound Interface Functions
******************************************************************************/

/*--------------------------------------------------------
	QSound Interface Initialization
--------------------------------------------------------*/

void qsound_sh_start(void)
{
	sound->stack     = 0x1000;
	sound->channels  = 2;
#if QSOUND_STREAM_48KHz
	sound->frequency = 48000;
	sound->samples   = SOUND_SAMPLES_48000;
#else
	sound->frequency = 24000;
	sound->samples   = SOUND_SAMPLES_24000;
#endif
	sound->callback  = qsound_update;

	qsound_sample_rom   = (QSOUND_SRC_SAMPLE *)memory_region_sound1;
	qsound_volume_shift = 6;
#if defined(AUDIO_PRODUCER_JOBS)
	qsound_mix_job = NULL;
#endif

#if (EMU_SYSTEM == CPS2)
	if (!strcmp(capcom_driver_name(), "csclub"))
	{
		qsound_volume_shift = 4;
	}
	else
	if (!strcmp(capcom_driver_name(), "ddsom")
	||	!strcmp(capcom_driver_name(), "vsav")
	||	!strcmp(capcom_driver_name(), "vsav2"))
	{
		qsound_volume_shift = 5;
	}
	else
	if (!strcmp(capcom_driver_name(), "batcir")
	||	!strcmp(capcom_driver_name(), "spf2t")
	||	!strcmp(capcom_driver_name(), "gigawing")
	||	!strcmp(capcom_driver_name(), "mpangj")
	||	!strcmp(capcom_driver_name(), "puzloop2"))
	{
		qsound_volume_shift = 7;
	}
#else
	if (!strncmp(capcom_driver_name(), "punish", 6))
	{
		qsound_volume_shift = 4;
	}
#endif
}


/*--------------------------------------------------------
	QSound Interface Exit
--------------------------------------------------------*/

void qsound_sh_stop(void)
{
}


/*--------------------------------------------------------
	QSound Interface Reset
--------------------------------------------------------*/

void qsound_sh_reset(void)
{
	memset(&qsound_channel, 0, sizeof(qsound_channel));
	qsound_data = 0;
}


/******************************************************************************
	Memory Handlers
******************************************************************************/

/*--------------------------------------------------------
	Sound Status Read
--------------------------------------------------------*/

READ8_HANDLER( qsound_status_r )
{
	/* Port ready bit (0x80 if ready) */
	return 0x80;
}


/*--------------------------------------------------------
	Data Write (high)
--------------------------------------------------------*/

WRITE8_HANDLER( qsound_data_h_w )
{
	qsound_data = (qsound_data & 0xff) | (data << 8);
}


/*--------------------------------------------------------
	Data Write (low)
--------------------------------------------------------*/

WRITE8_HANDLER( qsound_data_l_w )
{
	qsound_data = (qsound_data & 0xff00) | data;
}


/*--------------------------------------------------------
	Sound Command Write
--------------------------------------------------------*/

WRITE8_HANDLER( qsound_cmd_w )
{
	int ch, reg;

	if (data < 0x80)
	{
		ch = data >> 3;
		reg = data & 0x07;
	}
	else if (data < 0x90)
	{
		ch = data - 0x80;
		reg = 8;
	}
	else
	{
		/* Unknown registers */
		return;
	}

	switch (reg)
	{
	case 0: /* Bank */
		ch = (ch + 1) & 0x0f;	/* strange ... */
		qsound_channel[ch].bank = (qsound_data & 0x7f) << 16;
		break;

	case 1: /* start */
		qsound_channel[ch].address = qsound_data;
		break;

	case 2: /* pitch */
#if QSOUND_STREAM_48KHz
		qsound_channel[ch].pitch = qsound_data << 3;
#else
		qsound_channel[ch].pitch = qsound_data << 4;
#endif
		if (!qsound_data)
		{
			/* Key off */
			qsound_channel[ch].key = 0;
		}
		break;

	case 4: /* loop offset */
		qsound_channel[ch].loop = qsound_data;
		break;

	case 5: /* end */
		qsound_channel[ch].end = qsound_data;
		break;

	case 6: /* master volume */
		if (!qsound_data)
		{
			/* Key off */
			qsound_channel[ch].key = 0;
		}
		else if (!qsound_channel[ch].key)
		{
			/* Key on */
			qsound_channel[ch].key = 1;
			qsound_channel[ch].offset = 0;
			qsound_channel[ch].lastdt = 0;
		}
		qsound_channel[ch].vol = qsound_data;
		break;

	case 8: /* pan and L/R volume */
		qsound_channel[ch].pan = qsound_data;
		qsound_data = (qsound_data - 0x10) & 0x3f;
		if (qsound_data > 32) qsound_data = 32;
		qsound_channel[ch].rvol = qsound_pan_table[qsound_data];
		qsound_channel[ch].lvol = qsound_pan_table[32 - qsound_data];
		break;
	}
}


/******************************************************************************
	Save/Load State
******************************************************************************/

#ifdef SAVE_STATE

STATE_SAVE( qsound )
{
	int i;

	for (i = 0; i < QSOUND_CHANNELS; i++)
	{
		state_save_long(&qsound_channel[i].bank, 1);
		state_save_long(&qsound_channel[i].address, 1);
		state_save_long(&qsound_channel[i].pitch, 1);
		state_save_long(&qsound_channel[i].loop, 1);
		state_save_long(&qsound_channel[i].end, 1);
		state_save_long(&qsound_channel[i].vol, 1);
		state_save_long(&qsound_channel[i].pan, 1);
		state_save_long(&qsound_channel[i].key, 1);
		state_save_long(&qsound_channel[i].lvol, 1);
		state_save_long(&qsound_channel[i].rvol, 1);
		state_save_long(&qsound_channel[i].lastdt, 1);
		state_save_long(&qsound_channel[i].offset, 1);
	}
	state_save_long(&qsound_data, 1);
}

STATE_LOAD( qsound )
{
	int i;

	for (i = 0; i < QSOUND_CHANNELS; i++)
	{
		state_load_long(&qsound_channel[i].bank, 1);
		state_load_long(&qsound_channel[i].address, 1);
		state_load_long(&qsound_channel[i].pitch, 1);
		state_load_long(&qsound_channel[i].loop, 1);
		state_load_long(&qsound_channel[i].end, 1);
		state_load_long(&qsound_channel[i].vol, 1);
		state_load_long(&qsound_channel[i].pan, 1);
		state_load_long(&qsound_channel[i].key, 1);
		state_load_long(&qsound_channel[i].lvol, 1);
		state_load_long(&qsound_channel[i].rvol, 1);
		state_load_long(&qsound_channel[i].lastdt, 1);
		state_load_long(&qsound_channel[i].offset, 1);
	}
	state_load_long(&qsound_data, 1);
}

#endif /* SAVE_STATE */
