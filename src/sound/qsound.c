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
#include "common/cps2_me_sound_shadow.h"
#include "cps2/memintrf.h"
#endif

typedef int8_t  QSOUND_SRC_SAMPLE;
typedef int16_t QSOUND_SAMPLE;
typedef int32_t QSOUND_SAMPLE_MIX;


/******************************************************************************
	Local Variables/Structures
******************************************************************************/

static qsound_context_t ALIGN16_DATA qsound_default_context;
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
		qsound_channel_state_t *channel = &qsound_default_context.channel[ch];
		qsound_mix_channel_job_t *job_channel = &qsound_mix_job->channel[ch];
		int i;

		job_channel->left_gain =
			(channel->lvol * channel->vol) >> qsound_default_context.volume_shift;
		job_channel->right_gain =
			(channel->rvol * channel->vol) >> qsound_default_context.volume_shift;
		memset(job_channel->sample, 0, (size_t)length);
		if (!channel->key)
			continue;

		for (i = 0; i < length; i++)
		{
			int count = channel->offset >> 16;

			channel->offset &= 0xffff;
			if (count)
			{
				const QSOUND_SRC_SAMPLE *source =
					qsound_default_context.sample_rom + channel->bank;

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

void qsound_context_update(qsound_context_t *context, int32_t **buffer, int length)
{
	int ch;

	if (!context || !buffer || !buffer[0] || !buffer[1] || length <= 0 ||
		!context->sample_rom)
		return;

	for (ch = 0; ch < QSOUND_CHANNELS; ch++)
	{
		qsound_channel_state_t *pC = &context->channel[ch];

		if (pC->key)
		{
			int i;
			const QSOUND_SRC_SAMPLE *pST  = context->sample_rom + pC->bank;
			QSOUND_SAMPLE_MIX *bufL = buffer[0];
			QSOUND_SAMPLE_MIX *bufR = buffer[1];
			QSOUND_SAMPLE_MIX lvol  = (pC->lvol * pC->vol) >> context->volume_shift;
			QSOUND_SAMPLE_MIX rvol  = (pC->rvol * pC->vol) >> context->volume_shift;

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

static void qsound_update(int32_t **buffer, int length)
{
#if (EMU_SYSTEM == CPS2)
	if (cps2_me_sound_render(buffer, (uint32_t)length))
		return;
#endif
#if defined(AUDIO_PRODUCER_JOBS)
	if (qsound_update_me(buffer, length))
		return;
#endif
	qsound_context_update(&qsound_default_context, buffer, length);
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

	qsound_default_context.sample_rom = (const QSOUND_SRC_SAMPLE *)memory_region_sound1;
	qsound_default_context.volume_shift = 6;
#if defined(AUDIO_PRODUCER_JOBS)
	qsound_mix_job = NULL;
#endif

#if (EMU_SYSTEM == CPS2)
	if (!strcmp(capcom_driver_name(), "csclub"))
	{
		qsound_default_context.volume_shift = 4;
	}
	else
	if (!strcmp(capcom_driver_name(), "ddsom")
	||	!strcmp(capcom_driver_name(), "vsav")
	||	!strcmp(capcom_driver_name(), "vsav2"))
	{
		qsound_default_context.volume_shift = 5;
	}
	else
	if (!strcmp(capcom_driver_name(), "batcir")
	||	!strcmp(capcom_driver_name(), "spf2t")
	||	!strcmp(capcom_driver_name(), "gigawing")
	||	!strcmp(capcom_driver_name(), "mpangj")
	||	!strcmp(capcom_driver_name(), "puzloop2"))
	{
		qsound_default_context.volume_shift = 7;
	}
#else
	if (!strncmp(capcom_driver_name(), "punish", 6))
	{
		qsound_default_context.volume_shift = 4;
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
	memset(qsound_default_context.channel, 0, sizeof(qsound_default_context.channel));
	qsound_default_context.data = 0;
}

size_t qsound_context_size(void)
{
	return sizeof(qsound_context_t);
}

bool qsound_default_clone_for_worker(qsound_context_t *destination)
{
	if (!destination || !qsound_default_context.sample_rom)
		return false;
	*destination = qsound_default_context;
	return true;
}

bool qsound_default_restore_from_worker(const qsound_context_t *source)
{
	const int8_t *sample_rom;
	int volume_shift;

	if (!source)
		return false;
	sample_rom = qsound_default_context.sample_rom;
	volume_shift = qsound_default_context.volume_shift;
	qsound_default_context = *source;
	qsound_default_context.sample_rom = sample_rom;
	qsound_default_context.volume_shift = volume_shift;
	return true;
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
	qsound_context_data_h_w(&qsound_default_context, data);
}

void qsound_context_data_h_w(qsound_context_t *context, uint8_t data)
{
	if (context)
		context->data = (context->data & 0xff) | (data << 8);
}


/*--------------------------------------------------------
	Data Write (low)
--------------------------------------------------------*/

WRITE8_HANDLER( qsound_data_l_w )
{
	qsound_context_data_l_w(&qsound_default_context, data);
}

void qsound_context_data_l_w(qsound_context_t *context, uint8_t data)
{
	if (context)
		context->data = (context->data & 0xff00) | data;
}


/*--------------------------------------------------------
	Sound Command Write
--------------------------------------------------------*/

WRITE8_HANDLER( qsound_cmd_w )
{
	qsound_context_cmd_w(&qsound_default_context, data);
}

void qsound_context_cmd_w(qsound_context_t *context, uint8_t data)
{
	int ch, reg;
	int command_data;

	if (!context)
		return;
	command_data = context->data;

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
		context->channel[ch].bank = (command_data & 0x7f) << 16;
		break;

	case 1: /* start */
		context->channel[ch].address = command_data;
		break;

	case 2: /* pitch */
#if QSOUND_STREAM_48KHz
		context->channel[ch].pitch = command_data << 3;
#else
		context->channel[ch].pitch = command_data << 4;
#endif
		if (!command_data)
		{
			/* Key off */
			context->channel[ch].key = 0;
		}
		break;

	case 4: /* loop offset */
		context->channel[ch].loop = command_data;
		break;

	case 5: /* end */
		context->channel[ch].end = command_data;
		break;

	case 6: /* master volume */
		if (!command_data)
		{
			/* Key off */
			context->channel[ch].key = 0;
		}
		else if (!context->channel[ch].key)
		{
			/* Key on */
			context->channel[ch].key = 1;
			context->channel[ch].offset = 0;
			context->channel[ch].lastdt = 0;
		}
		context->channel[ch].vol = command_data;
		break;

	case 8: /* pan and L/R volume */
		context->channel[ch].pan = command_data;
		command_data = (command_data - 0x10) & 0x3f;
		if (command_data > 32) command_data = 32;
		context->channel[ch].rvol = qsound_pan_table[command_data];
		context->channel[ch].lvol = qsound_pan_table[32 - command_data];
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
		state_save_long(&qsound_default_context.channel[i].bank, 1);
		state_save_long(&qsound_default_context.channel[i].address, 1);
		state_save_long(&qsound_default_context.channel[i].pitch, 1);
		state_save_long(&qsound_default_context.channel[i].loop, 1);
		state_save_long(&qsound_default_context.channel[i].end, 1);
		state_save_long(&qsound_default_context.channel[i].vol, 1);
		state_save_long(&qsound_default_context.channel[i].pan, 1);
		state_save_long(&qsound_default_context.channel[i].key, 1);
		state_save_long(&qsound_default_context.channel[i].lvol, 1);
		state_save_long(&qsound_default_context.channel[i].rvol, 1);
		state_save_long(&qsound_default_context.channel[i].lastdt, 1);
		state_save_long(&qsound_default_context.channel[i].offset, 1);
	}
	state_save_long(&qsound_default_context.data, 1);
}

STATE_LOAD( qsound )
{
	int i;

	for (i = 0; i < QSOUND_CHANNELS; i++)
	{
		state_load_long(&qsound_default_context.channel[i].bank, 1);
		state_load_long(&qsound_default_context.channel[i].address, 1);
		state_load_long(&qsound_default_context.channel[i].pitch, 1);
		state_load_long(&qsound_default_context.channel[i].loop, 1);
		state_load_long(&qsound_default_context.channel[i].end, 1);
		state_load_long(&qsound_default_context.channel[i].vol, 1);
		state_load_long(&qsound_default_context.channel[i].pan, 1);
		state_load_long(&qsound_default_context.channel[i].key, 1);
		state_load_long(&qsound_default_context.channel[i].lvol, 1);
		state_load_long(&qsound_default_context.channel[i].rvol, 1);
		state_load_long(&qsound_default_context.channel[i].lastdt, 1);
		state_load_long(&qsound_default_context.channel[i].offset, 1);
	}
	state_load_long(&qsound_default_context.data, 1);
}

#endif /* SAVE_STATE */
