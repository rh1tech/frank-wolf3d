/*
 * NES gamepad driver via GPIO bit-bang
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define DPAD_LEFT   0x001000
#define DPAD_RIGHT  0x004000
#define DPAD_DOWN   0x000400
#define DPAD_UP     0x000100
#define DPAD_START  0x000040
#define DPAD_SELECT 0x000010
#define DPAD_B      0x000004
#define DPAD_A      0x000001

#define DPAD_Y      0x010000
#define DPAD_X      0x040000
#define DPAD_LT     0x100000
#define DPAD_RT     0x400000

extern uint32_t nespad_state;
extern uint32_t nespad_state2;

#include "hardware/pio.h"

extern bool nespad_begin(PIO pio, uint32_t cpu_khz, uint8_t clkPin,
                         uint8_t dataPin, uint8_t latPin);
extern void nespad_read(void);
extern void nespad_read_start(void);
extern void nespad_read_finish(void);
