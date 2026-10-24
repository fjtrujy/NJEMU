#ifndef PS2_STARTUP_TRACE_H
#define PS2_STARTUP_TRACE_H

/* Opt-in PS2Link boot checkpoints. Never call from an EE interrupt handler:
 * stdout/netprintf depends on the resident IOP server. */
#if defined(PS2_BENCHMARK_PS2LINK) && defined(PS2_STARTUP_TRACE)
#include <stdio.h>
#define PS2_BOOT_TRACE(...) do { \
    printf("[ps2-boot] "); \
    printf(__VA_ARGS__); \
    printf("\n"); \
    fflush(stdout); \
} while (0)
#else
#define PS2_BOOT_TRACE(...) ((void)0)
#endif

#endif /* PS2_STARTUP_TRACE_H */
