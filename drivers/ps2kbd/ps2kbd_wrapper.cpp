#include "../../src/board_config.h"
#include "ps2kbd_wrapper.h"
#include "ps2kbd_mrmltr.h"
#include <string.h>
#include <queue>

struct KeyEvent {
    int pressed;
    unsigned char hid_code;
};

static std::queue<KeyEvent> event_queue;

// Track currently pressed HID key states (256 possible HID codes)
static uint8_t key_states[256];

static void key_handler(hid_keyboard_report_t *curr, hid_keyboard_report_t *prev) {
    // Check modifiers
    uint8_t changed_mods = curr->modifier ^ prev->modifier;
    if (changed_mods) {
        // Left Ctrl = HID 0xE0 = SDL_SCANCODE_LCTRL = 224
        if (changed_mods & KEYBOARD_MODIFIER_LEFTCTRL) {
            int pressed = (curr->modifier & KEYBOARD_MODIFIER_LEFTCTRL) != 0;
            key_states[0xE0] = pressed;
            event_queue.push({pressed, 0xE0});
        }
        if (changed_mods & KEYBOARD_MODIFIER_RIGHTCTRL) {
            int pressed = (curr->modifier & KEYBOARD_MODIFIER_RIGHTCTRL) != 0;
            key_states[0xE4] = pressed;
            event_queue.push({pressed, 0xE4});
        }
        // Left Shift = HID 0xE1
        if (changed_mods & KEYBOARD_MODIFIER_LEFTSHIFT) {
            int pressed = (curr->modifier & KEYBOARD_MODIFIER_LEFTSHIFT) != 0;
            key_states[0xE1] = pressed;
            event_queue.push({pressed, 0xE1});
        }
        if (changed_mods & KEYBOARD_MODIFIER_RIGHTSHIFT) {
            int pressed = (curr->modifier & KEYBOARD_MODIFIER_RIGHTSHIFT) != 0;
            key_states[0xE5] = pressed;
            event_queue.push({pressed, 0xE5});
        }
        // Left Alt = HID 0xE2
        if (changed_mods & KEYBOARD_MODIFIER_LEFTALT) {
            int pressed = (curr->modifier & KEYBOARD_MODIFIER_LEFTALT) != 0;
            key_states[0xE2] = pressed;
            event_queue.push({pressed, 0xE2});
        }
        if (changed_mods & KEYBOARD_MODIFIER_RIGHTALT) {
            int pressed = (curr->modifier & KEYBOARD_MODIFIER_RIGHTALT) != 0;
            key_states[0xE6] = pressed;
            event_queue.push({pressed, 0xE6});
        }
    }

    // Check regular keys - detect newly pressed
    for (int i = 0; i < 6; i++) {
        if (curr->keycode[i] != 0) {
            bool found = false;
            for (int j = 0; j < 6; j++) {
                if (prev->keycode[j] == curr->keycode[i]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                key_states[curr->keycode[i]] = 1;
                event_queue.push({1, curr->keycode[i]});
            }
        }
    }

    // Detect newly released
    for (int i = 0; i < 6; i++) {
        if (prev->keycode[i] != 0) {
            bool found = false;
            for (int j = 0; j < 6; j++) {
                if (curr->keycode[j] == prev->keycode[i]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                key_states[prev->keycode[i]] = 0;
                event_queue.push({0, prev->keycode[i]});
            }
        }
    }
}

static Ps2Kbd_Mrmltr *kbd = nullptr;

extern "C" void ps2kbd_init(void) {
    memset(key_states, 0, sizeof(key_states));
    kbd = new Ps2Kbd_Mrmltr(pio0, PS2_PIN_CLK, key_handler);
    kbd->init_gpio();
}

extern "C" void ps2kbd_poll(void) {
    if (kbd) kbd->tick();
}

extern "C" int ps2kbd_get_key(int *pressed, unsigned char *key) {
    if (event_queue.empty()) return 0;
    KeyEvent e = event_queue.front();
    event_queue.pop();
    *pressed = e.pressed;
    *key = e.hid_code;
    return 1;
}

extern "C" bool ps2kbd_is_key_pressed(uint8_t hid_code) {
    return key_states[hid_code] != 0;
}
