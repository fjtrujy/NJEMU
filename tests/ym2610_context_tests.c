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
	void *a_storage = NULL;
	void *b_storage = NULL;
	ym2610_context_t *a = alloc_context(&a_storage);
	ym2610_context_t *b = alloc_context(&b_storage);
	int ok = 1;

	if (!a || !b)
	{
		fprintf(stderr, "YM2610 context allocation failed\n");
		free(a_storage);
		free(b_storage);
		return 1;
	}

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

	free(a_storage);
	free(b_storage);
	if (!ok)
		return 1;

	printf("YM2610 independent context control/timer/render oracle passed\n");
	return 0;
}
