#ifndef PS2KBD_WRAPPER_H
#define PS2KBD_WRAPPER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ps2kbd_init(void);
void ps2kbd_poll(void);
int  ps2kbd_get_key(int *pressed, unsigned char *key);

// Check if a specific HID keycode is currently pressed
bool ps2kbd_is_key_pressed(uint8_t hid_code);

#ifdef __cplusplus
}
#endif

#endif
