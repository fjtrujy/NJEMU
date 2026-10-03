#ifndef COMMON_EMULATOR_RUNTIME_H
#define COMMON_EMULATOR_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

enum
{
	LOOP_EXIT = 0,
	LOOP_BROWSER,
	LOOP_RESTART,
	LOOP_RESET,
	LOOP_EXEC
};

#define EMULATOR_SLEEP_POLL_US 100000U

extern uint32_t frames_displayed;
extern int fatal_error;

extern volatile int Loop;
extern volatile int Sleep;

void emu_main(void);
bool emu_test_exit_after_init(void);

void autoframeskip_reset(void);
uint8_t skip_this_frame(void);
void update_screen(void);

void fatalerror(const char *text, ...);
void show_fatal_error(void);

void save_snapshot(void);
void waitVBlank(void);

#endif /* COMMON_EMULATOR_RUNTIME_H */
