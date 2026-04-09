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
#include "ps2.h"
#include "nespad.h"
#ifdef USB_HID_ENABLED
#include "usbhid.h"
#endif
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

// Push a framebuffer to HDMI (NULL = use default wolf_screenbuffer)
void wolf_update_screen_from(uint8_t *buf) {
    if (!buf) buf = wolf_screenbuffer;
    if (!buf) return;

    uint8_t *hdmi_buf = graphics_get_buffer();
    if (!hdmi_buf) return;

    int y_offset = 20;

    memset(hdmi_buf, 0, WOLF_RESX * y_offset);
    memcpy(hdmi_buf + WOLF_RESX * y_offset, buf, WOLF_RESX * WOLF_RESY);
    memset(hdmi_buf + WOLF_RESX * (y_offset + WOLF_RESY), 0,
           WOLF_RESX * y_offset);
}

void wolf_update_screen(void) {
    wolf_update_screen_from(wolf_screenbuffer);
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

int __wrap_stat(const char *path, struct stat *buf) {
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
 * 5x7 bitmap font for welcome/error screens (before Wolf3D engine starts)
 *==========================================================================*/

static const uint8_t *glyph_5x7(char ch) {
    static const uint8_t g_space[7] = {0,0,0,0,0,0,0};
    static const uint8_t g_dot[7]   = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C};
    static const uint8_t g_comma[7] = {0x00,0x00,0x00,0x00,0x0C,0x0C,0x08};
    static const uint8_t g_colon[7] = {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00};
    static const uint8_t g_hyph[7]  = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00};
    static const uint8_t g_lp[7]    = {0x04,0x08,0x08,0x08,0x08,0x08,0x04};
    static const uint8_t g_rp[7]    = {0x04,0x02,0x02,0x02,0x02,0x02,0x04};
    static const uint8_t g_slash[7] = {0x01,0x02,0x04,0x08,0x10,0x00,0x00};
    static const uint8_t g_excl[7]  = {0x04,0x04,0x04,0x04,0x04,0x00,0x04};
    static const uint8_t g_0[7]={0x0E,0x11,0x13,0x15,0x19,0x11,0x0E};
    static const uint8_t g_1[7]={0x04,0x0C,0x04,0x04,0x04,0x04,0x0E};
    static const uint8_t g_2[7]={0x0E,0x11,0x01,0x02,0x04,0x08,0x1F};
    static const uint8_t g_3[7]={0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E};
    static const uint8_t g_4[7]={0x02,0x06,0x0A,0x12,0x1F,0x02,0x02};
    static const uint8_t g_5[7]={0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E};
    static const uint8_t g_6[7]={0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E};
    static const uint8_t g_7[7]={0x1F,0x01,0x02,0x04,0x08,0x08,0x08};
    static const uint8_t g_8[7]={0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E};
    static const uint8_t g_9[7]={0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E};
    /* uppercase */
    static const uint8_t g_A[7]={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11};
    static const uint8_t g_B[7]={0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E};
    static const uint8_t g_C[7]={0x0E,0x11,0x10,0x10,0x10,0x11,0x0E};
    static const uint8_t g_D[7]={0x1E,0x11,0x11,0x11,0x11,0x11,0x1E};
    static const uint8_t g_E[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F};
    static const uint8_t g_F[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10};
    static const uint8_t g_G[7]={0x0E,0x11,0x10,0x17,0x11,0x11,0x0E};
    static const uint8_t g_H[7]={0x11,0x11,0x11,0x1F,0x11,0x11,0x11};
    static const uint8_t g_I[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x1F};
    static const uint8_t g_J[7]={0x07,0x02,0x02,0x02,0x12,0x12,0x0C};
    static const uint8_t g_K[7]={0x11,0x12,0x14,0x18,0x14,0x12,0x11};
    static const uint8_t g_L[7]={0x10,0x10,0x10,0x10,0x10,0x10,0x1F};
    static const uint8_t g_M[7]={0x11,0x1B,0x15,0x15,0x11,0x11,0x11};
    static const uint8_t g_N[7]={0x11,0x19,0x15,0x13,0x11,0x11,0x11};
    static const uint8_t g_O[7]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E};
    static const uint8_t g_P[7]={0x1E,0x11,0x11,0x1E,0x10,0x10,0x10};
    static const uint8_t g_Q[7]={0x0E,0x11,0x11,0x11,0x15,0x12,0x0D};
    static const uint8_t g_R[7]={0x1E,0x11,0x11,0x1E,0x14,0x12,0x11};
    static const uint8_t g_S[7]={0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E};
    static const uint8_t g_T[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x04};
    static const uint8_t g_U[7]={0x11,0x11,0x11,0x11,0x11,0x11,0x0E};
    static const uint8_t g_V[7]={0x11,0x11,0x11,0x11,0x0A,0x0A,0x04};
    static const uint8_t g_W[7]={0x11,0x11,0x11,0x15,0x15,0x15,0x0A};
    static const uint8_t g_X[7]={0x11,0x0A,0x04,0x04,0x04,0x0A,0x11};
    static const uint8_t g_Y[7]={0x11,0x0A,0x04,0x04,0x04,0x04,0x04};
    static const uint8_t g_Z[7]={0x1F,0x02,0x04,0x08,0x10,0x10,0x1F};
    /* lowercase */
    static const uint8_t g_a[7]={0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F};
    static const uint8_t g_b[7]={0x10,0x10,0x1E,0x11,0x11,0x11,0x1E};
    static const uint8_t g_c[7]={0x00,0x00,0x0E,0x11,0x10,0x11,0x0E};
    static const uint8_t g_d[7]={0x01,0x01,0x0D,0x13,0x11,0x13,0x0D};
    static const uint8_t g_e[7]={0x00,0x00,0x0E,0x11,0x1F,0x10,0x0F};
    static const uint8_t g_f[7]={0x06,0x08,0x1E,0x08,0x08,0x08,0x08};
    static const uint8_t g_g[7]={0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E};
    static const uint8_t g_h[7]={0x10,0x10,0x1E,0x11,0x11,0x11,0x11};
    static const uint8_t g_i[7]={0x04,0x00,0x0C,0x04,0x04,0x04,0x0E};
    static const uint8_t g_j[7]={0x02,0x00,0x06,0x02,0x02,0x12,0x0C};
    static const uint8_t g_k[7]={0x10,0x10,0x11,0x12,0x1C,0x12,0x11};
    static const uint8_t g_l[7]={0x08,0x08,0x08,0x08,0x08,0x08,0x06};
    static const uint8_t g_m[7]={0x00,0x00,0x1A,0x15,0x15,0x15,0x15};
    static const uint8_t g_n[7]={0x00,0x00,0x1E,0x11,0x11,0x11,0x11};
    static const uint8_t g_o[7]={0x00,0x00,0x0E,0x11,0x11,0x11,0x0E};
    static const uint8_t g_p[7]={0x00,0x00,0x1E,0x11,0x1E,0x10,0x10};
    static const uint8_t g_q[7]={0x00,0x00,0x0D,0x13,0x13,0x0D,0x01};
    static const uint8_t g_r[7]={0x00,0x00,0x16,0x19,0x10,0x10,0x10};
    static const uint8_t g_s[7]={0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E};
    static const uint8_t g_t[7]={0x04,0x04,0x1F,0x04,0x04,0x04,0x03};
    static const uint8_t g_u[7]={0x00,0x00,0x11,0x11,0x11,0x13,0x0D};
    static const uint8_t g_v[7]={0x00,0x00,0x11,0x11,0x11,0x0A,0x04};
    static const uint8_t g_w[7]={0x00,0x00,0x11,0x11,0x15,0x15,0x0A};
    static const uint8_t g_x[7]={0x00,0x00,0x11,0x0A,0x04,0x0A,0x11};
    static const uint8_t g_y[7]={0x00,0x00,0x11,0x11,0x0F,0x01,0x0E};
    static const uint8_t g_z[7]={0x00,0x00,0x1F,0x02,0x04,0x08,0x1F};
    switch ((unsigned char)ch) {
        case ' ': return g_space; case '.': return g_dot; case ',': return g_comma;
        case ':': return g_colon; case '-': return g_hyph; case '(': return g_lp;
        case ')': return g_rp; case '/': return g_slash; case '!': return g_excl;
        case '0': return g_0; case '1': return g_1; case '2': return g_2;
        case '3': return g_3; case '4': return g_4; case '5': return g_5;
        case '6': return g_6; case '7': return g_7; case '8': return g_8;
        case '9': return g_9;
        case 'A': return g_A; case 'B': return g_B; case 'C': return g_C;
        case 'D': return g_D; case 'E': return g_E; case 'F': return g_F;
        case 'G': return g_G; case 'H': return g_H; case 'I': return g_I;
        case 'J': return g_J; case 'K': return g_K; case 'L': return g_L;
        case 'M': return g_M; case 'N': return g_N; case 'O': return g_O;
        case 'P': return g_P; case 'Q': return g_Q; case 'R': return g_R;
        case 'S': return g_S; case 'T': return g_T; case 'U': return g_U;
        case 'V': return g_V; case 'W': return g_W; case 'X': return g_X;
        case 'Y': return g_Y; case 'Z': return g_Z;
        case 'a': return g_a; case 'b': return g_b; case 'c': return g_c;
        case 'd': return g_d; case 'e': return g_e; case 'f': return g_f;
        case 'g': return g_g; case 'h': return g_h; case 'i': return g_i;
        case 'j': return g_j; case 'k': return g_k; case 'l': return g_l;
        case 'm': return g_m; case 'n': return g_n; case 'o': return g_o;
        case 'p': return g_p; case 'q': return g_q; case 'r': return g_r;
        case 's': return g_s; case 't': return g_t; case 'u': return g_u;
        case 'v': return g_v; case 'w': return g_w; case 'x': return g_x;
        case 'y': return g_y; case 'z': return g_z;
        default: return g_space;
    }
}

static void boot_draw_char(uint8_t *fb, int x, int y, char ch, uint8_t color) {
    const uint8_t *rows = glyph_5x7(ch);
    for (int row = 0; row < 7; ++row) {
        int yy = y + row;
        if (yy < 0 || yy >= HDMI_RESY) continue;
        uint8_t bits = rows[row];
        for (int col = 0; col < 5; ++col) {
            int xx = x + col;
            if (xx < 0 || xx >= WOLF_RESX) continue;
            if (bits & (1u << (4 - col)))
                fb[yy * WOLF_RESX + xx] = color;
        }
    }
}

static void boot_draw_text(uint8_t *fb, int x, int y, const char *text, uint8_t color) {
    for (const char *p = text; *p; ++p) {
        boot_draw_char(fb, x, y, *p, color);
        x += 6;
    }
}

static void boot_fill_rect(uint8_t *fb, int x, int y, int w, int h, uint8_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > WOLF_RESX) w = WOLF_RESX - x;
    if (y + h > HDMI_RESY) h = HDMI_RESY - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; ++yy)
        memset(&fb[yy * WOLF_RESX + x], color, (size_t)w);
}

static int boot_text_width(const char *text) {
    int n = 0;
    for (const char *p = text; *p; ++p) n++;
    return n * 6;
}

/* Cyan-themed Wolf3D welcome screen palette */
static void boot_setup_palette(void) {
    graphics_set_palette(0, 0x000000);  /* black */
    graphics_set_palette(1, 0xFFFFFF);  /* white text */
    /* Indices 2-17: dark cyans for background border */
    static const uint32_t wolf_bg_pal[16] = {
        0x000504, 0x000A08, 0x00100C, 0x001610,
        0x001C14, 0x002218, 0x00281C, 0x002E20,
        0x003424, 0x003A28, 0x00402C, 0x004630,
        0x004C34, 0x005238, 0x00583C, 0x005E40,
    };
    for (int i = 0; i < 16; ++i)
        graphics_set_palette(2 + i, wolf_bg_pal[i]);
    /* Title highlight: cyan */
    graphics_set_palette(18, 0x007050);
}

#ifndef FRANK_WOLF_VERSION
#define FRANK_WOLF_VERSION "?"
#endif

static void boot_draw_animated_bg(uint8_t *fb, uint32_t t_ms,
                                  int px, int py, int pw, int ph) {
    const int t = (int)(t_ms / 80);
    const int px2 = px + pw, py2 = py + ph;
    for (int y = 0; y < HDMI_RESY; ++y) {
        for (int x = 0; x < WOLF_RESX; ++x) {
            if (x >= px && x < px2 && y >= py && y < py2) continue;
            int bx = x >> 3, by = y >> 3;
            uint8_t v = (uint8_t)((bx + by + t) & 0x0F);
            v ^= (uint8_t)(((bx << 1) ^ (by + (t >> 1))) & 0x07);
            fb[y * WOLF_RESX + x] = (uint8_t)(2 + (v & 0x0F));
        }
    }
}

/* Draw the static panel content (called once) */
static const int WS_PX = 40, WS_PY = 70;
#define WS_PW (WOLF_RESX - 80)
#define WS_PH 100

static void wolf_draw_welcome_panel(uint8_t *fb) {
    boot_fill_rect(fb, WS_PX, WS_PY, WS_PW, WS_PH, 0);

    const char *title = "FRANK WOLF3D";
    char version[64];
    snprintf(version, sizeof(version), " v%s", FRANK_WOLF_VERSION);
    int title_w = boot_text_width(title);
    int title_x = WS_PX + (WS_PW - title_w - boot_text_width(version)) / 2;
    int ty = WS_PY + 8;
    boot_fill_rect(fb, title_x - 2, ty - 2, title_w + 4, 11, 18);
    boot_draw_text(fb, title_x, ty, title, 0);
    boot_draw_text(fb, title_x + title_w, ty, version, 1);

    int lx = WS_PX + 6;
    boot_draw_text(fb, lx, ty + 18, "Wolfenstein 3D for RP2350", 1);
    boot_draw_text(fb, lx, ty + 28, "by Mikhail Matveev", 1);

    char buf[64];
#if defined(BOARD_M2)
    const char *board = "M2";
#else
    const char *board = "M1";
#endif
    snprintf(buf, sizeof(buf), "%s, CPU: %d MHz, PSRAM: %d MHz",
             board, CPU_CLOCK_MHZ, PSRAM_MAX_FREQ_MHZ);
    boot_draw_text(fb, lx, ty + 46, buf, 1);
    boot_draw_text(fb, lx, ty + 56, "github.com/rh1tech/frank-wolf3d", 1);
#ifdef USB_HID_ENABLED
    boot_draw_text(fb, lx, ty + 66, "Gamepad: NES, USB HID", 1);
#else
    boot_draw_text(fb, lx, ty + 66, "Gamepad: NES", 1);
#endif
    boot_draw_text(fb, lx, ty + 78, "Press any key or button...", 1);
}

static void wolf_show_error(uint8_t *fb, const char *line1, const char *line2) {
    boot_setup_palette();
    graphics_set_palette(19, 0x880000);  /* dark red for error background */

    /* Static cyan background */
    for (int y = 0; y < HDMI_RESY; ++y)
        for (int x = 0; x < WOLF_RESX; ++x) {
            int bx = x >> 3, by = y >> 3;
            uint8_t v = (uint8_t)((bx + by) & 0x0F);
            v ^= (uint8_t)(((bx << 1) ^ by) & 0x07);
            fb[y * WOLF_RESX + x] = (uint8_t)(2 + (v & 0x0F));
        }

    const int panel_x = 40, panel_y = 80;
    const int panel_w = WOLF_RESX - 80, panel_h = 80;
    boot_fill_rect(fb, panel_x, panel_y, panel_w, panel_h, 19);

    int lx = panel_x + 6;
    boot_draw_text(fb, lx, panel_y + 10, "ERROR", 1);
    if (line1) boot_draw_text(fb, lx, panel_y + 30, line1, 1);
    if (line2) boot_draw_text(fb, lx, panel_y + 42, line2, 1);
    boot_draw_text(fb, lx, panel_y + 60, "Insert SD card and reset.", 1);
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

    printf("Initializing PS/2 keyboard...\n");
    ps2kbd_init();

    printf("Initializing PS/2 mouse (pio1)...\n");
    if (ps2_mouse_pio_init(pio1, PS2_MOUSE_CLK)) {
        if (ps2_mouse_init_device()) {
            printf("PS/2 mouse initialized%s\n",
                   ps2_mouse_has_wheel() ? " (wheel)" : "");
        } else {
            printf("PS/2 mouse device init failed\n");
        }
    } else {
        printf("PS/2 mouse PIO init failed\n");
    }

    printf("Initializing NES gamepad (pio0)...\n");
    if (nespad_begin(pio0, clock_get_hz(clk_sys) / 1000,
                     NESPAD_CLK_PIN, NESPAD_DATA_PIN, NESPAD_LATCH_PIN)) {
        printf("NES gamepad initialized\n");
    } else {
        printf("NES gamepad init failed\n");
    }

#ifdef USB_HID_ENABLED
    printf("Initializing USB HID Host...\n");
    usbhid_init();
#endif

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

    memset((void *)ps2_key_states, 0, sizeof(ps2_key_states));

    /* ---- SD card mount ---- */
    printf("Initializing SD card...\n");
    FRESULT fr = f_mount(&fatfs, "", 1);
    if (fr != FR_OK) {
        printf("ERROR: Failed to mount SD card (error %d)\n", fr);
        char detail[48];
        snprintf(detail, sizeof(detail), "SD mount failed (error %d)", fr);
        wolf_show_error(hdmi_framebuffer, "No SD card detected!", detail);
        while (1) tight_loop_contents();
    }

    /* Change to wolf3d data directory if it exists */
    FILINFO fno;
    if (f_stat("wolf3d", &fno) == FR_OK && (fno.fattrib & AM_DIR))
        f_chdir("wolf3d");

    /* Check that at least one VSWAP data file exists */
    int has_data = 0;
    if (f_stat("vswap.wl6", &fno) == FR_OK) has_data = 1;
    else if (f_stat("vswap.wl3", &fno) == FR_OK) has_data = 1;
    else if (f_stat("vswap.wl1", &fno) == FR_OK) has_data = 1;
    if (!has_data) {
        printf("ERROR: No Wolf3D data files found\n");
        wolf_show_error(hdmi_framebuffer,
                        "No Wolf3D data files found!",
                        "Copy .wl6/.wl1 files to /wolf3d/");
        while (1) tight_loop_contents();
    }

    extern void stdio_fatfs_init(void);
    stdio_fatfs_init();

    /* ---- Welcome screen with animated background ---- */
    boot_setup_palette();
    memset(hdmi_framebuffer, 0, WOLF_RESX * HDMI_RESY);
    wolf_draw_welcome_panel(hdmi_framebuffer);  /* panel text drawn once */

    for (;;) {
        /* Only redraw the animated border — panel area is skipped */
        boot_draw_animated_bg(hdmi_framebuffer,
                              to_ms_since_boot(get_absolute_time()),
                              WS_PX, WS_PY, WS_PW, WS_PH);

        for (int t = 0; t < 16; t++) ps2kbd_poll();
        int pressed;
        unsigned char hid_code;
        if (ps2kbd_get_key(&pressed, &hid_code) && pressed)
            break;
        nespad_read();
        if (nespad_state)
            break;
#ifdef USB_HID_ENABLED
        usbhid_task();
        {
            uint8_t kc; int down;
            if (usbhid_get_key_action(&kc, &down) && down)
                break;
        }
#endif
        sleep_ms(33);
    }

    /* Restore black screen before Wolf3D takes over */
    memset(hdmi_framebuffer, 0, WOLF_RESX * HDMI_RESY);

    boot_time = get_absolute_time();
    printf("Hardware initialization complete\n");
}
