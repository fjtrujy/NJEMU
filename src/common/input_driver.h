/******************************************************************************

	input_driver.h

******************************************************************************/

#ifndef INPUT_DRIVER_H
#define INPUT_DRIVER_H

#include <stdint.h>
#include <stdbool.h>

#include "input_state.h"

#define PAD_WAIT_INFINITY	-1

bool pad_init(void);
void pad_exit(void);
void refresh_gamepads(void);
uint32_t gamepad_count(void);
bool sample_gamepad_index(uint32_t controller, input_state_t *state);
uint32_t poll_gamepad(void);
uint32_t poll_gamepad_index(uint32_t controller);
void pad_update(void);
bool pad_pressed(uint32_t code);
bool pad_pressed_any(void);
bool pad_menu_combo_pressed(uint32_t buttons);
void pad_wait_clear(void);
void pad_wait_press(int msec);

extern volatile int Loop;
extern volatile int Sleep;

typedef struct input_driver
{
	/* Human-readable identifier. */
	const char *ident;
	/* Creates and initializes handle to input driver.
	*
	* Returns: input driver handle on success, otherwise NULL.
	**/
	void *(*init)(void);
	/* Stops and frees driver data. */
	void (*free)(void *data);
	/* Refresh controller topology when the caller is outside the latency-sensitive
	 * gameplay path. Backends may leave this NULL when discovery is always cheap. */
	void (*refresh)(void *data);
	/* Number of currently usable physical controllers. This must be a cheap state
	 * query and must not initiate blocking device discovery. */
	uint32_t (*controllerCount)(void *data);
	/* Sample raw physical state by logical controller index. Emulator-specific
	 * interpretation of analog axes belongs in common/target code. */
	bool (*sample)(void *data, uint32_t controller, input_state_t *state);
} input_driver_t;


extern input_driver_t *const input_driver;

#endif /* INPUT_DRIVER_H */
