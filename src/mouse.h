#ifndef __MOUSE_H__
#define __MOUSE_H__

#include "efi.h"

#define MOUSE_LEFT_BUTTON  0x01
#define MOUSE_RIGHT_BUTTON 0x02
#define MOUSE_MIDDLE_BUTTON 0x04

typedef struct {
    int x;
    int y;
    u8 buttons;
    int dx;
    int dy;
    int present;
} mouse_state;

void mouse_init(efi_system_table *st);
int  mouse_poll(mouse_state *state);
void mouse_draw(int x, int y);
void mouse_restore(int x, int y);
void mouse_get_pos(int *x, int *y);
void mouse_set_pos(int x, int y);

#endif
