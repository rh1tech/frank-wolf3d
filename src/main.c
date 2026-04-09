/*
 * WOLF3D - RP2350 Port (frank-wolf3d)
 * Main entry point with overclocking support
 */
#include "pico/stdlib.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/structs/qmi.h"
#include <stdio.h>

#include "board_config.h"

// Flash timing configuration for overclocking
// Must be called BEFORE changing system clock
// FLASH_MAX_FREQ_MHZ is defined via CMake (66 or 88 MHz)

static void __no_inline_not_in_flash_func(set_flash_timings)(int cpu_mhz) {
    const int clock_hz = cpu_mhz * 1000000;
    const int max_flash_freq = FLASH_MAX_FREQ_MHZ * 1000000;

    int divisor = (clock_hz + max_flash_freq - (max_flash_freq >> 4) - 1) / max_flash_freq;
    if (divisor == 1 && clock_hz >= 166000000) {
        divisor = 2;
    }

    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000 && clock_hz >= 166000000) {
        rxdelay += 1;
    }

    qmi_hw->m[0].timing = 0x60007000 |
                        rxdelay << QMI_M0_TIMING_RXDELAY_LSB |
                        divisor << QMI_M0_TIMING_CLKDIV_LSB;
}

// Provided by wolf_rp2350.c
void wolf_rp2350_init(void);

// Provided by wl_main.c (original Wolf4SDL)
int wolf_main(int argc, char *argv[]);

int main() {
    // Overclock support: For speeds > 252 MHz, increase voltage first
#if CPU_CLOCK_MHZ > 252
    vreg_disable_voltage_limit();
    vreg_set_voltage(CPU_VOLTAGE);
    set_flash_timings(CPU_CLOCK_MHZ);
    sleep_ms(100);
#endif

    // Set system clock
    // 640x480@60Hz pixel clock is ~25.2MHz, PIO DVI needs 10x = ~252MHz
    if (!set_sys_clock_khz(CPU_CLOCK_MHZ * 1000, false)) {
        set_sys_clock_khz(252 * 1000, true);
    }

    stdio_init_all();

    // Brief startup delay for USB serial connection
    for (int i = 0; i < 3; i++) {
        sleep_ms(500);
    }

    printf("FRANK Wolf3D - Wolfenstein 3D for RP2350\n");
    printf("System Clock: %lu MHz\n", clock_get_hz(clk_sys) / 1000000);

    // Initialize RP2350 hardware (PSRAM, HDMI, SD, PS/2, audio)
    wolf_rp2350_init();

    printf("Starting Wolfenstein 3D...\n");

    char *argv[] = {"wolf3d", NULL};
    wolf_main(1, argv);

    // Should never reach here
    while (1) {
        tight_loop_contents();
    }

    return 0;
}
