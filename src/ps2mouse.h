#ifndef __PS2MOUSE_H__
#define __PS2MOUSE_H__

#include "efi.h"

int ps2mouse_init(void);
int ps2mouse_poll(int *dx, int *dy, u8 *buttons);
void ps2mouse_get_pos(int *x, int *y);
void ps2mouse_set_pos(int x, int y);
int ps2mouse_is_ready(void);

#endif
