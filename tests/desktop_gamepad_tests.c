#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "common/input_driver.h"
#include "common/emulator_options.h"

extern input_driver_t input_desktop;
volatile int Loop = 1;
int option_desktop_p1_input = DESKTOP_P1_INPUT_BOTH;

static void key_event(Uint32 type, SDL_Scancode scancode, SDL_Keycode key)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.key.type = type;
    e.key.keysym.scancode = scancode;
    e.key.keysym.sym = key;
    assert(SDL_PushEvent(&e) == 1);
}

static input_state_t sample(void *input, uint32_t player)
{
    input_state_t s;
    assert(input_desktop.sample(input, player, &s));
    return s;
}

static void test_keyboard(void *input)
{
    input_state_t s;
    assert(input_desktop.controllerCount(input) == 1);
    s = sample(input, 0);
    assert(s.buttons == 0);
    assert(s.axis_flags == 0);
    assert(!input_desktop.sample(input, 1, &s));

    key_event(SDL_KEYDOWN, SDL_SCANCODE_UP, SDLK_UP);
    key_event(SDL_KEYDOWN, SDL_SCANCODE_Z, SDLK_z);
    key_event(SDL_KEYDOWN, SDL_SCANCODE_X, SDLK_x);
    key_event(SDL_KEYDOWN, SDL_SCANCODE_RETURN, SDLK_RETURN);
    s = sample(input, 0);
    assert(s.buttons == (PLATFORM_PAD_UP | PLATFORM_PAD_B1 |
                         PLATFORM_PAD_B2 | PLATFORM_PAD_START));
    key_event(SDL_KEYUP, SDL_SCANCODE_Z, SDLK_z);
    s = sample(input, 0);
    assert(s.buttons == (PLATFORM_PAD_UP | PLATFORM_PAD_B2 | PLATFORM_PAD_START));

    {
        SDL_Event e;
        memset(&e, 0, sizeof(e));
        e.type = SDL_WINDOWEVENT;
        e.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        assert(SDL_PushEvent(&e) == 1);
    }
    s = sample(input, 0);
    assert(s.buttons == 0);
}

#if SDL_VERSION_ATLEAST(2, 0, 14)
static int attach_virtual(void)
{
    int device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,
                                           SDL_CONTROLLER_AXIS_MAX,
                                           SDL_CONTROLLER_BUTTON_MAX, 0);
    assert(device >= 0);
    assert(SDL_IsGameController(device));
    return device;
}

static void test_controller(void *input)
{
    int first_index, second_index;
    SDL_Joystick *first, *second;
    input_state_t s;

    first_index = attach_virtual();
    assert(first_index == 0);
    first = SDL_JoystickOpen(first_index);
    assert(first != NULL);
    input_desktop.refresh(input);
    assert(input_desktop.controllerCount(input) == 1);
    assert(SDL_JoystickSetVirtualButton(first, SDL_CONTROLLER_BUTTON_B, 1) == 0);
    assert(SDL_JoystickSetVirtualButton(first, SDL_CONTROLLER_BUTTON_A, 1) == 0);
    assert(SDL_JoystickSetVirtualButton(first, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 1) == 0);
    assert(SDL_JoystickSetVirtualButton(first, SDL_CONTROLLER_BUTTON_BACK, 1) == 0);
    assert(SDL_JoystickSetVirtualButton(first, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 1) == 0);
    assert(SDL_JoystickSetVirtualAxis(first, SDL_CONTROLLER_AXIS_LEFTX, 32767) == 0);
    assert(SDL_JoystickSetVirtualAxis(first, SDL_CONTROLLER_AXIS_LEFTY, -32768) == 0);
    s = sample(input, 0);
    assert((s.buttons & (PLATFORM_PAD_B1 | PLATFORM_PAD_B2 |
                         PLATFORM_PAD_L | PLATFORM_PAD_SELECT | PLATFORM_PAD_RIGHT)) ==
           (PLATFORM_PAD_B1 | PLATFORM_PAD_B2 | PLATFORM_PAD_L |
            PLATFORM_PAD_SELECT | PLATFORM_PAD_RIGHT));
    assert(s.lx == 255 && s.ly == 0);
    assert(s.axis_flags == (INPUT_AXIS_LX | INPUT_AXIS_LY));
    assert((input_state_digital_buttons(&s) &
            (PLATFORM_PAD_RIGHT | PLATFORM_PAD_UP)) ==
            (PLATFORM_PAD_RIGHT | PLATFORM_PAD_UP));

    key_event(SDL_KEYDOWN, SDL_SCANCODE_C, SDLK_c);
    s = sample(input, 0);
    assert(s.buttons & PLATFORM_PAD_B3); /* keyboard and gamepad merge */
    key_event(SDL_KEYUP, SDL_SCANCODE_C, SDLK_c);
    (void)sample(input, 0);

    option_desktop_p1_input = DESKTOP_P1_INPUT_KEYBOARD;
    assert(input_desktop.controllerCount(input) == 2);
    key_event(SDL_KEYDOWN, SDL_SCANCODE_C, SDLK_c);
    s = sample(input, 0);
    assert(s.buttons == PLATFORM_PAD_B3);
    assert(s.axis_flags == 0);
    s = sample(input, 1);
    assert((s.buttons & PLATFORM_PAD_B1) != 0);
    assert((s.buttons & PLATFORM_PAD_B3) == 0);
    key_event(SDL_KEYUP, SDL_SCANCODE_C, SDLK_c);
    (void)sample(input, 0);

    option_desktop_p1_input = DESKTOP_P1_INPUT_CONTROLLER;
    assert(input_desktop.controllerCount(input) == 1);
    key_event(SDL_KEYDOWN, SDL_SCANCODE_C, SDLK_c);
    s = sample(input, 0);
    assert((s.buttons & PLATFORM_PAD_B1) != 0);
    assert((s.buttons & PLATFORM_PAD_B3) == 0);
    key_event(SDL_KEYUP, SDL_SCANCODE_C, SDLK_c);
    (void)sample(input, 0);

    option_desktop_p1_input = DESKTOP_P1_INPUT_BOTH;

    second_index = attach_virtual();
    assert(second_index == 1);
    second = SDL_JoystickOpen(second_index);
    assert(second != NULL);
    assert(SDL_JoystickSetVirtualButton(second, SDL_CONTROLLER_BUTTON_Y, 1) == 0);
    assert(SDL_JoystickSetVirtualButton(second, SDL_CONTROLLER_BUTTON_START, 1) == 0);
    s = sample(input, 0); /* handles controller-added events */
    assert(input_desktop.controllerCount(input) == 2);
    s = sample(input, 1);
    assert(s.buttons == (PLATFORM_PAD_B4 | PLATFORM_PAD_START));
    assert(!input_desktop.sample(input, 2, &s));

    option_desktop_p1_input = DESKTOP_P1_INPUT_KEYBOARD;
    assert(input_desktop.controllerCount(input) == 3);
    s = sample(input, 0);
    assert(s.buttons == 0 && s.axis_flags == 0);
    s = sample(input, 1);
    assert(s.buttons & PLATFORM_PAD_B1);
    s = sample(input, 2);
    assert(s.buttons == (PLATFORM_PAD_B4 | PLATFORM_PAD_START));
    option_desktop_p1_input = DESKTOP_P1_INPUT_BOTH;

    assert(SDL_JoystickDetachVirtual(first_index) == 0);
    s = sample(input, 0);
    assert(input_desktop.controllerCount(input) == 1);
    assert(s.buttons == (PLATFORM_PAD_B4 | PLATFORM_PAD_START));
    SDL_JoystickClose(first);
    assert(SDL_JoystickDetachVirtual(0) == 0);
    s = sample(input, 0);
    assert(s.buttons == 0);
    assert(s.axis_flags == 0);
    SDL_JoystickClose(second);

    option_desktop_p1_input = DESKTOP_P1_INPUT_CONTROLLER;
    key_event(SDL_KEYDOWN, SDL_SCANCODE_C, SDLK_c);
    s = sample(input, 0);
    /* Controller-only falls back to keyboard when no pad is connected. */
    assert(s.buttons == PLATFORM_PAD_B3);
    key_event(SDL_KEYUP, SDL_SCANCODE_C, SDLK_c);
    (void)sample(input, 0);
    option_desktop_p1_input = DESKTOP_P1_INPUT_BOTH;
}
#endif

static void test_quit(void *input)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    e.type = SDL_QUIT;
    assert(SDL_PushEvent(&e) == 1);
    (void)sample(input, 0);
    assert(Loop == 0);
}

int main(void)
{
    void *input;
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0);
    input = input_desktop.init();
    assert(input != NULL);
    test_keyboard(input);
#if SDL_VERSION_ATLEAST(2, 0, 14)
    test_controller(input);
#else
    puts("Virtual controller test skipped: SDL2 < 2.0.14");
#endif
    key_event(SDL_KEYDOWN, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
    (void)sample(input, 0);
    assert(Loop == 0);
    Loop = 1;
    test_quit(input);
    input_desktop.free(input);
    SDL_Quit();
    puts("Desktop gamepad input tests passed");
    return 0;
}
