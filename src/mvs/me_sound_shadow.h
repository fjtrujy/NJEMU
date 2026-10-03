#ifndef MVS_ME_SOUND_SHADOW_H
#define MVS_ME_SOUND_SHADOW_H

#include <stdbool.h>
#include <stdint.h>

#include "cpu/z80/cz80.h"

#ifdef PSP_ME_SOUND_COPROCESSOR
bool mvs_me_sound_shadow_command(uint8_t command, uint64_t emulated_time);
bool mvs_me_sound_shadow_z80_snapshot(const cz80_state_t *state,
	const uint8_t *visible_memory, const uint8_t *source_rom,
	uint32_t source_length, const uint32_t banks[4], uint8_t sound_code,
	uint8_t pending_command, uint8_t result_code);
void mvs_me_sound_shadow_z80_slice_begin(void);
void mvs_me_sound_shadow_z80_io_read(uint16_t port, uint8_t value);
void mvs_me_sound_shadow_z80_io_write(uint16_t port, uint8_t value);
void mvs_me_sound_shadow_z80_irq(int32_t state, uint64_t emulated_time);
void mvs_me_sound_shadow_z80_slice_completed(uint32_t cycles,
	uint64_t emulated_time, const cz80_state_t *expected_state,
	const uint32_t banks[4], const uint8_t *visible_memory);
void mvs_me_sound_shadow_frame_completed(uint64_t emulated_time);
#else
static inline bool mvs_me_sound_shadow_command(uint8_t command,
	uint64_t emulated_time)
{
	(void)command;
	(void)emulated_time;
	return true;
}

static inline bool mvs_me_sound_shadow_z80_snapshot(const cz80_state_t *state,
	const uint8_t *visible_memory, const uint8_t *source_rom,
	uint32_t source_length, const uint32_t banks[4], uint8_t sound_code,
	uint8_t pending_command, uint8_t result_code)
{
	(void)state;
	(void)visible_memory;
	(void)source_rom;
	(void)source_length;
	(void)banks;
	(void)sound_code;
	(void)pending_command;
	(void)result_code;
	return true;
}

static inline void mvs_me_sound_shadow_z80_slice_begin(void)
{
}

static inline void mvs_me_sound_shadow_z80_io_read(uint16_t port, uint8_t value)
{
	(void)port;
	(void)value;
}

static inline void mvs_me_sound_shadow_z80_io_write(uint16_t port, uint8_t value)
{
	(void)port;
	(void)value;
}

static inline void mvs_me_sound_shadow_z80_irq(int32_t state, uint64_t emulated_time)
{
	(void)state;
	(void)emulated_time;
}

static inline void mvs_me_sound_shadow_z80_slice_completed(uint32_t cycles,
	uint64_t emulated_time, const cz80_state_t *expected_state,
	const uint32_t banks[4], const uint8_t *visible_memory)
{
	(void)cycles;
	(void)emulated_time;
	(void)expected_state;
	(void)banks;
	(void)visible_memory;
}

static inline void mvs_me_sound_shadow_frame_completed(uint64_t emulated_time)
{
	(void)emulated_time;
}
#endif

#endif /* MVS_ME_SOUND_SHADOW_H */
