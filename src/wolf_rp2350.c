/*
 * wolf_rp2350.c - RP2350 hardware initialization for Wolf3D
 *
 * Initializes PSRAM, HDMI, SD card, PS/2 keyboard, and I2S audio.
 * Provides SDL compatibility functions (SDL_GetTicks, SDL_Delay, etc.)
 */

#include "board_config.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#include "HDMI.h"
#include "psram_init.h"
#include "psram_allocator.h"
#include "ff.h"
#include "ps2kbd_wrapper.h"
#include "audio.h"

#include <stdio.h>
#include <string.h>

#include "sdl_compat.h"

/*==========================================================================
 * Global state
 *==========================================================================*/

// HDMI framebuffer (8-bit indexed, 320x240)
#define WOLF_RESX 320
#define WOLF_RESY 200
#define HDMI_RESY 240

// The screenBuffer (8-bit indexed) that Wolf3D draws into
static uint8_t *wolf_screenbuffer = NULL;

// HDMI display buffer (320x240, with black bars for 320x200 content)
static uint8_t *hdmi_framebuffer = NULL;

// Current palette (RGB888 for HDMI conversion)
static SDL_Color current_palette[256];

// FatFS mount
static FATFS fatfs;

// Timing base (ms since boot)
static absolute_time_t boot_time;

// PS/2 keyboard state
static volatile uint8_t ps2_key_states[SDL_NUM_SCANCODES];
static volatile int32_t ps2_last_scancode = 0;

// I2S audio config
static i2s_config_t audio_config;

/*==========================================================================
 * SDL_GetTicks / SDL_Delay implementations
 *==========================================================================*/

uint32_t SDL_GetTicks(void) {
    return to_ms_since_boot(get_absolute_time());
}

void SDL_Delay(uint32_t ms) {
    sleep_ms(ms);
}

/*==========================================================================
 * SDL_Event / PS/2 keyboard integration
 *==========================================================================*/

// Map HID usage codes to SDL scancodes
static uint8_t hid_to_sdl_scancode(uint8_t hid_code) {
    // HID keyboard usage codes map almost directly to SDL scancodes
    // for the common range (4-231 maps to SDL_SCANCODE_A through SDL_SCANCODE_RALT)
    if (hid_code >= 4 && hid_code <= 231)
        return hid_code;
    return SDL_SCANCODE_UNKNOWN;
}

// Called by PS/2 keyboard driver when a key event occurs
void wolf_ps2_key_callback(uint8_t hid_code, bool pressed) {
    uint8_t sc = hid_to_sdl_scancode(hid_code);
    if (sc != SDL_SCANCODE_UNKNOWN && sc < SDL_NUM_SCANCODES) {
        ps2_key_states[sc] = pressed ? 1 : 0;
        if (pressed) {
            ps2_last_scancode = sc;
        }
    }
}

// SDL_PollEvent and SDL_WaitEvent_compat are defined in id_in_rp2350.c

/*==========================================================================
 * Framebuffer / HDMI interface
 *
 * Wolf3D's screenBuffer is an 8-bit indexed surface (320x200).
 * On each VH_UpdateScreen call, we convert to HDMI pixel format.
 *==========================================================================*/

// Provide access to the framebuffer for Wolf3D's id_vl.c
uint8_t *wolf_get_screenbuffer(void) {
    return wolf_screenbuffer;
}

// Get current palette
SDL_Color *wolf_get_palette(void) {
    return current_palette;
}

// Push framebuffer to HDMI
void wolf_update_screen(void) {
    if (!wolf_screenbuffer) return;

    // The HDMI driver uses a shared graphics buffer.
    // Wolf3D is 320x200, HDMI is 320x240 -- center vertically with 20px black bars
    uint8_t *hdmi_buf = graphics_get_buffer();
    if (!hdmi_buf) return;

    int y_offset = 20;

    // Clear top bar
    memset(hdmi_buf, 0, WOLF_RESX * y_offset);

    // Copy framebuffer (already 8-bit indexed, HDMI palette handles conversion)
    memcpy(hdmi_buf + WOLF_RESX * y_offset, wolf_screenbuffer,
           WOLF_RESX * WOLF_RESY);

    // Clear bottom bar
    memset(hdmi_buf + WOLF_RESX * (y_offset + WOLF_RESY), 0,
           WOLF_RESX * y_offset);
}

// Set HDMI palette from SDL_Color array
void wolf_set_palette(SDL_Color *palette) {
    memcpy(current_palette, palette, sizeof(current_palette));

    // Update HDMI palette
    for (int i = 0; i < 256; i++) {
        uint32_t color888 = ((uint32_t)palette[i].r << 16) |
                            ((uint32_t)palette[i].g << 8) |
                            (uint32_t)palette[i].b;
        graphics_set_palette(i, color888);
    }
}

// Error() is provided by wl_utils.c

// mkdir stub for bare metal - FatFS uses f_mkdir internally
#include <sys/stat.h>

int mkdir(const char *path, mode_t mode) {
    (void)mode;
    FRESULT fr = f_mkdir(path);
    return (fr == FR_OK || fr == FR_EXIST) ? 0 : -1;
}

// stat() via FatFS - needed by CheckForEpisodes
int unlink(const char *path) {
    FRESULT fr = f_unlink(path);
    return (fr == FR_OK) ? 0 : -1;
}

int _stat(const char *path, struct stat *buf) {
    FILINFO fno;
    FRESULT fr = f_stat(path, &fno);
    if (fr != FR_OK) return -1;
    memset(buf, 0, sizeof(*buf));
    buf->st_size = fno.fsize;
    if (fno.fattrib & AM_DIR) buf->st_mode = S_IFDIR;
    else buf->st_mode = S_IFREG;
    return 0;
}

/*==========================================================================
 * Hardware initialization
 *==========================================================================*/

void wolf_rp2350_init(void) {
    printf("Initializing PSRAM...\n");
    uint psram_pin = get_psram_pin();
    psram_init(psram_pin);

    printf("Initializing HDMI (320x240)...\n");

    // Allocate HDMI display buffer (320x240) in PSRAM
    hdmi_framebuffer = (uint8_t *)psram_malloc(WOLF_RESX * HDMI_RESY);
    if (!hdmi_framebuffer) {
        printf("ERROR: Failed to allocate HDMI framebuffer\n");
        while (1) tight_loop_contents();
    }
    memset(hdmi_framebuffer, 0, WOLF_RESX * HDMI_RESY);

    // Allocate Wolf3D game framebuffer (320x200) in PSRAM
    wolf_screenbuffer = (uint8_t *)psram_malloc(WOLF_RESX * WOLF_RESY);
    if (!wolf_screenbuffer) {
        printf("ERROR: Failed to allocate game framebuffer\n");
        while (1) tight_loop_contents();
    }
    memset(wolf_screenbuffer, 0, WOLF_RESX * WOLF_RESY);

    // Point HDMI driver at our buffer and initialize
    graphics_set_res(WOLF_RESX, HDMI_RESY);
    graphics_set_buffer(hdmi_framebuffer);
    graphics_init(g_out_HDMI);

    printf("Initializing SD card...\n");
    FRESULT fr = f_mount(&fatfs, "", 1);
    if (fr != FR_OK) {
        printf("ERROR: Failed to mount SD card (error %d)\n", fr);
        while (1) tight_loop_contents();
    }
    printf("SD card mounted successfully\n");

    // Change to wolf3d data directory if it exists, otherwise stay in root
    FILINFO fno;
    if (f_stat("wolf3d", &fno) == FR_OK && (fno.fattrib & AM_DIR)) {
        f_chdir("wolf3d");
        printf("Using data directory: /wolf3d/\n");
    } else {
        printf("Using data directory: / (root)\n");
    }

    // Initialize stdio_fatfs wrappers
    extern void stdio_fatfs_init(void);
    stdio_fatfs_init();

    printf("Initializing PS/2 keyboard...\n");
    ps2kbd_init();

    printf("Initializing I2S audio...\n");
    memset(&audio_config, 0, sizeof(audio_config));
    audio_config.sample_freq = 44100;
    audio_config.channel_count = 2;
    audio_config.data_pin = I2S_DATA_PIN;
    audio_config.clock_pin_base = I2S_CLOCK_PIN_BASE;
    audio_config.pio = pio1;
    audio_config.dma_trans_count = 1024;
    audio_config.volume = 0;
    i2s_init(&audio_config);

    // Clear keyboard state
    memset((void *)ps2_key_states, 0, sizeof(ps2_key_states));

    boot_time = get_absolute_time();

    printf("Hardware initialization complete\n");
}
