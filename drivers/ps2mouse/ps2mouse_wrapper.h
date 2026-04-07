// PS/2 Mouse Wrapper - Generic
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef PS2MOUSE_WRAPPER_H
#define PS2MOUSE_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

void ps2mouse_wrapper_init(void);
void ps2mouse_wrapper_tick(void);
int  ps2mouse_wrapper_get_state(int *dx, int *dy, int *buttons);

#ifdef __cplusplus
}
#endif

#endif // PS2MOUSE_WRAPPER_H
