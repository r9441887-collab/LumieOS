#include "editor.h"
#include "terminal.h"
#include "gop.h"
#include "keyboard.h"
#include "fat.h"
#include "lumie.h"
#include "kernel.h"

#define EDITOR_MAX_LINES 1024
#define EDITOR_MAX_LINE_LEN 256
#define EDITOR_TAB_STOP 4

typedef struct {
    char lines[EDITOR_MAX_LINES][EDITOR_MAX_LINE_LEN];
    int num_lines;
    int cursor_x;
    int cursor_y;
    int offset_x;
    int offset_y;
    char filename[256];
    int modified;
    int dirty;
} editor_state;

static editor_state ed;

static void editor_status_msg(const char *msg) {
    int cols = term_get_width();
    u32 fg = gop_make_color(0xFF, 0xFF, 0xFF);
    u32 bg = gop_make_color(0x00, 0x00, 0x80);
    int yy = (term_get_height() - 1) * 16;
    gop_fill_rect(0, yy, gop_get_width(), 16, bg);
    for (int i = 0; msg[i] && i < cols; i++)
        gop_draw_char(i * 8, yy, fg, bg, msg[i]);
}

static void editor_render() {
    int rows = term_get_height() - 1;
    int cols = term_get_width();
    u32 bg = gop_make_color(0, 0, 0);
    int status_row = rows * 16;
    u32 fg_status = gop_make_color(0xFF, 0xFF, 0xFF);
    u32 bg_status = gop_make_color(0x00, 0x00, 0x80);

    /* Clear text area */
    gop_fill_rect(0, 0, gop_get_width(), status_row, bg);

    /* Draw each visible line directly at pixel position */
    for (int row = 0; row < rows; row++) {
        int line_idx = ed.offset_y + row;
        int yy = row * 16;

        if (line_idx >= ed.num_lines) continue;

        /* Line number (4 chars right-aligned) */
        char ln[8];
        lumie_itoa(line_idx + 1, ln, 10);
        int llen = lumie_strlen(ln);
        int col = 0;
        for (int c = 0; c < 4 - llen; c++, col++)
            gop_draw_char(col * 8, yy, gop_make_color(0x55,0x55,0x55), bg, ' ');
        for (int c = 0; c < llen; c++, col++)
            gop_draw_char(col * 8, yy, gop_make_color(0x55,0x55,0x55), bg, ln[c]);
        /* " | " separator */
        gop_draw_char(col * 8, yy, gop_make_color(0x55,0x55,0x55), bg, ' '); col++;
        gop_draw_char(col * 8, yy, gop_make_color(0x55,0x55,0x55), bg, '|'); col++;
        gop_draw_char(col * 8, yy, gop_make_color(0x55,0x55,0x55), bg, ' '); col++;

        /* Line content */
        u32 fg_line = gop_make_color(0xAA, 0xAA, 0xAA);
        int line_len = lumie_strlen(ed.lines[line_idx]);
        int start = ed.offset_x;
        int end = start + (cols - 6);
        if (end > line_len) end = line_len;

        for (int ci = start; ci < end; ci++) {
            char ch = ed.lines[line_idx][ci];
            if (ch == '\t') {
                for (int t = 0; t < EDITOR_TAB_STOP; t++, col++)
                    if (col < cols)
                        gop_draw_char(col * 8, yy, fg_line, bg, ' ');
            } else if (col < cols) {
                gop_draw_char(col * 8, yy, fg_line, bg, ch);
                col++;
            }
        }
        /* Clear rest of line */
        while (col < cols) {
            gop_draw_char(col * 8, yy, fg_line, bg, ' ');
            col++;
        }
    }

    /* Status bar */
    gop_fill_rect(0, status_row, gop_get_width(), 16, bg_status);
    {
        char status[128];
        char sz[16];
        lumie_itoa(ed.num_lines, sz, 10);
        status[0] = 0;
        lumie_strcpy(status, ed.filename);
        lumie_strcat(status, " - ");
        lumie_strcat(status, sz);
        lumie_strcat(status, " lines");
        if (ed.modified) lumie_strcat(status, " [MODIFIED]");
        for (int i = 0; status[i] && i < cols; i++)
            gop_draw_char(i * 8, status_row, fg_status, bg_status, status[i]);
    }
}

static void editor_insert_char(char c) {
    if (ed.cursor_y >= ed.num_lines) return;
    char *line = ed.lines[ed.cursor_y];
    int len = lumie_strlen(line);

    if (len < EDITOR_MAX_LINE_LEN - 1) {
        for (int i = len; i >= ed.cursor_x; i--) {
            line[i + 1] = line[i];
        }
        line[ed.cursor_x] = c;
        ed.cursor_x++;
        ed.modified = 1;
    }
}

static void editor_delete_char() {
    if (ed.cursor_y >= ed.num_lines) return;
    char *line = ed.lines[ed.cursor_y];
    int len = lumie_strlen(line);

    if (ed.cursor_x > 0) {
        for (int i = ed.cursor_x - 1; i < len; i++) {
            line[i] = line[i + 1];
        }
        ed.cursor_x--;
        ed.modified = 1;
    } else if (ed.cursor_y > 0) {
        /* Join with previous line */
        char *prev_line = ed.lines[ed.cursor_y - 1];
        int prev_len = lumie_strlen(prev_line);
        for (int i = 0; i < len && prev_len + i < EDITOR_MAX_LINE_LEN - 1; i++) {
            prev_line[prev_len + i] = line[i];
        }
        int joined = prev_len + len;
        if (joined > EDITOR_MAX_LINE_LEN - 1) joined = EDITOR_MAX_LINE_LEN - 1;
        prev_line[joined] = 0;

        /* Shift lines up */
        for (int i = ed.cursor_y; i < ed.num_lines - 1; i++) {
            lumie_strcpy(ed.lines[i], ed.lines[i + 1]);
        }
        ed.num_lines--;
        ed.cursor_y--;
        ed.cursor_x = prev_len;
        ed.modified = 1;
    }
}

static void editor_newline() {
    if (ed.num_lines >= EDITOR_MAX_LINES) return;

    char *line = ed.lines[ed.cursor_y];
    int len = lumie_strlen(line);

    /* Shift lines down */
    for (int i = ed.num_lines; i > ed.cursor_y + 1; i--) {
        lumie_strcpy(ed.lines[i], ed.lines[i - 1]);
    }

    /* Split line */
    int j = 0;
    for (int i = ed.cursor_x; i < len && j < EDITOR_MAX_LINE_LEN - 1; i++) {
        ed.lines[ed.cursor_y + 1][j++] = line[i];
    }
    ed.lines[ed.cursor_y + 1][j] = 0;
    line[ed.cursor_x] = 0;

    ed.num_lines++;
    ed.cursor_y++;
    ed.cursor_x = 0;
    ed.modified = 1;
}

static void editor_save() {
    /* Calculate total size */
    int total_size = 0;
    for (int i = 0; i < ed.num_lines; i++) {
        total_size += lumie_strlen(ed.lines[i]) + 1;
    }

    /* Allocate buffer based on actual size */
    u32 alloc_sz = total_size + 1 < 64 * 1024 ? total_size + 1 : 64 * 1024;
    char *buf = NULL;
    if (((efi_bs_allocate_pool)g_BS->AllocatePool)(EFI_BOOT_SERVICES_DATA, alloc_sz, (void**)&buf) != 0 || !buf) {
        editor_status_msg("Out of memory!");
        return;
    }
    if (total_size > (int)alloc_sz - 1) {
        editor_status_msg("File too large to save!");
        ((efi_bs_free_pool)g_BS->FreePool)(buf);
        return;
    }

    int pos = 0;
    for (int i = 0; i < ed.num_lines; i++) {
        int len = lumie_strlen(ed.lines[i]);
        lumie_memcpy(buf + pos, ed.lines[i], len);
        pos += len;
        buf[pos++] = '\n';
    }
    buf[pos] = 0;

    int result = fat_write_file(ed.filename, buf, pos);
    ((efi_bs_free_pool)g_BS->FreePool)(buf);
    if (result == 0) {
        ed.modified = 0;
        editor_status_msg("File saved!");
    } else {
        editor_status_msg("Error: read-only filesystem");
    }
}

void editor_run(const char *filename) {
    /* Initialize editor state */
    lumie_memset(&ed, 0, sizeof(ed));
    lumie_strcpy(ed.filename, filename);
    ed.num_lines = 1;
    ed.cursor_x = 0;
    ed.cursor_y = 0;
    ed.modified = 0;

    /* Try to load file */
    int size = fat_get_file_size(filename);
    if (size > 0 && size < 64 * 1024) {
        char *buf = NULL;
        if (((efi_bs_allocate_pool)g_BS->AllocatePool)(EFI_BOOT_SERVICES_DATA, 64 * 1024, (void**)&buf) != 0 || !buf)
            goto done_load;
        int read = fat_read_file(filename, buf, 64 * 1024 - 1);
        if (read > 0) {
            buf[read] = 0;
            /* Parse into lines */
            ed.num_lines = 0;
            int line_start = 0;
            for (int i = 0; buf[i] && ed.num_lines < EDITOR_MAX_LINES; i++) {
                if (buf[i] == '\n') {
                    int len = i - line_start;
                    if (len > 0 && buf[line_start + len - 1] == '\r') len--;
                    if (len > EDITOR_MAX_LINE_LEN - 1) len = EDITOR_MAX_LINE_LEN - 1;
                    lumie_memcpy(ed.lines[ed.num_lines], buf + line_start, len);
                    ed.lines[ed.num_lines][len] = 0;
                    ed.num_lines++;
                    line_start = i + 1;
                }
            }
            /* Last line if no trailing newline */
            if (line_start < read) {
                int len = read - line_start;
                if (len > 0 && buf[line_start + len - 1] == '\r') len--;
                if (len > EDITOR_MAX_LINE_LEN - 1) len = EDITOR_MAX_LINE_LEN - 1;
                lumie_memcpy(ed.lines[ed.num_lines], buf + line_start, len);
                ed.lines[ed.num_lines][len] = 0;
                ed.num_lines++;
            }
            if (ed.num_lines == 0) ed.num_lines = 1;
        }
        ((efi_bs_free_pool)g_BS->FreePool)(buf);
    }
done_load:

    ed.cursor_x = 0;
    ed.cursor_y = 0;
    ed.offset_x = 0;
    ed.offset_y = 0;
    ed.modified = 0;

    editor_status_msg("Press Ctrl+Q to quit, Ctrl+S to save, arrows to navigate");

    /* Hide cursor */
    term_set_cursor(0);
    kbd_flush();

    /* Editor main loop */
    int running = 1;
    while (running) {
        editor_render();

        int c = kbd_getchar();

        switch (c) {
            case KBD_UP:
                if (ed.cursor_y > 0) ed.cursor_y--;
                break;
            case KBD_DOWN:
                if (ed.cursor_y < ed.num_lines - 1) ed.cursor_y++;
                break;
            case KBD_LEFT:
                if (ed.cursor_x > 0) ed.cursor_x--;
                else if (ed.cursor_y > 0) {
                    ed.cursor_y--;
                    ed.cursor_x = lumie_strlen(ed.lines[ed.cursor_y]);
                }
                break;
            case KBD_RIGHT:
            {
                int len = lumie_strlen(ed.lines[ed.cursor_y]);
                if (ed.cursor_x < len) ed.cursor_x++;
                else if (ed.cursor_y < ed.num_lines - 1) {
                    ed.cursor_y++;
                    ed.cursor_x = 0;
                }
                break;
            }
            case KBD_HOME:
                ed.cursor_x = 0;
                break;
            case KBD_END:
                ed.cursor_x = lumie_strlen(ed.lines[ed.cursor_y]);
                break;
            case KBD_PGUP:
                ed.cursor_y -= 20;
                if (ed.cursor_y < 0) ed.cursor_y = 0;
                break;
            case KBD_PGDN:
                ed.cursor_y += 20;
                if (ed.cursor_y >= ed.num_lines) ed.cursor_y = ed.num_lines - 1;
                break;
            case 0x11: /* Ctrl+Q */
                if (ed.modified) {
                    editor_status_msg("Unsaved changes! Press Ctrl+S to save, Ctrl+Q again to quit");
                    c = kbd_getchar();
                    if (c == 0x11) running = 0;
                } else {
                    running = 0;
                }
                break;
            case 0x13: /* Ctrl+S */
                editor_save();
                break;
            case '\n':
                editor_newline();
                break;
            case '\b':
            case 0x7F:
                editor_delete_char();
                break;
            case '\t':
                for (int i = 0; i < EDITOR_TAB_STOP; i++) editor_insert_char(' ');
                break;
            default:
                if (c >= 0x20 && c <= 0x7E) {
                    editor_insert_char((char)c);
                }
                break;
        }

        /* Clamp cursor_x to current line length */
        {
            int len = lumie_strlen(ed.lines[ed.cursor_y]);
            if (ed.cursor_x > len) ed.cursor_x = len;
        }

        /* Adjust offset */
        int cols = term_get_width();
        int rows = term_get_height() - 1;

        if (ed.cursor_x < ed.offset_x) ed.offset_x = ed.cursor_x;
        if (ed.cursor_x >= ed.offset_x + cols - 6) ed.offset_x = ed.cursor_x - cols + 7;
        if (ed.cursor_y < ed.offset_y) ed.offset_y = ed.cursor_y;
        if (ed.cursor_y >= ed.offset_y + rows) ed.offset_y = ed.cursor_y - rows + 1;
    }

    term_set_cursor(1);
    term_clear(LUMIE_BLUE);
}
