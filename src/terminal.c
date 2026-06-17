#include "terminal.h"
#include "gop.h"
#include "mm.h"

#define TAB_WIDTH 4

static int term_x = 0, term_y = 0;
static int term_cols, term_rows;
static int fg_color = 0x00FFFFFF;
static int bg_color = 0x000000AA;
static int cursor_enabled = 1;

static char *screen_buf = NULL;
static u32 *color_buf = NULL;
static int screen_buf_size = 0;

void term_init() {
    term_cols = gop_get_width() / 8;
    term_rows = gop_get_height() / 16;
    screen_buf_size = term_cols * term_rows;
    if (screen_buf_size > 0) {
        screen_buf = kmalloc(screen_buf_size);
        color_buf = kmalloc(screen_buf_size * sizeof(u32));
        if (screen_buf) lumie_memset(screen_buf, ' ', screen_buf_size);
    }
}

static void update_line_from_buf(int row) {
    int y = row * 16;
    gop_fill_rect(0, y, gop_get_width(), 16, bg_color);
    for (int col = 0; col < term_cols; col++) {
        int idx = row * term_cols + col;
        char c = screen_buf ? screen_buf[idx] : ' ';
        u32 fg = color_buf ? color_buf[idx] : (u32)fg_color;
        gop_draw_char(col * 8, y, fg, bg_color, c);
    }
}

void term_clear(lumie_color bg) {
    bg_color = gop_make_color(0, 0, 0xAA);
    if (bg == LUMIE_BLACK) bg_color = gop_make_color(0, 0, 0);
    else if (bg == LUMIE_BLUE) bg_color = gop_make_color(0x00, 0x00, 0xAA);
    else if (bg == LUMIE_GREEN) bg_color = gop_make_color(0, 0xAA, 0);
    else if (bg == LUMIE_CYAN) bg_color = gop_make_color(0, 0xAA, 0xAA);
    else if (bg == LUMIE_RED) bg_color = gop_make_color(0xAA, 0, 0);
    else if (bg == LUMIE_WHITE) bg_color = gop_make_color(0xAA, 0xAA, 0xAA);
    else if (bg == LUMIE_DARKGRAY) bg_color = gop_make_color(0x55, 0x55, 0x55);

    gop_fill_rect(0, 0, gop_get_width(), gop_get_height(), bg_color);
    term_x = 0;
    term_y = 0;
}

static void scroll_up() {
    for (int row = 1; row < term_rows; row++) {
        for (int col = 0; col < term_cols; col++) {
            int dst = (row - 1) * term_cols + col;
            int src = row * term_cols + col;
            if (screen_buf) screen_buf[dst] = screen_buf[src];
            if (color_buf) color_buf[dst] = color_buf[src];
        }
    }
    int last_row = term_rows - 1;
    for (int col = 0; col < term_cols; col++) {
        if (screen_buf) screen_buf[last_row * term_cols + col] = ' ';
        if (color_buf) color_buf[last_row * term_cols + col] = fg_color;
    }
    gop_fill_rect(0, 0, gop_get_width(), gop_get_height(), bg_color);
    for (int row = 0; row < term_rows; row++) {
        update_line_from_buf(row);
    }
}

void term_newline() {
    term_x = 0;
    term_y++;
    if (term_y >= term_rows) {
        term_y = term_rows - 1;
        scroll_up();
    }
}

void term_putchar(char c) {
    if (c == '\n') {
        term_newline();
        return;
    }
    if (c == '\r') {
        term_x = 0;
        return;
    }
    if (c == '\b') {
        if (term_x > 0) term_x--;
        return;
    }
    if (c == '\t') {
        int next = (term_x / TAB_WIDTH + 1) * TAB_WIDTH;
        while (term_x < next && term_x < term_cols) term_putchar(' ');
        return;
    }

    if (term_x >= term_cols) term_newline();

    int idx = term_y * term_cols + term_x;
    if (screen_buf && idx < screen_buf_size) screen_buf[idx] = c;
    if (color_buf && idx < screen_buf_size) color_buf[idx] = fg_color;

    gop_draw_char(term_x * 8, term_y * 16, fg_color, bg_color, c);
    term_x++;
}

void term_write(const char *str) {
    while (*str) {
        term_putchar(*str);
        str++;
    }
}

void term_writeln(const char *str) {
    term_write(str);
    term_newline();
}

void term_set_fg(lumie_color c) {
    switch (c) {
        case LUMIE_BLACK:       fg_color = gop_make_color(0,0,0); break;
        case LUMIE_BLUE:        fg_color = gop_make_color(0,0,0xAA); break;
        case LUMIE_GREEN:       fg_color = gop_make_color(0,0xAA,0); break;
        case LUMIE_CYAN:        fg_color = gop_make_color(0,0xAA,0xAA); break;
        case LUMIE_RED:         fg_color = gop_make_color(0xAA,0,0); break;
        case LUMIE_MAGENTA:     fg_color = gop_make_color(0xAA,0,0xAA); break;
        case LUMIE_BROWN:       fg_color = gop_make_color(0xAA,0x55,0); break;
        case LUMIE_LIGHTGRAY:   fg_color = gop_make_color(0xAA,0xAA,0xAA); break;
        case LUMIE_DARKGRAY:    fg_color = gop_make_color(0x55,0x55,0x55); break;
        case LUMIE_LIGHTBLUE:   fg_color = gop_make_color(0x55,0x55,0xFF); break;
        case LUMIE_LIGHTGREEN:  fg_color = gop_make_color(0x55,0xFF,0x55); break;
        case LUMIE_LIGHTCYAN:   fg_color = gop_make_color(0x55,0xFF,0xFF); break;
        case LUMIE_LIGHTRED:    fg_color = gop_make_color(0xFF,0x55,0x55); break;
        case LUMIE_LIGHTMAGENTA:fg_color = gop_make_color(0xFF,0x55,0xFF); break;
        case LUMIE_YELLOW:      fg_color = gop_make_color(0xFF,0xFF,0x55); break;
        case LUMIE_WHITE:       fg_color = gop_make_color(0xFF,0xFF,0xFF); break;
    }
}

void term_set_bg(lumie_color c) {
    switch (c) {
        case LUMIE_BLACK:       bg_color = gop_make_color(0,0,0); break;
        case LUMIE_WHITE:       bg_color = gop_make_color(0xAA,0xAA,0xAA); break;
        case LUMIE_BLUE:        bg_color = gop_make_color(0x00,0x00,0xAA); break;
        case LUMIE_DARKGRAY:    bg_color = gop_make_color(0x33,0x33,0x33); break;
        default:                bg_color = gop_make_color(0,0,0); break;
    }
}

void term_set_pos(int x, int y) {
    if (x >= 0 && x < term_cols) term_x = x;
    if (y >= 0 && y < term_rows) term_y = y;
}

int term_get_width() { return term_cols; }
int term_get_height() { return term_rows; }
int term_get_x() { return term_x; }
int term_get_y() { return term_y; }

void term_set_cursor(int visible) {
    cursor_enabled = visible;
}

void term_set_buf(char *buf, u32 *colors) {
    screen_buf = buf;
    color_buf = colors;
}
