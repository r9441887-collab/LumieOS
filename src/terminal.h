#ifndef __TERMINAL_H__
#define __TERMINAL_H__

#include "efi.h"
#include "lumie.h"

void term_init();
void term_newline();
void term_clear(lumie_color bg);
void term_putchar(char c);
void term_write(const char *str);
void term_writeln(const char *str);
void term_set_fg(lumie_color c);
void term_set_bg(lumie_color c);
void term_set_pos(int x, int y);
int  term_get_width();
int  term_get_height();
int  term_get_x();
int  term_get_y();
void term_set_cursor(int visible);
void term_set_buf(char *buf, u32 *colors);

#endif
