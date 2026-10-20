#ifndef NJEMU_HOST_PS2_BENCHMARK_RESULT_H
#define NJEMU_HOST_PS2_BENCHMARK_RESULT_H

#include <stddef.h>
#include <stdint.h>

#define PS2_BENCHMARK_MAX_TRIALS 256
#define PS2_BENCHMARK_MAX_ROM 24

typedef struct ps2_benchmark_result {
    char core[8];
    char rom[PS2_BENCHMARK_MAX_ROM];
    char compiler[8];
    char scenario[24];
    char state[24];
    char state_hash[33];
    char cache[12];
    char sound[12];
    char resident[12];
    char status[24];
    char revision[48];
    char compiler_version[24];
    uint64_t total_us;
    uint64_t mean_us;
    uint64_t sample_sum_us;
    uint64_t io_bytes;
    uint64_t core_update_us;
    uint64_t screen_update_us;
    uint32_t warmup;
    uint32_t frames;
    uint32_t expected;
    uint32_t min_us;
    uint32_t max_us;
    uint32_t p50_us;
    uint32_t p95_us;
    uint32_t p99_us;
    uint32_t over_60hz;
    uint32_t io_ops;
    uint32_t ticker_pair_ns;
    uint32_t cpu_bytes;
    uint32_t gfx_bytes;
    uint32_t pcm_bytes;
} ps2_benchmark_result_t;

/* Parses exactly one structured line from a possibly noisy PS2Link transcript. */
int ps2_benchmark_parse(const char *text, ps2_benchmark_result_t *result,
    char *error, size_t error_size);
int ps2_benchmark_result_accepted(const ps2_benchmark_result_t *result);
uint64_t ps2_benchmark_median(const uint64_t *values, size_t count);

#endif
