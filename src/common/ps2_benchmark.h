#ifndef COMMON_PS2_BENCHMARK_H
#define COMMON_PS2_BENCHMARK_H

#include <stdint.h>

/* Available only in explicitly enabled PS2 development benchmark builds. */
void ps2_benchmark_reset(void);
void ps2_benchmark_apply_options(void);
void ps2_benchmark_frame_begin(void);
void ps2_benchmark_cpu_begin(void);
void ps2_benchmark_cpu_end(void);
void ps2_benchmark_render_begin(void);
void ps2_benchmark_render_end(void);
void ps2_benchmark_frame_end(void);
void ps2_benchmark_report(void);

#endif /* COMMON_PS2_BENCHMARK_H */
