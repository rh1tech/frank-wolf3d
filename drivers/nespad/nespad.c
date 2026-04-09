/*
 * NES gamepad driver via GPIO bit-bang
 *
 * Replaces PIO-based driver to free pio0 state machine resources.
 *
 * Based on pico-infonesPlus by shuichitakano and fhoedemakers
 * https://github.com/fhoedemakers/pico-infonesPlus
 * SPDX-License-Identifier: MIT
 */

#include "nespad.h"
#include "hardware/gpio.h"
#include "pico/time.h"

static uint8_t pin_clk;
static uint8_t pin_data;
static uint8_t pin_latch;
static bool pad_initialized = false;

uint32_t nespad_state = 0;
uint32_t nespad_state2 = 0;

bool nespad_begin(PIO pio, uint32_t cpu_khz, uint8_t clkPin,
                  uint8_t dataPin, uint8_t latPin)
{
    (void)pio;
    (void)cpu_khz;

    pin_clk = clkPin;
    pin_data = dataPin;
    pin_latch = latPin;

    gpio_init(pin_clk);
    gpio_init(pin_data);
    gpio_init(pin_latch);

    gpio_set_dir(pin_clk, GPIO_OUT);
    gpio_set_dir(pin_latch, GPIO_OUT);
    gpio_set_dir(pin_data, GPIO_IN);
    gpio_pull_up(pin_data);

    gpio_put(pin_clk, 1);
    gpio_put(pin_latch, 0);

    pad_initialized = true;
    return true;
}

void nespad_read(void) {
    if (!pad_initialized) return;

    gpio_put(pin_latch, 1);
    busy_wait_us_32(12);
    gpio_put(pin_latch, 0);
    gpio_put(pin_clk, 0);
    busy_wait_us_32(6);

    uint8_t buttons = 0;
    for (int i = 0; i < 8; i++) {
        if (!gpio_get(pin_data))
            buttons |= (1 << i);
        gpio_put(pin_clk, 1);
        busy_wait_us_32(6);
        gpio_put(pin_clk, 0);
        busy_wait_us_32(6);
    }
    gpio_put(pin_clk, 1);

    uint32_t state = 0;
    if (buttons & 0x01) state |= DPAD_A;
    if (buttons & 0x02) state |= DPAD_B;
    if (buttons & 0x04) state |= DPAD_SELECT;
    if (buttons & 0x08) state |= DPAD_START;
    if (buttons & 0x10) state |= DPAD_UP;
    if (buttons & 0x20) state |= DPAD_DOWN;
    if (buttons & 0x40) state |= DPAD_LEFT;
    if (buttons & 0x80) state |= DPAD_RIGHT;

    nespad_state = state;
    nespad_state2 = 0;
}

void nespad_read_start(void) { nespad_read(); }
void nespad_read_finish(void) { }
