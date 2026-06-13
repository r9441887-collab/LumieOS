#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

#include "efi.h"

/* Extended key codes */
#define KBD_UP    0xE0
#define KBD_DOWN  0xE1
#define KBD_LEFT  0xE2
#define KBD_RIGHT 0xE3
#define KBD_ESC   0x1B
#define KBD_DEL   0x7F
#define KBD_HOME  0xE4
#define KBD_END   0xE5
#define KBD_PGUP  0xE6
#define KBD_PGDN  0xE7
#define KBD_INS   0xE8

void kbd_init(efi_system_table *st);
int  kbd_getchar();
int  kbd_kbhit();
int  kbd_getch_noblock();
void kbd_flush();

#endif
