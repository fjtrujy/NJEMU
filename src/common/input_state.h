/******************************************************************************

	input_state.h

	Platform-neutral physical input sample and helpers.

******************************************************************************/

#ifndef INPUT_STATE_H
#define INPUT_STATE_H

#include <stdint.h>

/* Platform-neutral digital controls. */
#define PLATFORM_PAD_UP     (1u << 0)
#define PLATFORM_PAD_DOWN   (1u << 1)
#define PLATFORM_PAD_LEFT   (1u << 2)
#define PLATFORM_PAD_RIGHT  (1u << 3)
#define PLATFORM_PAD_B1     (1u << 4)
#define PLATFORM_PAD_B2     (1u << 5)
#define PLATFORM_PAD_B3     (1u << 6)
#define PLATFORM_PAD_B4     (1u << 7)
#define PLATFORM_PAD_L      (1u << 8)
#define PLATFORM_PAD_R      (1u << 9)
#define PLATFORM_PAD_SELECT (1u << 10)
#define PLATFORM_PAD_START  (1u << 11)

#define INPUT_AXIS_LX (1u << 0)
#define INPUT_AXIS_LY (1u << 1)

#define INPUT_ANALOG_NEUTRAL        0x80u
#define INPUT_ANALOG_LOW_THRESHOLD  0x30u
#define INPUT_ANALOG_HIGH_THRESHOLD 0xd0u

typedef struct input_state
{
	uint32_t buttons;
	uint8_t lx;
	uint8_t ly;
	uint8_t axis_flags;
} input_state_t;

void input_state_reset(input_state_t *state);
uint32_t input_state_digital_buttons(const input_state_t *state);

#endif /* INPUT_STATE_H */
