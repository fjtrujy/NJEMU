#ifndef CPS2_SOUND_OFFLOAD_H
#define CPS2_SOUND_OFFLOAD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef NJEMU_SOUND_OFFLOAD
bool cps2_sound_offload_snapshot_from_cpu(void);
bool cps2_sound_offload_prepare_cpu_state(void);
bool cps2_sound_offload_resume_from_cpu(void);
bool cps2_sound_offload_z80_cpu_suppressed(void);
bool cps2_sound_offload_main_slice_begin(void);
bool cps2_sound_offload_main_shared_ram_access(bool write);
bool cps2_sound_offload_main_slice_finish(uint32_t cycles, uint64_t emulated_time,
	bool run_z80);
bool cps2_sound_offload_advance(uint32_t cycles, uint64_t emulated_time);
bool cps2_sound_offload_irq(int32_t state, uint64_t emulated_time);
bool cps2_sound_offload_z80_reset_line(int state, uint64_t emulated_time);
bool cps2_sound_offload_memory_read(uint32_t offset, uint8_t *data, uint32_t size);
bool cps2_sound_offload_memory_write_byte(uint32_t offset, uint8_t data);
bool cps2_sound_offload_render(int32_t **buffer, uint32_t samples);
void cps2_sound_offload_frame_completed(void);
#else
static inline bool cps2_sound_offload_snapshot_from_cpu(void)
{
	return true;
}

static inline bool cps2_sound_offload_prepare_cpu_state(void)
{
	return true;
}

static inline bool cps2_sound_offload_resume_from_cpu(void)
{
	return true;
}

static inline bool cps2_sound_offload_z80_cpu_suppressed(void)
{
	return false;
}

static inline bool cps2_sound_offload_main_slice_begin(void)
{
	return false;
}

static inline bool cps2_sound_offload_main_shared_ram_access(bool write)
{
	(void)write;
	return true;
}

static inline bool cps2_sound_offload_main_slice_finish(uint32_t cycles,
	uint64_t emulated_time, bool run_z80)
{
	(void)cycles;
	(void)emulated_time;
	(void)run_z80;
	return false;
}

static inline bool cps2_sound_offload_advance(uint32_t cycles, uint64_t emulated_time)
{
	(void)cycles;
	(void)emulated_time;
	return false;
}

static inline bool cps2_sound_offload_irq(int32_t state, uint64_t emulated_time)
{
	(void)state;
	(void)emulated_time;
	return false;
}

static inline bool cps2_sound_offload_z80_reset_line(int state, uint64_t emulated_time)
{
	(void)state;
	(void)emulated_time;
	return false;
}

static inline bool cps2_sound_offload_memory_read(uint32_t offset, uint8_t *data,
	uint32_t size)
{
	(void)offset;
	(void)data;
	(void)size;
	return false;
}

static inline bool cps2_sound_offload_memory_write_byte(uint32_t offset, uint8_t data)
{
	(void)offset;
	(void)data;
	return false;
}

static inline bool cps2_sound_offload_render(int32_t **buffer, uint32_t samples)
{
	(void)buffer;
	(void)samples;
	return false;
}

static inline void cps2_sound_offload_frame_completed(void)
{
}
#endif

#endif /* CPS2_SOUND_OFFLOAD_H */
