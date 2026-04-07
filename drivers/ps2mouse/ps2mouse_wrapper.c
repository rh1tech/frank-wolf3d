// PS/2 Mouse Wrapper - Generic (no game engine dependency)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ps2mouse_wrapper.h"
#include "ps2mouse.h"
#include <stdint.h>

static uint8_t prev_buttons = 0;
static int16_t accum_dx = 0;
static int16_t accum_dy = 0;

void ps2mouse_wrapper_init(void) {
    ps2mouse_init();
    prev_buttons = 0;
    accum_dx = 0;
    accum_dy = 0;
}

void ps2mouse_wrapper_tick(void) {
    int16_t dx, dy;
    int8_t wheel;
    uint8_t buttons;

    if (ps2mouse_get_state(&dx, &dy, &wheel, &buttons)) {
        accum_dx += dx;
        accum_dy += dy;
        prev_buttons = buttons & 0x07;
    }
}

int ps2mouse_wrapper_get_state(int *dx, int *dy, int *buttons) {
    *dx = accum_dx;
    *dy = accum_dy;
    *buttons = prev_buttons;
    int had_motion = (accum_dx != 0 || accum_dy != 0);
    accum_dx = 0;
    accum_dy = 0;
    return had_motion;
}
