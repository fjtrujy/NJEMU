#ifndef CPS2_ME_SOUND_SHADOW_H
#define CPS2_ME_SOUND_SHADOW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef PSP_ME_SOUND_COPROCESSOR
bool cps2_me_sound_snapshot_from_cpu(void);
bool cps2_me_sound_prepare_cpu_state(void);
bool cps2_me_sound_resume_from_cpu(void);
bool cps2_me_sound_z80_cpu_suppressed(void);
bool cps2_me_sound_main_slice_begin(void);
bool cps2_me_sound_main_shared_ram_access(bool write);
bool cps2_me_sound_main_slice_finish(uint32_t cycles, uint64_t emulated_time,
	bool run_z80);
bool cps2_me_sound_advance(uint32_t cycles, uint64_t emulated_time);
bool cps2_me_sound_irq(int32_t state, uint64_t emulated_time);
bool cps2_me_sound_z80_reset_line(int state, uint64_t emulated_time);
bool cps2_me_sound_memory_read(uint32_t offset, uint8_t *data, uint32_t size);
bool cps2_me_sound_memory_write_byte(uint32_t offset, uint8_t data);
bool cps2_me_sound_render(int32_t **buffer, uint32_t samples);
void cps2_me_sound_frame_completed(void);
#else
static inline bool cps2_me_sound_snapshot_from_cpu(void)
{
	return true;
}

static inline bool cps2_me_sound_prepare_cpu_state(void)
{
	return true;
}

static inline bool cps2_me_sound_resume_from_cpu(void)
{
	return true;
}

static inline bool cps2_me_sound_z80_cpu_suppressed(void)
{
	return false;
}

static inline bool cps2_me_sound_main_slice_begin(void)
{
	return false;
}

static inline bool cps2_me_sound_main_shared_ram_access(bool write)
{
	(void)write;
	return true;
}

static inline bool cps2_me_sound_main_slice_finish(uint32_t cycles,
	uint64_t emulated_time, bool run_z80)
{
	(void)cycles;
	(void)emulated_time;
	(void)run_z80;
	return false;
}

static inline bool cps2_me_sound_advance(uint32_t cycles, uint64_t emulated_time)
{
	(void)cycles;
	(void)emulated_time;
	return false;
}

static inline bool cps2_me_sound_irq(int32_t state, uint64_t emulated_time)
{
	(void)state;
	(void)emulated_time;
	return false;
}

static inline bool cps2_me_sound_z80_reset_line(int state, uint64_t emulated_time)
{
	(void)state;
	(void)emulated_time;
	return false;
}

static inline bool cps2_me_sound_memory_read(uint32_t offset, uint8_t *data,
	uint32_t size)
{
	(void)offset;
	(void)data;
	(void)size;
	return false;
}

static inline bool cps2_me_sound_memory_write_byte(uint32_t offset, uint8_t data)
{
	(void)offset;
	(void)data;
	return false;
}

static inline bool cps2_me_sound_render(int32_t **buffer, uint32_t samples)
{
	(void)buffer;
	(void)samples;
	return false;
}

static inline void cps2_me_sound_frame_completed(void)
{
}
#endif

#endif /* CPS2_ME_SOUND_SHADOW_H */
