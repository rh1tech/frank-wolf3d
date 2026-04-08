/*
 * ID_IN.C - Input Manager for RP2350 (PS/2 keyboard)
 *
 * Replaces SDL event-based input with PS/2 keyboard polling.
 */

#include "wl_def.h"
#include "ps2kbd_wrapper.h"
#include "ps2.h"

/*
=============================================================================
                        GLOBAL VARIABLES
=============================================================================
*/

boolean MousePresent;
boolean forcegrabmouse;

bool        Keyboard[sc_Last];
char        textinput[TEXTINPUTSIZE];
boolean     Paused;
ScanCode    LastScan;

int JoyNumButtons;
bool GrabInput = false;

/*
=============================================================================
                        LOCAL VARIABLES
=============================================================================
*/

static boolean IN_Started;

static byte DirTable[] = {
    dir_NorthWest, dir_North, dir_NorthEast,
    dir_West,      dir_None,  dir_East,
    dir_SouthWest, dir_South, dir_SouthEast
};

/*
=============================================================================
                    PS/2 Keyboard Integration
=============================================================================
*/

// Map HID usage codes to SDL scancodes (they're nearly identical)
static ScanCode hid_to_scancode(uint8_t hid_code) {
    if (hid_code >= 4 && hid_code <= 231)
        return (ScanCode)hid_code;
    return sc_None;
}

// Convert HID key code to ASCII character (unshifted / shifted)
static char hid_to_ascii(uint8_t hid_code, bool shifted) {
    // Letters: HID 0x04 ('a') through 0x1D ('z')
    if (hid_code >= 0x04 && hid_code <= 0x1D) {
        char c = 'a' + (hid_code - 0x04);
        return shifted ? (c - 32) : c;
    }
    // Digits: HID 0x1E ('1') through 0x26 ('9'), 0x27 = '0'
    if (hid_code >= 0x1E && hid_code <= 0x27) {
        if (!shifted) {
            if (hid_code == 0x27) return '0';
            return '1' + (hid_code - 0x1E);
        }
        // Shifted digit row
        static const char shifted_digits[] = "!@#$%^&*()";
        return shifted_digits[hid_code - 0x1E];
    }
    // Space
    if (hid_code == 0x2C) return ' ';
    // Common punctuation (unshifted / shifted)
    switch (hid_code) {
        case 0x2D: return shifted ? '_' : '-';
        case 0x2E: return shifted ? '+' : '=';
        case 0x2F: return shifted ? '{' : '[';
        case 0x30: return shifted ? '}' : ']';
        case 0x31: return shifted ? '|' : '\\';
        case 0x33: return shifted ? ':' : ';';
        case 0x34: return shifted ? '"' : '\'';
        case 0x35: return shifted ? '~' : '`';
        case 0x36: return shifted ? '<' : ',';
        case 0x37: return shifted ? '>' : '.';
        case 0x38: return shifted ? '?' : '/';
    }
    return 0;
}

static ScanCode IN_MapKey(int key) {
    ScanCode scan = key;

    switch (key) {
        case sc_KeyPadEnter: scan = sc_Enter; break;
        case sc_RShift: scan = sc_LShift; break;
        case sc_RAlt: scan = sc_LAlt; break;
        case sc_RControl: scan = sc_LControl; break;
        default: break;
    }
    return scan;
}

/*
=============================================================================
                    Mouse State
=============================================================================
*/

// Accumulated mouse deltas (read and reset by SDL_GetRelativeMouseState)
static int16_t mouse_accum_dx = 0;
static int16_t mouse_accum_dy = 0;
static uint8_t mouse_buttons = 0;

/*
=============================================================================
                    INPUT PROCESSING
=============================================================================
*/

void IN_ProcessEvents(void) {
    // Process multiple PS/2 bytes per frame
    for (int t = 0; t < 16; t++)
        ps2kbd_poll();

    // Drain the event queue from the PS/2 driver
    int pressed;
    unsigned char hid_code;
    while (ps2kbd_get_key(&pressed, &hid_code)) {
        ScanCode sc = hid_to_scancode(hid_code);

        if (sc == sc_None || sc >= sc_Last) continue;

        sc = IN_MapKey(sc);

        if (pressed) {
            Keyboard[sc] = true;
            LastScan = sc;
            if (sc == sc_Pause)
                Paused = true;

            // Generate ASCII text input for save game names, menus, etc.
            bool shifted = Keyboard[sc_LShift] || Keyboard[sc_RShift];
            char ascii = hid_to_ascii(hid_code, shifted);
            if (ascii) {
                textinput[0] = ascii;
                textinput[1] = '\0';
            }
        } else {
            Keyboard[sc] = false;
        }
    }

    // Poll PS/2 mouse - accumulate deltas for SDL_GetRelativeMouseState
    if (ps2_mouse_is_initialized()) {
        int16_t dx, dy;
        int8_t wheel;
        uint8_t buttons;
        ps2_mouse_get_state(&dx, &dy, &wheel, &buttons);
        mouse_accum_dx += dx;
        mouse_accum_dy += dy;
        mouse_buttons = buttons;
    }
}

void IN_WaitEvent(void) {
    SDL_Delay(5);
    IN_ProcessEvents();
}

void IN_WaitAndProcessEvents(void) {
    IN_WaitEvent();
}

int SDL_PollEvent(SDL_Event *event) {
    (void)event;
    IN_ProcessEvents();
    return 0;
}

void SDL_WaitEvent_compat(void *unused) {
    (void)unused;
    IN_WaitEvent();
}

static int INL_GetMouseButtons(void) {
    // PS/2 buttons: bit 0=left, bit 1=right, bit 2=middle
    // Wolf3D expects same layout (original code swaps SDL middle/right to match)
    return mouse_buttons;
}

/*
=============================================================================
                    SDL Mouse Implementation
=============================================================================
*/

// Called by Wolf3D engine (wl_play.c PollControls, wl_menu.c)
uint32_t SDL_GetRelativeMouseState(int *x, int *y) {
    if (x) *x = mouse_accum_dx;
    if (y) *y = -mouse_accum_dy;  // PS/2 Y is inverted vs screen coords
    mouse_accum_dx = 0;
    mouse_accum_dy = 0;

    // Return SDL-style button mask
    // PS/2: bit 0=left, bit 1=right, bit 2=middle
    // SDL:  bit 0=left, bit 1=middle, bit 2=right
    uint32_t sdl_buttons = (mouse_buttons & 1)             // left stays bit 0
                         | ((mouse_buttons & 4) >> 1)      // middle bit 2 -> bit 1
                         | ((mouse_buttons & 2) << 1);     // right bit 1 -> bit 2
    return sdl_buttons;
}

/*
=============================================================================
                    Joystick stubs
=============================================================================
*/

void IN_GetJoyDelta(int *dx, int *dy) { *dx = *dy = 0; }
void IN_GetJoyFineDelta(int *dx, int *dy) { *dx = *dy = 0; }
int IN_JoyButtons(void) { return 0; }
boolean IN_JoyPresent(void) { return false; }
void IN_CenterMouse(void) { mouse_accum_dx = 0; mouse_accum_dy = 0; }

void IN_SetWindowGrab(SDL_Window *win) { (void)win; }

/*
=============================================================================
                    Standard Input Functions
=============================================================================
*/

void IN_Startup(void) {
    if (IN_Started) return;

    IN_ClearKeysDown();
    MousePresent = ps2_mouse_is_initialized();
    GrabInput = true;  // Bare metal - always grab input

    IN_Started = true;
}

void IN_Shutdown(void) {
    if (!IN_Started) return;
    IN_Started = false;
}

void IN_ClearKeysDown(void) {
    LastScan = sc_None;
    memset(Keyboard, 0, sizeof(Keyboard));
}

void IN_ClearTextInput(void) {
    memset(textinput, 0, sizeof(textinput));
}

void IN_ReadControl(ControlInfo *info) {
    int dx, dy;
    int mx, my;

    dx = dy = 0;
    mx = my = 0;

    IN_ProcessEvents();

    if (Keyboard[sc_UpArrow])    my = -1;
    else if (Keyboard[sc_DownArrow])  my = 1;
    if (Keyboard[sc_LeftArrow])  mx = -1;
    else if (Keyboard[sc_RightArrow]) mx = 1;

    if (Keyboard[sc_Home])   { mx = -1; my = -1; }
    else if (Keyboard[sc_PgUp])   { mx = 1;  my = -1; }
    else if (Keyboard[sc_End])    { mx = -1; my = 1; }
    else if (Keyboard[sc_PgDn])   { mx = 1;  my = 1; }

    dx = mx * 127;
    dy = my * 127;

    info->x = dx;
    info->xaxis = mx;
    info->y = dy;
    info->yaxis = my;
    info->button0 = false;
    info->button1 = false;
    info->button2 = false;
    info->button3 = false;
    info->dir = DirTable[((my + 1) * 3) + (mx + 1)];
}

ScanCode IN_WaitForKey(void) {
    ScanCode result;
    for (result = LastScan; !result; result = LastScan)
        IN_WaitAndProcessEvents();
    LastScan = 0;
    return result;
}

boolean btnstate[NUMBUTTONS];

void IN_StartAck(void) {
    IN_ProcessEvents();
    IN_ClearKeysDown();
    memset(btnstate, 0, sizeof(btnstate));
}

boolean IN_CheckAck(void) {
    IN_ProcessEvents();
    if (LastScan) return true;
    return false;
}

void IN_Ack(void) {
    IN_StartAck();
    do {
        IN_WaitAndProcessEvents();
    } while (!IN_CheckAck());
}

boolean IN_UserInput(longword delay) {
    longword lasttime;

    lasttime = GetTimeCount();
    IN_StartAck();

    do {
        IN_ProcessEvents();
        if (IN_CheckAck()) return true;
        SDL_Delay(5);
    } while (GetTimeCount() - lasttime < delay);

    return false;
}

int IN_MouseButtons(void) {
    if (MousePresent)
        return INL_GetMouseButtons();
    return 0;
}
