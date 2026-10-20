#include "common/ps2_benchmark.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/emulator_options.h"
#include "common/emulator_runtime.h"
#include "common/runtime_paths.h"
#include "common/ticker_driver.h"
#include "cpu/m68000/m68000.h"
#include "cpu/z80/z80.h"
#if EMU_SYSTEM == CPS1
#include "cps1/memintrf.h"
#elif EMU_SYSTEM == CPS2
#include "cps2/memintrf.h"
#elif EMU_SYSTEM == MVS
#include "mvs/memintrf.h"
#endif

#ifndef PS2_BENCHMARK_FRAMES
#define PS2_BENCHMARK_FRAMES 10000
#endif
#ifndef PS2_BENCHMARK_WARMUP
#define PS2_BENCHMARK_WARMUP 500
#endif
#ifndef PS2_BENCHMARK_NJEMU_COMMIT
#define PS2_BENCHMARK_NJEMU_COMMIT "unknown"
#endif
#ifndef PS2_BENCHMARK_CC_VERSION
#define PS2_BENCHMARK_CC_VERSION "unknown"
#endif
#ifndef PS2_BENCHMARK_WORKTREE_SHA256
#define PS2_BENCHMARK_WORKTREE_SHA256 "unknown"
#endif

#if !defined(PS2) || !defined(PS2_COMPILER_BENCHMARK) || USE_CACHE
#error "PS2 benchmark requires PS2, PS2_COMPILER_BENCHMARK, and USE_CACHE=OFF"
#endif

static uint32_t samples_us[PS2_BENCHMARK_FRAMES];
static uint32_t warmup_completed;
static uint32_t measured_completed;
static uint64_t frame_begin_us;
static uint64_t start_us;
static uint64_t stop_us;
static volatile unsigned io_operations;
static volatile uint64_t io_read_bytes;
static volatile int measuring;
static bool invalid_sound;
static bool invalid_residency;
static bool residency_checked;
static uint32_t resident_cpu_bytes;
static uint32_t resident_gfx_bytes;
static uint32_t resident_pcm_bytes;
static uint64_t state_fingerprint;
static uint64_t m68k_fingerprint;
static uint64_t z80_fingerprint;
static uint64_t ram_fingerprint;
static uint64_t configuration_fingerprint;
static uint64_t cpu_start_us;
static uint64_t render_start_us;
static uint64_t cpu_total_us;
static uint64_t render_total_us;
static uint32_t ticker_pair_ns;

static void hash_bytes(uint64_t *hash, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t i;
    for (i = 0; i < length; ++i) {
        *hash ^= bytes[i];
        *hash *= UINT64_C(1099511628211);
    }
}

static void hash_register(uint64_t *hash, uint32_t value)
{
    uint8_t encoded[4];
    unsigned i;
    for (i = 0; i < sizeof(encoded); ++i)
        encoded[i] = (uint8_t)(value >> (8u * i));
    hash_bytes(hash, encoded, sizeof(encoded));
}

/* Diagnostic end-state fingerprint. Capture only AFTER the last timed
 * timestamp and BEFORE the game's normal memory/CPU teardown. This hashes
 * selected mutable RAM and CPU registers, not all chip/IO state, so it cannot
 * by itself certify equivalent sound/video execution. */
static void capture_state(void)
{
    static const int m68k_regs[] = {
        M68K_PC, M68K_SR, M68K_USP, M68K_MSP,
        M68K_D0, M68K_D1, M68K_D2, M68K_D3,
        M68K_D4, M68K_D5, M68K_D6, M68K_D7,
        M68K_A0, M68K_A1, M68K_A2, M68K_A3,
        M68K_A4, M68K_A5, M68K_A6, M68K_A7
    };
    static const int z80_regs[] = {
        Z80_PC, Z80_SP, Z80_AF, Z80_BC, Z80_DE, Z80_HL,
        Z80_IX, Z80_IY, Z80_AF2, Z80_BC2, Z80_DE2, Z80_HL2,
        Z80_R, Z80_I, Z80_IM, Z80_IFF1, Z80_IFF2, Z80_HALT
    };
    uint64_t hash = UINT64_C(14695981039346656037);
    uint64_t m68k_hash = UINT64_C(14695981039346656037);
    uint64_t z80_hash = UINT64_C(14695981039346656037);
    uint64_t ram_hash = UINT64_C(14695981039346656037);
    unsigned i;
    for (i = 0; i < sizeof(m68k_regs) / sizeof(m68k_regs[0]); ++i) {
        uint32_t value = m68000_get_reg(m68k_regs[i]);
        hash_register(&hash, value);
        hash_register(&m68k_hash, value);
    }
    for (i = 0; i < sizeof(z80_regs) / sizeof(z80_regs[0]); ++i) {
        uint32_t value = z80_get_reg(z80_regs[i]);
        hash_register(&hash, value);
        hash_register(&z80_hash, value);
    }
#if EMU_SYSTEM == CPS1
    hash_bytes(&hash, cps1_ram, sizeof(cps1_ram));
    hash_bytes(&ram_hash, cps1_ram, sizeof(cps1_ram));
    hash_bytes(&hash, cps1_gfxram, sizeof(cps1_gfxram));
    hash_bytes(&ram_hash, cps1_gfxram, sizeof(cps1_gfxram));
    hash_bytes(&hash, cps1_output, sizeof(cps1_output));
    hash_bytes(&ram_hash, cps1_output, sizeof(cps1_output));
#elif EMU_SYSTEM == CPS2
    hash_bytes(&hash, cps1_ram, sizeof(cps1_ram));
    hash_bytes(&ram_hash, cps1_ram, sizeof(cps1_ram));
    hash_bytes(&hash, cps2_ram, sizeof(cps2_ram));
    hash_bytes(&ram_hash, cps2_ram, sizeof(cps2_ram));
    hash_bytes(&hash, cps1_gfxram, sizeof(cps1_gfxram));
    hash_bytes(&ram_hash, cps1_gfxram, sizeof(cps1_gfxram));
    hash_bytes(&hash, cps2_objram, sizeof(cps2_objram));
    hash_bytes(&ram_hash, cps2_objram, sizeof(cps2_objram));
    hash_bytes(&hash, cps2_output, sizeof(cps2_output));
    hash_bytes(&ram_hash, cps2_output, sizeof(cps2_output));
#elif EMU_SYSTEM == MVS
    hash_bytes(&hash, neogeo_ram, sizeof(neogeo_ram));
    hash_bytes(&ram_hash, neogeo_ram, sizeof(neogeo_ram));
    hash_bytes(&hash, neogeo_sram16, sizeof(neogeo_sram16));
    hash_bytes(&ram_hash, neogeo_sram16, sizeof(neogeo_sram16));
    hash_bytes(&hash, neogeo_memcard, sizeof(neogeo_memcard));
    hash_bytes(&ram_hash, neogeo_memcard, sizeof(neogeo_memcard));
    if (memory_region_user3 != NULL) {
        hash_bytes(&hash, memory_region_user3, memory_length_user3);
        hash_bytes(&ram_hash, memory_region_user3, memory_length_user3);
    }
#endif
    state_fingerprint = hash;
    m68k_fingerprint = m68k_hash;
    z80_fingerprint = z80_hash;
    ram_fingerprint = ram_hash;
}

static void note_io(uint64_t bytes)
{
    ++io_operations;
    io_read_bytes += bytes;
}

/* Measurement is deliberately independent of ROM/configuration loading. */
void ps2_benchmark_reset(void)
{
    warmup_completed = 0;
    measured_completed = 0;
    frame_begin_us = 0;
    start_us = 0;
    stop_us = 0;
    io_operations = 0;
    io_read_bytes = 0;
    measuring = 0;
    invalid_sound = false;
    invalid_residency = false;
    residency_checked = false;
    resident_cpu_bytes = 0;
    resident_gfx_bytes = 0;
    resident_pcm_bytes = 0;
    state_fingerprint = 0;
    m68k_fingerprint = 0;
    z80_fingerprint = 0;
    ram_fingerprint = 0;
    configuration_fingerprint = 0;
    cpu_start_us = render_start_us = 0;
    cpu_total_us = render_total_us = 0;
    ticker_pair_ns = 0;
}

void ps2_benchmark_apply_options(void)
{
    /* load_gamecfg() overwrites these values; apply immediately afterward. */
    option_vsync = 0;
    option_speedlimit = 0;
    option_autoframeskip = 0;
    option_frameskip = 0;
    option_showfps = 0;
    option_sound_enable = 1;
}

void ps2_benchmark_frame_begin(void)
{
    if (!residency_checked) {
        uint64_t begin_calibration;
        uint64_t end_calibration;
        volatile uint64_t sink = 0;
        unsigned sample;
        begin_calibration = ticker_driver->currentUs(ticker_data);
        for (sample = 0; sample < 4096; ++sample) {
            uint64_t a = ticker_driver->currentUs(ticker_data);
            uint64_t b = ticker_driver->currentUs(ticker_data);
            sink += b - a;
        }
        end_calibration = ticker_driver->currentUs(ticker_data);
        (void)sink;
        if (end_calibration >= begin_calibration)
            ticker_pair_ns = (uint32_t)(
                ((end_calibration - begin_calibration) * 1000u) / 4096u);
        configuration_fingerprint = UINT64_C(14695981039346656037);
        hash_register(&configuration_fingerprint, (uint32_t)option_sound_enable);
        hash_register(&configuration_fingerprint, (uint32_t)option_samplerate);
        hash_register(&configuration_fingerprint, (uint32_t)option_sound_volume);
        hash_register(&configuration_fingerprint, (uint32_t)option_vsync);
        hash_register(&configuration_fingerprint, (uint32_t)option_speedlimit);
        hash_register(&configuration_fingerprint, (uint32_t)option_autoframeskip);
        hash_register(&configuration_fingerprint, (uint32_t)option_frameskip);
        hash_register(&configuration_fingerprint, (uint32_t)option_video_output_mode);
        hash_register(&configuration_fingerprint, (uint32_t)option_display_mode);
        hash_register(&configuration_fingerprint, (uint32_t)option_video_backend);
        hash_register(&configuration_fingerprint, (uint32_t)option_video_offset_x);
        hash_register(&configuration_fingerprint, (uint32_t)option_video_offset_y);
        hash_register(&configuration_fingerprint, (uint32_t)machine_sound_type);
        residency_checked = true;
        resident_cpu_bytes = memory_length_cpu1;
        resident_pcm_bytes = memory_length_sound1;
#if EMU_SYSTEM == MVS
        resident_gfx_bytes = memory_length_gfx3;
        invalid_residency = !memory_region_cpu1 || !memory_region_gfx3 ||
            (memory_length_sound1 && !memory_region_sound1);
        if (disable_sound)
            invalid_sound = true;
#else
        resident_gfx_bytes = memory_length_gfx1;
        invalid_residency = !memory_region_cpu1 || !memory_region_gfx1 ||
            (memory_length_sound1 && !memory_region_sound1);
#endif
    }
    if (warmup_completed < PS2_BENCHMARK_WARMUP ||
        measured_completed >= PS2_BENCHMARK_FRAMES)
        return;

    /* Check a runtime downgrade to sound-off, e.g. a failed MVS PCM load. */
    if (!option_sound_enable)
        invalid_sound = true;
#if EMU_SYSTEM == MVS
    if (disable_sound)
        invalid_sound = true;
#endif

    measuring = 1;
    frame_begin_us = ticker_driver->currentUs(ticker_data);
    if (measured_completed == 0)
        start_us = frame_begin_us;
}

void ps2_benchmark_cpu_begin(void)
{
    if (measuring)
        cpu_start_us = ticker_driver->currentUs(ticker_data);
}

void ps2_benchmark_cpu_end(void)
{
    if (measuring) {
        uint64_t now = ticker_driver->currentUs(ticker_data);
        if (now >= cpu_start_us)
            cpu_total_us += now - cpu_start_us;
    }
}

void ps2_benchmark_render_begin(void)
{
    if (measuring)
        render_start_us = ticker_driver->currentUs(ticker_data);
}

void ps2_benchmark_render_end(void)
{
    if (measuring) {
        uint64_t now = ticker_driver->currentUs(ticker_data);
        if (now >= render_start_us)
            render_total_us += now - render_start_us;
    }
}

void ps2_benchmark_frame_end(void)
{
    uint64_t now;
    uint64_t delta;

    if (warmup_completed < PS2_BENCHMARK_WARMUP) {
        ++warmup_completed;
        return;
    }
    if (measured_completed >= PS2_BENCHMARK_FRAMES)
        return;

    now = ticker_driver->currentUs(ticker_data);
    delta = now >= frame_begin_us ? now - frame_begin_us : 0;
    samples_us[measured_completed] =
        delta > UINT32_MAX ? UINT32_MAX : (uint32_t)delta;
    ++measured_completed;
    if (measured_completed == PS2_BENCHMARK_FRAMES) {
        stop_us = now;
        measuring = 0;
        capture_state();
        Loop = LOOP_EXIT;
    }
}

static int compare_u32(const void *a, const void *b)
{
    uint32_t left = *(const uint32_t *)a;
    uint32_t right = *(const uint32_t *)b;
    return (left > right) - (left < right);
}

/* Nearest-rank percentile on a sorted complete sample set. */
static uint32_t percentile(unsigned percentage)
{
    size_t rank = ((size_t)percentage * measured_completed + 99u) / 100u;
    if (rank == 0)
        rank = 1;
    return samples_us[rank - 1];
}

void ps2_benchmark_report(void)
{
    uint32_t minimum = UINT32_MAX;
    uint32_t maximum = 0;
    uint32_t over_budget = 0;
    uint64_t sample_sum = 0;
    uint64_t duration = stop_us >= start_us ? stop_us - start_us : 0;
    uint32_t p50 = 0, p95 = 0, p99 = 0;
    unsigned i;
    const char *status;

    for (i = 0; i < measured_completed; ++i) {
        uint32_t value = samples_us[i];
        if (value < minimum) minimum = value;
        if (value > maximum) maximum = value;
        if ((uint64_t)value * 60u > 1000000u) ++over_budget;
        sample_sum += value;
    }
    if (measured_completed > 0) {
        qsort(samples_us, measured_completed, sizeof(samples_us[0]), compare_u32);
        p50 = percentile(50);
        p95 = percentile(95);
        p99 = percentile(99);
    } else {
        minimum = 0;
    }

    if (measured_completed != PS2_BENCHMARK_FRAMES || duration == 0)
        status = "failed";
    else if (io_operations != 0 || invalid_sound || invalid_residency)
        status = "rejected";
    else
        /* No state/replay oracle yet: never label the trial comparable. */
        status = "unverified";

    /* Diagnostic only: the split fingerprints help identify a varying CPU,
     * sound co-processor or RAM region between same-compiler trials. They do
     * not turn an end-state snapshot into a deterministic replay oracle. */
    printf("NJEMU_BENCHMARK_STATE_PARTS_V1 m68k=%016" PRIx64
           " z80=%016" PRIx64 " ram=%016" PRIx64 "\n",
           m68k_fingerprint, z80_fingerprint, ram_fingerprint);
    printf("NJEMU_BENCHMARK_V1 core=%s rom=%s compiler=%s "
           "warmup=%u frames=%u expected=%u total_us=%" PRIu64 " "
           "sample_sum_us=%" PRIu64 " mean_us=%" PRIu64 " "
           "min_us=%u max_us=%u p50_us=%u p95_us=%u p99_us=%u "
           "over_60hz=%u io_ops=%u io_bytes=%" PRIu64 " "
           "cpu_bytes=%u gfx_bytes=%u pcm_bytes=%u resident=%s "
           "core_update_us=%" PRIu64 " screen_update_us=%" PRIu64 " ticker_pair_ns=%u "
           "cache=off sound=%s scenario=attract state=unverified "
           "state_hash=%016" PRIx64 " config_hash=%016" PRIx64
           " revision=%s worktree=%s cc_version=%s status=%s\n",
           TARGET_STR, game_name,
#if defined(__clang__)
           "LLVM",
#else
           "GCC",
#endif
           (unsigned)warmup_completed, (unsigned)measured_completed,
           (unsigned)PS2_BENCHMARK_FRAMES, duration, sample_sum,
           measured_completed ? sample_sum / measured_completed : 0,
           (unsigned)minimum, (unsigned)maximum, (unsigned)p50,
           (unsigned)p95, (unsigned)p99, (unsigned)over_budget,
           io_operations, io_read_bytes,
           (unsigned)resident_cpu_bytes,
           (unsigned)resident_gfx_bytes,
           (unsigned)resident_pcm_bytes,
           residency_checked && !invalid_residency ? "on" : "off",
           cpu_total_us, render_total_us, (unsigned)ticker_pair_ns,
           invalid_sound ? "off" : "on", state_fingerprint,
           configuration_fingerprint, PS2_BENCHMARK_NJEMU_COMMIT,
           PS2_BENCHMARK_WORKTREE_SHA256, PS2_BENCHMARK_CC_VERSION, status);
}

/*
 * GNU ld --wrap hooks cover direct POSIX/stdio operations issued by linked
 * objects. They are intentionally conservative: even a metadata operation
 * invalidates a benchmark interval. PS2-specific direct IOP RPC calls still
 * require a separate boundary audit before declaring zero-I/O proven.
 */
#include <fcntl.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>

extern int __real_open(const char *, int, ...);
extern int __real_close(int);
extern ssize_t __real_read(int, void *, size_t);
extern ssize_t __real_write(int, const void *, size_t);
extern off_t __real_lseek(int, off_t, int);
extern FILE *__real_fopen(const char *, const char *);
extern int __real_fclose(FILE *);
extern size_t __real_fread(void *, size_t, size_t, FILE *);
extern size_t __real_fwrite(const void *, size_t, size_t, FILE *);
extern int __real_fseek(FILE *, long, int);

int __wrap_open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    int timed = measuring;
    int result;
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    result = __real_open(path, flags, mode);
    if (timed || measuring) note_io(0);
    return result;
}

int __wrap_close(int fd)
{
    int timed = measuring;
    int result = __real_close(fd);
    if (timed || measuring) note_io(0);
    return result;
}

ssize_t __wrap_read(int fd, void *buffer, size_t bytes)
{
    int timed = measuring;
    ssize_t result = __real_read(fd, buffer, bytes);
    if (timed || measuring) note_io(result > 0 ? (uint64_t)result : 0);
    return result;
}

ssize_t __wrap_write(int fd, const void *buffer, size_t bytes)
{
    int timed = measuring;
    ssize_t result = __real_write(fd, buffer, bytes);
    if (timed || measuring) note_io(0);
    return result;
}

off_t __wrap_lseek(int fd, off_t offset, int whence)
{
    int timed = measuring;
    off_t result = __real_lseek(fd, offset, whence);
    if (timed || measuring) note_io(0);
    return result;
}

FILE *__wrap_fopen(const char *path, const char *mode)
{
    int timed = measuring;
    FILE *result = __real_fopen(path, mode);
    if (timed || measuring) note_io(0);
    return result;
}

int __wrap_fclose(FILE *file)
{
    int timed = measuring;
    int result = __real_fclose(file);
    if (timed || measuring) note_io(0);
    return result;
}

size_t __wrap_fread(void *buffer, size_t size, size_t count, FILE *file)
{
    int timed = measuring;
    size_t result = __real_fread(buffer, size, count, file);
    if (timed || measuring) note_io((uint64_t)result * size);
    return result;
}

size_t __wrap_fwrite(const void *buffer, size_t size, size_t count, FILE *file)
{
    int timed = measuring;
    size_t result = __real_fwrite(buffer, size, count, file);
    if (timed || measuring) note_io(0);
    return result;
}

int __wrap_fseek(FILE *file, long offset, int origin)
{
    int timed = measuring;
    int result = __real_fseek(file, offset, origin);
    if (timed || measuring) note_io(0);
    return result;
}
