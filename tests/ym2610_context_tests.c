#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/sound.h"
#include "sound/ym2610.h"

int option_samplerate;
struct sound_t *sound;

float timer_get_time(void)
{
	return 0.0f;
}

typedef struct callback_state
{
	uint32_t timer_calls;
	uint32_t irq_asserts;
	uint32_t irq_clears;
	int last_timer_channel;
	int last_timer_count;
} callback_state_t;

static void test_timer(void *opaque, int channel, int count, double step_time)
{
	callback_state_t *state = (callback_state_t *)opaque;
	(void)step_time;
	state->timer_calls++;
	state->last_timer_channel = channel;
	state->last_timer_count = count;
}

static void test_irq(void *opaque, int irq)
{
	callback_state_t *state = (callback_state_t *)opaque;
	if (irq)
		state->irq_asserts++;
	else
		state->irq_clears++;
}

static void write_reg(ym2610_context_t *context, uint8_t reg, uint8_t value)
{
	YM2610ContextWrite(context, 0, reg);
	YM2610ContextWrite(context, 1, value);
}

static void write_reg_b(ym2610_context_t *context, uint8_t reg, uint8_t value)
{
	YM2610ContextWrite(context, 2, reg);
	YM2610ContextWrite(context, 3, value);
}

static void start_adpcma_channel_zero(ym2610_context_t *context)
{
	write_reg_b(context, 0x01, 0x3f); /* ADPCM-A total level: maximum. */
	write_reg_b(context, 0x08, 0xdf); /* Center pan, channel level maximum. */
	write_reg_b(context, 0x10, 0x00); /* Start = 0x000000. */
	write_reg_b(context, 0x18, 0x00);
	write_reg_b(context, 0x20, 0x00); /* End = 0x0000ff. */
	write_reg_b(context, 0x28, 0x00);
	write_reg_b(context, 0x00, 0x01); /* Key on channel 0. */
}

static void start_adpcmb(ym2610_context_t *context)
{
	write_reg(context, 0x11, 0xc0); /* Center pan. */
	write_reg(context, 0x12, 0x00); /* Start = 0x000000. */
	write_reg(context, 0x13, 0x00);
	write_reg(context, 0x14, 0x00); /* End = 0x0000ff. */
	write_reg(context, 0x15, 0x00);
	write_reg(context, 0x19, 0xff); /* Maximum playback delta. */
	write_reg(context, 0x1a, 0xff);
	write_reg(context, 0x1b, 0xff); /* Maximum volume. */
	write_reg(context, 0x10, 0x80); /* Start playback. */
}

static uint8_t read_reg(ym2610_context_t *context, uint8_t reg)
{
	YM2610ContextWrite(context, 0, reg);
	return YM2610ContextRead(context, 1);
}

static ym2610_context_t *alloc_context(void **storage_out)
{
	size_t size = YM2610ContextSize();
	size_t alignment = YM2610ContextAlignment();
	uintptr_t aligned;
	uint8_t *storage = malloc(size + alignment - 1u);
	if (!storage)
		return NULL;
	aligned = ((uintptr_t)storage + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
	*storage_out = storage;
	return (ym2610_context_t *)aligned;
}

int main(void)
{
	static uint8_t pcm_a[0x1000];
	static uint8_t pcm_b[0x1000];
	int32_t a_left[128], a_right[128];
	int32_t b_left[128], b_right[128];
	int32_t *a_buffer[2] = { a_left, a_right };
	int32_t *b_buffer[2] = { b_left, b_right };
	callback_state_t a_state = { 0 };
	callback_state_t b_state = { 0 };
	callback_state_t c_state = { 0 };
	void *a_storage = NULL;
	void *b_storage = NULL;
	void *c_storage = NULL;
	ym2610_context_t *a = alloc_context(&a_storage);
	ym2610_context_t *b = alloc_context(&b_storage);
	ym2610_context_t *c = alloc_context(&c_storage);
	int ok = 1;
	uint32_t i;

	if (!a || !b || !c)
	{
		fprintf(stderr, "YM2610 context allocation failed\n");
		free(a_storage);
		free(b_storage);
		free(c_storage);
		return 1;
	}
	for (i = 0; i < sizeof(pcm_a); i++)
		pcm_a[i] = (uint8_t)(i * 37u + 11u);
	for (i = 0; i < sizeof(pcm_b); i++)
		pcm_b[i] = (uint8_t)(i * 19u + 7u);

	YM2610ContextInit(a, 8000000, 44100, pcm_a, sizeof(pcm_a),
		pcm_b, sizeof(pcm_b), test_timer, test_irq, &a_state);
	YM2610ContextInit(b, 8000000, 44100, pcm_a, sizeof(pcm_a),
		pcm_b, sizeof(pcm_b), test_timer, test_irq, &b_state);

	write_reg(a, 0x08, 0x0f);
	if (read_reg(a, 0x08) != 0x0f || read_reg(b, 0x08) != 0x00)
	{
		fprintf(stderr, "SSG register state leaked between YM2610 contexts\n");
		ok = 0;
	}

	/* Load + enable Timer A only in context A. */
	write_reg(a, 0x24, 0xff);
	write_reg(a, 0x25, 0x03);
	write_reg(a, 0x27, 0x05);
	if (a_state.timer_calls == 0 || b_state.timer_calls != 0 ||
		a_state.last_timer_channel != 0 || a_state.last_timer_count != 1)
	{
		fprintf(stderr, "Timer A callback was not isolated\n");
		ok = 0;
	}

	YM2610ContextTimerOver(a, 0);
	if ((YM2610ContextRead(a, 0) & 0x01) == 0 ||
		(YM2610ContextRead(b, 0) & 0x03) != 0 ||
		a_state.irq_asserts != 1 || b_state.irq_asserts != 0)
	{
		fprintf(stderr, "Timer A IRQ/status leaked between YM2610 contexts\n");
		ok = 0;
	}

	/* Clear A, then independently exercise Timer B in B. */
	write_reg(a, 0x27, 0x10);
	write_reg(b, 0x26, 0xff);
	write_reg(b, 0x27, 0x0a);
	YM2610ContextTimerOver(b, 1);
	if ((YM2610ContextRead(a, 0) & 0x03) != 0 ||
		(YM2610ContextRead(b, 0) & 0x02) == 0 ||
		a_state.irq_clears != 1 || b_state.irq_asserts != 1)
	{
		fprintf(stderr, "Timer B IRQ/status was not isolated\n");
		ok = 0;
	}

	/* Rendering must use only the selected context. Configure identical SSG
	 * tones, verify bit-exact output, then change A alone and require the next
	 * render period to diverge. */
	YM2610ContextReset(a);
	YM2610ContextReset(b);
	write_reg(a, 0x00, 0x20);
	write_reg(b, 0x00, 0x20);
	write_reg(a, 0x01, 0x00);
	write_reg(b, 0x01, 0x00);
	write_reg(a, 0x07, 0x3e);
	write_reg(b, 0x07, 0x3e);
	write_reg(a, 0x08, 0x0f);
	write_reg(b, 0x08, 0x0f);
	YM2610ContextUpdate(a, a_buffer, 128);
	YM2610ContextUpdate(b, b_buffer, 128);
	if (memcmp(a_left, b_left, sizeof(a_left)) != 0 ||
		memcmp(a_right, b_right, sizeof(a_right)) != 0)
	{
		fprintf(stderr, "Equal YM2610 contexts rendered different PCM\n");
		ok = 0;
	}

	write_reg(a, 0x08, 0x00);
	YM2610ContextUpdate(a, a_buffer, 128);
	YM2610ContextUpdate(b, b_buffer, 128);
	if (memcmp(a_left, b_left, sizeof(a_left)) == 0 &&
		memcmp(a_right, b_right, sizeof(a_right)) == 0)
	{
		fprintf(stderr, "YM2610 render state leaked between contexts\n");
		ok = 0;
	}

#if (EMU_SYSTEM == MVS)
	{
		ym2610_pcm_window_t window;
		bool nonzero = false;

		/* A window-backed context must decode exactly the same ADPCM-A stream
		 * as a resident-ROM context without receiving decoder state from it. */
		YM2610ContextInit(a, 8000000, 44100, pcm_a, sizeof(pcm_a),
			pcm_b, sizeof(pcm_b), test_timer, test_irq, &a_state);
		YM2610ContextInit(b, 8000000, 44100, NULL, sizeof(pcm_a),
			NULL, sizeof(pcm_b), test_timer, test_irq, &b_state);
		YM2610ContextEnablePcmWindowSource(b, sizeof(pcm_a), sizeof(pcm_b));
		start_adpcma_channel_zero(a);
		start_adpcma_channel_zero(b);
		memset(&window, 0, sizeof(window));
		window.samples = 128;
		window.adpcma[0].base_byte = 0;
		window.adpcma[0].size = YM2610_PCM_WINDOW_SEGMENT_BYTES;
		memcpy(window.adpcma[0].data, pcm_a, YM2610_PCM_WINDOW_SEGMENT_BYTES);
		YM2610ContextUpdate(a, a_buffer, 128);
		if (!YM2610ContextUpdatePcmWindow(b, b_buffer, 128, &window) ||
			memcmp(a_left, b_left, sizeof(a_left)) != 0 ||
			memcmp(a_right, b_right, sizeof(a_right)) != 0)
		{
			fprintf(stderr, "Window-backed ADPCM-A render differs from resident PCM\n");
			ok = 0;
		}
		for (i = 0; i < 128; i++)
		{
			if (a_left[i] != 0 || a_right[i] != 0)
			{
				nonzero = true;
				break;
			}
		}
		if (!nonzero)
		{
			fprintf(stderr, "ADPCM-A window oracle produced only silence\n");
			ok = 0;
		}

		/* Missing source bytes must fail closed rather than dereferencing a
		 * null PCM pointer or silently advancing a different decoder state. */
		YM2610ContextInit(b, 8000000, 44100, NULL, sizeof(pcm_a),
			NULL, sizeof(pcm_b), test_timer, test_irq, &b_state);
		YM2610ContextEnablePcmWindowSource(b, sizeof(pcm_a), sizeof(pcm_b));
		start_adpcma_channel_zero(b);
		memset(&window, 0, sizeof(window));
		window.samples = 128;
		window.adpcma[0].base_byte = 0;
		window.adpcma[0].size = 1;
		window.adpcma[0].data[0] = pcm_a[0];
		if (YM2610ContextUpdatePcmWindow(b, b_buffer, 128, &window))
		{
			fprintf(stderr, "Truncated ADPCM-A window did not fail closed\n");
			ok = 0;
		}

		/* Delta-T/ADPCM-B uses the same window contract, including games where
		 * B aliases SOUND1. */
		YM2610ContextInit(a, 8000000, 44100, pcm_a, sizeof(pcm_a),
			pcm_b, sizeof(pcm_b), test_timer, test_irq, &a_state);
		YM2610ContextInit(b, 8000000, 44100, NULL, sizeof(pcm_a),
			NULL, sizeof(pcm_b), test_timer, test_irq, &b_state);
		YM2610ContextEnablePcmWindowSource(b, sizeof(pcm_a), sizeof(pcm_b));
		start_adpcmb(a);
		start_adpcmb(b);
		memset(&window, 0, sizeof(window));
		window.samples = 128;
		window.adpcmb_segment_count = 1;
		window.adpcmb[0].base_byte = 0;
		window.adpcmb[0].size = YM2610_PCM_WINDOW_SEGMENT_BYTES;
		memcpy(window.adpcmb[0].data, pcm_b, YM2610_PCM_WINDOW_SEGMENT_BYTES);
		YM2610ContextUpdate(a, a_buffer, 128);
		if (!YM2610ContextUpdatePcmWindow(b, b_buffer, 128, &window) ||
			memcmp(a_left, b_left, sizeof(a_left)) != 0 ||
			memcmp(a_right, b_right, sizeof(a_right)) != 0)
		{
			fprintf(stderr, "Window-backed ADPCM-B render differs from resident PCM\n");
			ok = 0;
		}

		/* C5 snapshots the live authoritative YM rather than reconstructing a
		 * fresh reset-equivalent chip. Rebinding must preserve every renderable
		 * state while replacing context-local/source pointers. Prove that the
		 * cloned context produces the same next PCM period as its live source. */
		{
			uint32_t channel;
			uint32_t segment;

			if (!YM2610ContextCloneForPcmWindow(b, a))
			{
				fprintf(stderr, "YM2610 live-state clone failed\n");
				ok = 0;
			}
			else if (!YM2610ContextPreparePcmWindow(b, 128, &window))
			{
				fprintf(stderr, "YM2610 live-state clone window prepare failed\n");
				ok = 0;
			}
			else
			{
				for (channel = 0; channel < YM2610_PCM_WINDOW_ADPCMA_CHANNELS;
					channel++)
				{
					ym2610_pcm_window_segment_t *part = &window.adpcma[channel];
					if (part->size != 0)
						memcpy(part->data, pcm_a + part->base_byte, part->size);
				}
				for (segment = 0; segment < window.adpcmb_segment_count; segment++)
				{
					ym2610_pcm_window_segment_t *part = &window.adpcmb[segment];
					if (part->size != 0)
						memcpy(part->data, pcm_b + part->base_byte, part->size);
				}
				YM2610ContextUpdate(a, a_buffer, 128);
				if (!YM2610ContextUpdatePcmWindow(b, b_buffer, 128, &window) ||
					memcmp(a_left, b_left, sizeof(a_left)) != 0 ||
					memcmp(a_right, b_right, sizeof(a_right)) != 0 ||
					YM2610ContextRead(a, 2) != YM2610ContextRead(b, 2))
				{
					fprintf(stderr, "YM2610 live-state clone rendered different PCM/status\n");
					ok = 0;
				}
			}

			/* Recovery takes a live window-backed ME context and restores its
			 * semantic state into a native CPU context without importing ME-local
			 * source pointers or callbacks. */
			YM2610ContextInit(c, 8000000, 44100, pcm_a, sizeof(pcm_a),
				pcm_b, sizeof(pcm_b), test_timer, test_irq, &c_state);
			write_reg(c, 0x08, 0x07); /* Deliberately diverge before restore. */
			if (!YM2610ContextRestoreFromPcmWindow(c, b) ||
				!YM2610ContextPreparePcmWindow(b, 128, &window))
			{
				fprintf(stderr, "YM2610 recovery restore failed\n");
				ok = 0;
			}
			else
			{
				for (channel = 0; channel < YM2610_PCM_WINDOW_ADPCMA_CHANNELS;
					channel++)
				{
					ym2610_pcm_window_segment_t *part = &window.adpcma[channel];
					if (part->size != 0)
						memcpy(part->data, pcm_a + part->base_byte, part->size);
				}
				for (segment = 0; segment < window.adpcmb_segment_count; segment++)
				{
					ym2610_pcm_window_segment_t *part = &window.adpcmb[segment];
					if (part->size != 0)
						memcpy(part->data, pcm_b + part->base_byte, part->size);
				}
				if (!YM2610ContextUpdatePcmWindow(b, b_buffer, 128, &window))
				{
					fprintf(stderr, "YM2610 recovered source render failed\n");
					ok = 0;
				}
				else
				{
					YM2610ContextUpdate(c, a_buffer, 128);
					if (memcmp(a_left, b_left, sizeof(a_left)) != 0 ||
						memcmp(a_right, b_right, sizeof(a_right)) != 0 ||
						YM2610ContextRead(c, 2) != YM2610ContextRead(b, 2))
					{
						fprintf(stderr,
							"YM2610 restored native context diverged from ME state\n");
						ok = 0;
					}
				}
			}
			{
				uint32_t timer_calls = c_state.timer_calls;

				write_reg(c, 0x24, 0xff);
				write_reg(c, 0x25, 0x03);
				write_reg(c, 0x27, 0x05);
				if (c_state.timer_calls <= timer_calls)
				{
					fprintf(stderr, "YM2610 restore lost CPU timer callback binding\n");
					ok = 0;
				}
			}
		}
	}
#endif

	free(a_storage);
	free(b_storage);
	free(c_storage);
	if (!ok)
		return 1;

	printf("YM2610 independent context control/timer/render oracle passed\n");
	return 0;
}
