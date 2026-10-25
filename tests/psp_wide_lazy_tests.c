/*
 * PSP widescreen lifecycle regression test.
 * Compile each variant against its production wide.c and wide_runtime.c,
 * with PSP defined, to check lazy load and repeated on/off transitions.
 * Mock only the profile/parser and 68000 memory access boundaries.
 */
/* Assertions must run in CMake's Release/CI builds too. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/wide_profile.h"
#include "common/emulator_options.h"
#if defined(TEST_MVS)
#include "mvs/wide.h"
static const char *const game = "kof96";
uint16_t neogeo_ngh = 0x214;
#elif defined(TEST_CPS1)
#include "cps1/wide.h"
static const char *const game = "sf2ce";
#elif defined(TEST_CPS2)
#include "cps2/wide.h"
static const char *const game = "sfa";
#else
#error "Select TEST_MVS, TEST_CPS1 or TEST_CPS2"
#endif

char game_name[16];
char launchDir[4096];
int machine_screen_type;
uint8_t *memory_region_cpu1;
uint32_t memory_length_cpu1;
#if defined(TEST_CPS2)
uint8_t *memory_region_user1;
uint32_t memory_length_user1;
#endif

static int loads;
static int checks;
static int applications;
static int enables;
static int disables;
static int fail_next_load;

wide_profile_t *wide_profile_load(const char *path, char *error, size_t error_size)
{
    wide_profile_t *profile;
    assert(strstr(path, game) != NULL);
    (void)error;
    (void)error_size;
    loads++;
    if (fail_next_load) {
        fail_next_load = 0;
        if (error && error_size)
            snprintf(error, error_size, "test missing profile");
        return NULL;
    }
    profile = calloc(1, sizeof(*profile));
    assert(profile);
    strcpy(profile->game, game);
    profile->status = WIDE_PROFILE_VERIFIED;
#if defined(TEST_MVS)
    profile->ngh = neogeo_ngh;
#endif
    return profile;
}

int wide_profile_check(const wide_profile_t *profile, const uint16_t *program,
                       size_t size, char *error, size_t error_size)
{
    assert(profile && program && size > 0);
    (void)error;
    (void)error_size;
    checks++;
    return 0;
}

bool wide_profile_apply(const wide_profile_t *profile, uint16_t *program,
                        size_t size, bool enable, char *error, size_t error_size)
{
    assert(profile && program && size > 0);
    (void)error;
    (void)error_size;
    applications++;
    if (enable) enables++;
    else disables++;
    return true;
}

#if defined(TEST_MVS)
uint16_t m68000_read_memory_16(uint32_t address)
{
    (void)address;
    return 0;
}
void wide_layers_pan(const wide_layers_t *layers, uint16_t (*read16)(uint32_t),
                     int left, int right, int8_t *pan, size_t sprites)
{
    (void)layers;
    (void)read16;
    (void)left;
    (void)right;
    memset(pan, 0, sprites);
}
static void init(void) { mvs_wide_init(); }
static void update(void) { mvs_wide_update(); }
static void shutdown(void) { mvs_wide_exit(); }
static void option(int enabled) { option_mvs_widescreen = enabled; }
static int active(void) { return mvs_true_wide_enabled(); }
#elif defined(TEST_CPS1)
static void init(void) { cps1_wide_init(); }
static void update(void) { cps1_wide_update(); }
static void shutdown(void) { cps1_wide_exit(); }
static void option(int enabled) { option_cps_widescreen = enabled; }
static int active(void) { return cps1_wide_margin != 0; }
#else
static void init(void) { cps2_wide_init(); }
static void update(void) { cps2_wide_update(); }
static void shutdown(void) { cps2_wide_exit(); }
static void option(int enabled) { option_cps_widescreen = enabled; }
static int active(void) { return cps2_wide_margin != 0; }
#endif

int main(void)
{
    uint8_t program[256] = {0};
    strcpy(game_name, game);
    strcpy(launchDir, "/tmp/psp-wide-lazy/");
    memory_region_cpu1 = program;
    memory_length_cpu1 = sizeof(program);

    option(0);
    init();
    assert(loads == 0 && checks == 0 && active() == 0);
    for (int i = 0; i < 5; ++i) update();
    assert(loads == 0 && applications == 0);

    option(1);
    update();
    assert(loads == 1 && checks == 1 && enables == 1 && active() != 0);
    for (int i = 0; i < 5; ++i) update();
    assert(loads == 1 && applications == 1);

    option(0);
    update();
    assert(active() == 0 && disables == 1);
    for (int i = 0; i < 5; ++i) update();
    assert(loads == 1 && applications == 2);

    option(1);
    update();
    assert(loads == 2 && checks == 2 && enables == 2 && active() != 0);
    option(0);
    update();
    assert(active() == 0 && disables == 2);
    shutdown();

    option(0);
    init();
    assert(loads == 2);
    fail_next_load = 1;
    option(1);
    update();
    assert(active() == 0 && loads == 3 && applications == 4);
    /* A failed request must not retry file I/O every emulated frame. */
    for (int i = 0; i < 5; ++i) update();
    assert(loads == 3 && applications == 4);
    shutdown();

    option(0);
    init();
    option(1);
    update();
    assert(active() != 0 && loads == 4 && enables == 3);
    shutdown();
    printf("PSP widescreen lifecycle PASS: loads=%d checks=%d enables=%d disables=%d\n",
           loads, checks, enables, disables);
    return 0;
}
