#ifndef __PS2KBD_H__
#define __PS2KBD_H__

int ps2kbd_init(void);
int ps2kbd_getchar(void);
int ps2kbd_kbhit(void);
int ps2kbd_getch_noblock(void);

#endif
