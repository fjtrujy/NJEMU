#ifndef COMMON_NEOGEO_ME_SOUND_SHADOW_H
#define COMMON_NEOGEO_ME_SOUND_SHADOW_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/z80/cz80.h"

#ifdef PSP_ME_SOUND_COPROCESSOR
bool neogeo_me_sound_shadow_command(uint8_t command, uint64_t emulated_time);
void neogeo_me_sound_shadow_status_pending(void);
bool neogeo_me_sound_shadow_main_status(uint8_t sound_code, uint8_t pending_command,
	uint8_t result_code, uint8_t *presented_pending, uint8_t *presented_result);
bool neogeo_me_sound_shadow_z80_snapshot(const cz80_state_t *state,
	const uint8_t *visible_memory, const uint8_t *source_rom,
	uint32_t source_length, const uint32_t banks[4], uint8_t sound_code,
	uint8_t pending_command, uint8_t result_code, uint32_t pcm_a_size,
	uint32_t pcm_b_size);
bool neogeo_me_sound_shadow_prepare_cpu_state(void);
bool neogeo_me_sound_shadow_state_resume_requested(void);
void neogeo_me_sound_shadow_scheduler_boundary(void);
bool neogeo_me_sound_shadow_z80_cpu_suppressed(void);
bool neogeo_me_sound_shadow_z80_slice_begin(uint64_t horizon_time,
	uint32_t scheduler_time_left);
void neogeo_me_sound_shadow_z80_io_read(uint16_t port, uint8_t value);
void neogeo_me_sound_shadow_z80_io_write(uint16_t port, uint8_t value);
void neogeo_me_sound_shadow_z80_preempt(uint32_t timer_channel);
void neogeo_me_sound_shadow_z80_irq(int32_t state, uint64_t emulated_time);
void neogeo_me_sound_shadow_ym_timer(uint32_t channel, uint64_t emulated_time);
void neogeo_me_sound_shadow_ym_timer_completed(void);
bool neogeo_me_sound_shadow_ym_render_begin(uint32_t samples, uint64_t emulated_time);
bool neogeo_me_sound_shadow_ym_authoritative(void);
bool neogeo_me_sound_shadow_authoritative(void);
bool neogeo_me_sound_shadow_ym_render_completed_authoritative(int32_t **buffer,
	uint32_t samples);
void neogeo_me_sound_shadow_ym_render_completed(int32_t **buffer, uint32_t samples,
	uint8_t status_b);
void neogeo_me_sound_shadow_z80_slice_completed(uint64_t emulated_time);
bool neogeo_me_sound_shadow_checkpoint_due(void);
void neogeo_me_sound_shadow_frame_completed(uint64_t emulated_time,
	uint8_t sound_code, uint8_t pending_command, uint8_t result_code,
	const cz80_state_t *expected_state, const uint32_t banks[4],
	const uint8_t *visible_memory);
#else
static inline bool neogeo_me_sound_shadow_command(uint8_t command,
	uint64_t emulated_time)
{
	(void)command;
	(void)emulated_time;
	return true;
}

static inline bool neogeo_me_sound_shadow_z80_snapshot(const cz80_state_t *state,
	const uint8_t *visible_memory, const uint8_t *source_rom,
	uint32_t source_length, const uint32_t banks[4], uint8_t sound_code,
	uint8_t pending_command, uint8_t result_code, uint32_t pcm_a_size,
	uint32_t pcm_b_size)
{
	(void)state;
	(void)visible_memory;
	(void)source_rom;
	(void)source_length;
	(void)banks;
	(void)sound_code;
	(void)pending_command;
	(void)result_code;
	(void)pcm_a_size;
	(void)pcm_b_size;
	return true;
}

static inline bool neogeo_me_sound_shadow_prepare_cpu_state(void)
{
	return true;
}

static inline bool neogeo_me_sound_shadow_state_resume_requested(void)
{
	return false;
}

static inline void neogeo_me_sound_shadow_scheduler_boundary(void)
{
}

static inline bool neogeo_me_sound_shadow_z80_cpu_suppressed(void)
{
	return false;
}

static inline bool neogeo_me_sound_shadow_z80_slice_begin(uint64_t horizon_time,
	uint32_t scheduler_time_left)
{
	(void)horizon_time;
	(void)scheduler_time_left;
	return false;
}

static inline void neogeo_me_sound_shadow_z80_io_read(uint16_t port, uint8_t value)
{
	(void)port;
	(void)value;
}

static inline void neogeo_me_sound_shadow_z80_io_write(uint16_t port, uint8_t value)
{
	(void)port;
	(void)value;
}

static inline void neogeo_me_sound_shadow_z80_preempt(uint32_t timer_channel)
{
	(void)timer_channel;
}

static inline void neogeo_me_sound_shadow_z80_irq(int32_t state, uint64_t emulated_time)
{
	(void)state;
	(void)emulated_time;
}

static inline void neogeo_me_sound_shadow_ym_timer(uint32_t channel,
	uint64_t emulated_time)
{
	(void)channel;
	(void)emulated_time;
}

static inline void neogeo_me_sound_shadow_ym_timer_completed(void)
{
}

static inline bool neogeo_me_sound_shadow_ym_render_begin(uint32_t samples,
	uint64_t emulated_time)
{
	(void)samples;
	(void)emulated_time;
	return false;
}

static inline bool neogeo_me_sound_shadow_ym_authoritative(void)
{
	return false;
}

static inline bool neogeo_me_sound_shadow_authoritative(void)
{
	return false;
}

static inline bool neogeo_me_sound_shadow_ym_render_completed_authoritative(
	int32_t **buffer, uint32_t samples)
{
	(void)buffer;
	(void)samples;
	return false;
}

static inline void neogeo_me_sound_shadow_status_pending(void)
{
}

static inline bool neogeo_me_sound_shadow_main_status(uint8_t sound_code,
	uint8_t pending_command, uint8_t result_code, uint8_t *presented_pending,
	uint8_t *presented_result)
{
	(void)sound_code;
	(void)pending_command;
	(void)result_code;
	(void)presented_pending;
	(void)presented_result;
	return false;
}

static inline void neogeo_me_sound_shadow_ym_render_completed(int32_t **buffer,
	uint32_t samples, uint8_t status_b)
{
	(void)buffer;
	(void)samples;
	(void)status_b;
}

static inline void neogeo_me_sound_shadow_z80_slice_completed(uint64_t emulated_time)
{
	(void)emulated_time;
}

static inline bool neogeo_me_sound_shadow_checkpoint_due(void)
{
	return false;
}

static inline void neogeo_me_sound_shadow_frame_completed(uint64_t emulated_time,
	uint8_t sound_code, uint8_t pending_command, uint8_t result_code,
	const cz80_state_t *expected_state, const uint32_t banks[4],
	const uint8_t *visible_memory)
{
	(void)emulated_time;
	(void)sound_code;
	(void)pending_command;
	(void)result_code;
	(void)expected_state;
	(void)banks;
	(void)visible_memory;
}
#endif

#endif /* COMMON_NEOGEO_ME_SOUND_SHADOW_H */
