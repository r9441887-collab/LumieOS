#include "shell.h"
#include "terminal.h"
#include "keyboard.h"
#include "fat.h"
#include "gop.h"
#include "editor.h"
#include "net.h"
#include "extract.h"
#include "kernel.h"

#define LINE_BUF_SIZE 4096
#define MAX_ARGS 64

static char line_buf[LINE_BUF_SIZE];
static int line_pos = 0;

static int shell_parse(char *line, char **argv, int max_args) {
    int argc = 0;
    int in_word = 0;

    for (int i = 0; line[i] && argc < max_args; i++) {
        if (line[i] == ' ' || line[i] == '\t') {
            in_word = 0;
            line[i] = 0;
        } else {
            if (!in_word) {
                argv[argc++] = &line[i];
                in_word = 1;
            }
        }
    }
    return argc;
}

static void cmd_help() {
    term_writeln("LumieOS Shell Commands:");
    term_writeln("  help        - Show this help");
    term_writeln("  clear       - Clear screen");
    term_writeln("  ls [path]   - List directory contents");
    term_writeln("  cat <file>  - Display file contents");
    term_writeln("  edit <file> - Open text editor");
    term_writeln("  echo <text> - Print text");
    term_writeln("  info        - System information");
    term_writeln("  ver         - OS version");
    term_writeln("  reboot      - Restart system");
    term_writeln("  shutdown    - Power off");
    term_writeln("  cls         - Clear screen (alias)");
    term_writeln("  dir [path]  - List directory (alias)");
    term_writeln("  type <file> - Display file (alias)");
    term_writeln("  wher <dir> <pattern> - Find files in directory recursively");
    term_writeln("  wher1 <pattern>      - Find files everywhere from root");
    term_writeln("  renet <name|url>     - Download file via HTTP (needs init)");
    term_writeln("  extract <file.tar.gz|tar.xz> - Decompress gz/xz + extract tar");
}

static void cmd_clear() {
    term_clear(LUMIE_BLACK);
}

static void cmd_ls(const char *path) {
    lumie_dirent entries[256];
    const char *dir = path ? path : "/";
    int count = fat_list_dir(dir, entries, 256);

    if (count < 0) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Error: cannot list directory");
        term_set_fg(LUMIE_LIGHTGRAY);
        return;
    }

    int total_size = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].is_dir) {
            term_set_fg(LUMIE_LIGHTCYAN);
            term_write("[DIR ] ");
        } else {
            term_set_fg(LUMIE_LIGHTGREEN);
            term_write("[FILE] ");
        }
        term_set_fg(LUMIE_WHITE);
        term_write(entries[i].name);
        if (!entries[i].is_dir) {
            char size_str[32];
            lumie_itoa(entries[i].size, size_str, 10);
            term_set_fg(LUMIE_YELLOW);
            term_write(" (");
            term_write(size_str);
            term_writeln(" bytes)");
            total_size += entries[i].size;
        } else {
            term_writeln("");
        }
    }

    term_set_fg(LUMIE_LIGHTGRAY);
    char buf[64];
    lumie_itoa(count, buf, 10);
    term_write(buf);
    term_write(" items");

    if (total_size > 0) {
        char sz[32];
        lumie_itoa(total_size, sz, 10);
        term_write(", ");
        term_write(sz);
        term_write(" bytes total");
    }
    term_writeln("");
}

static void cmd_cat(const char *file) {
    if (!file) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Usage: cat <filename>");
        term_set_fg(LUMIE_LIGHTGRAY);
        return;
    }

    /* Check file size first */
    int size = fat_get_file_size(file);
    if (size < 0) {
        term_set_fg(LUMIE_LIGHTRED);
        term_write("Error: file '");
        term_write(file);
        term_writeln("' not found");
        term_set_fg(LUMIE_LIGHTGRAY);
        return;
    }

    if (size > 64 * 1024) {
        term_set_fg(LUMIE_YELLOW);
        term_write("File too large (");
        char sz[32];
        lumie_itoa(size, sz, 10);
        term_write(sz);
        term_writeln(" bytes). Max 64KB.");
        term_set_fg(LUMIE_LIGHTGRAY);
        return;
    }

    /* Allocate buffer on heap */
    char *buf = NULL;
    if (((efi_bs_allocate_pool)g_BS->AllocatePool)(EFI_BOOT_SERVICES_DATA, 4096, (void**)&buf) != 0 || !buf) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Out of memory");
        term_set_fg(LUMIE_LIGHTGRAY);
        return;
    }

    int read = fat_read_file(file, buf, 4095);
    if (read < 0) {
        ((efi_bs_free_pool)g_BS->FreePool)(buf);
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Error reading file");
        term_set_fg(LUMIE_LIGHTGRAY);
        return;
    }

    buf[read] = 0;
    term_write(buf);
    ((efi_bs_free_pool)g_BS->FreePool)(buf);
    if (read > 0 && buf[read-1] != '\n') term_writeln("");
}

static void cmd_echo(const char *text) {
    if (text) term_writeln(text);
}

static void cmd_info() {
    fb_info *fb = gop_get_fb();
    char buf[64];

    term_set_fg(LUMIE_CYAN);
    term_writeln("=== LumieOS System Information ===");
    term_set_fg(LUMIE_LIGHTGRAY);

    term_write("Resolution: ");
    lumie_itoa(fb->width, buf, 10);
    term_write(buf);
    term_write("x");
    lumie_itoa(fb->height, buf, 10);
    term_writeln(buf);

    term_write("Framebuffer: 0x");
    lumie_itoa(fb->base, buf, 16);
    term_write(buf);
    term_write(" (");
    lumie_itoa(fb->size / 1024, buf, 10);
    term_write(buf);
    term_writeln(" KB)");

    term_write("Terminal: ");
    lumie_itoa(term_get_width(), buf, 10);
    term_write(buf);
    term_write("x");
    lumie_itoa(term_get_height(), buf, 10);
    term_writeln(buf);

    term_set_fg(LUMIE_CYAN);
    term_writeln("================================");
    term_set_fg(LUMIE_LIGHTGRAY);
}

static void cmd_ver() {
    term_set_fg(LUMIE_GREEN);
    term_writeln("LumieOS v0.1 - 64-bit UEFI Operating System");
    term_set_fg(LUMIE_LIGHTGRAY);
}

static int match_name(const char *pattern, const char *name) {
    int plen = lumie_strlen(pattern);
    int nlen = lumie_strlen(name);
    if (plen == 0) return 0;

    /* Contains: *text* */
    if (pattern[0] == '*' && pattern[plen-1] == '*') {
        char mid[256];
        int m = 0;
        for (int i = 1; i < plen - 1 && m < 255; i++) mid[m++] = pattern[i];
        mid[m] = 0;
        return lumie_strstr(name, mid) != NULL;
    }

    /* Suffix: *text */
    if (pattern[0] == '*') {
        if (nlen < plen - 1) return 0;
        return lumie_strcmp(name + nlen - (plen - 1), pattern + 1) == 0;
    }

    /* Prefix: text* */
    if (pattern[plen-1] == '*') {
        return lumie_strncmp(name, pattern, plen - 1) == 0;
    }

    /* Exact match */
    return lumie_strcmp(pattern, name) == 0;
}

#define WHER_STACK_SIZE 256
#define WHER_PATH_MAX 256

static void cmd_wher(const char *dir, const char *pattern) {
    if (!pattern) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Usage: wher <directory> <pattern>");
        term_set_fg(LUMIE_LIGHTGRAY);
        return;
    }

    char base[WHER_PATH_MAX];
    if (!dir || dir[0] == 0) {
        lumie_strcpy(base, "/");
    } else {
        lumie_strcpy(base, dir);
    }

    char *stack_mem = NULL;
    if (((efi_bs_allocate_pool)g_BS->AllocatePool)(EFI_BOOT_SERVICES_DATA, WHER_STACK_SIZE * WHER_PATH_MAX, (void**)&stack_mem) != 0 || !stack_mem) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Out of memory");
        term_set_fg(LUMIE_LIGHTGRAY);
        return;
    }
    char (*stack)[WHER_PATH_MAX] = (char (*)[WHER_PATH_MAX])stack_mem;
    int sp = 0;
    lumie_strcpy(stack[sp++], base);

    int found = 0;

    while (sp > 0) {
        sp--;
        char *cur = stack[sp];

        lumie_dirent entries[128];
        int count = fat_list_dir(cur, entries, 128);
        if (count < 0) continue;

        for (int i = 0; i < count; i++) {
            char full[WHER_PATH_MAX];
            lumie_strcpy(full, cur);
            int flen = lumie_strlen(full);
            if (full[flen-1] != '/') {
                full[flen] = '/';
                full[flen+1] = 0;
            }
            lumie_strcat(full, entries[i].name);

            if (entries[i].is_dir) {
                if (sp < WHER_STACK_SIZE) {
                    lumie_strcpy(stack[sp++], full);
                }
            }

            if (match_name(pattern, entries[i].name)) {
                term_set_fg(entries[i].is_dir ? LUMIE_LIGHTCYAN : LUMIE_LIGHTGREEN);
                term_write(full);
                if (!entries[i].is_dir) {
                    char sz[32];
                    term_set_fg(LUMIE_YELLOW);
                    term_write(" (");
                    lumie_itoa(entries[i].size, sz, 10);
                    term_write(sz);
                    term_write(" bytes)");
                }
                term_set_fg(LUMIE_LIGHTGRAY);
                term_writeln("");
                found++;
            }
        }
    }

    term_set_fg(LUMIE_LIGHTGRAY);
    if (found == 0) {
        term_set_fg(LUMIE_YELLOW);
        term_write("wher: no matches for '");
        term_write(pattern);
        term_writeln("'");
    } else {
        char buf[32];
        lumie_itoa(found, buf, 10);
        term_write(buf);
        term_writeln(" matches found");
    }
    term_set_fg(LUMIE_LIGHTGRAY);
    ((efi_bs_free_pool)g_BS->FreePool)(stack_mem);
}

static void cmd_wher1(const char *pattern) {
    if (!pattern) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Usage: wher1 <pattern>");
        term_set_fg(LUMIE_LIGHTGRAY);
        return;
    }
    cmd_wher("/", pattern);
}

static int confirm_action(const char *msg) {
    char buf[128];
    int i;
    for (i = 0; msg[i] && i < 127; i++) buf[i] = msg[i];
    buf[i] = 0;
    term_write(buf);
    term_write(" (y/n): ");

    while (1) {
        int c = kbd_getchar();
        if (c == 'y' || c == 'Y') {
            term_writeln("y");
            return 1;
        }
        if (c == 'n' || c == 'N') {
            term_writeln("n");
            return 0;
        }
    }
}

void shell_run() {
    char *argv[MAX_ARGS];
    int show_prompt = 1;

    term_set_fg(LUMIE_GREEN);
    term_writeln("LumieOS v0.1 - 64-bit UEFI Operating System");
    term_writeln("Type 'help' for commands.");
    term_writeln("");

    while (1) {
        if (show_prompt) {
            term_set_fg(LUMIE_LIGHTCYAN);
            term_write("lumie@os66");
            term_set_fg(LUMIE_WHITE);
            term_write(":");
            term_set_fg(LUMIE_LIGHTBLUE);
            term_write("~");
            term_set_fg(LUMIE_WHITE);
            term_write("$ ");
        }
        show_prompt = 1;

        line_pos = 0;
        lumie_memset(line_buf, 0, LINE_BUF_SIZE);

        /* Read line */
        while (1) {
            int c = kbd_getchar();

            if (c == '\n') {
                term_writeln("");
                break;
            }

            if (c == '\b') {
                if (line_pos > 0) {
                    line_pos--;
                    term_putchar('\b');
                    term_putchar(' ');
                    term_putchar('\b');
                }
                continue;
            }

            if (c == KBD_ESC) {
                line_pos = 0;
                lumie_memset(line_buf, 0, LINE_BUF_SIZE);
                term_writeln("^C");
                show_prompt = 1;
                break;
            }

            if (c >= ' ' && c <= '~' && line_pos < LINE_BUF_SIZE - 1) {
                line_buf[line_pos++] = (char)c;
                term_putchar((char)c);
            }
        }

        if (line_pos == 0) continue;

        int argc = shell_parse(line_buf, argv, MAX_ARGS);
        if (argc == 0) continue;

        /* Command dispatch */
        if (lumie_strcmp(argv[0], "help") == 0 || lumie_strcmp(argv[0], "?") == 0) {
            cmd_help();
        } else if (lumie_strcmp(argv[0], "clear") == 0 || lumie_strcmp(argv[0], "cls") == 0) {
            cmd_clear();
        } else if (lumie_strcmp(argv[0], "ls") == 0 || lumie_strcmp(argv[0], "dir") == 0) {
            cmd_ls(argc > 1 ? argv[1] : NULL);
        } else if (lumie_strcmp(argv[0], "cat") == 0 || lumie_strcmp(argv[0], "type") == 0) {
            cmd_cat(argc > 1 ? argv[1] : NULL);
        } else if (lumie_strcmp(argv[0], "edit") == 0) {
            if (argc > 1) {
                lumie_edit(argv[1]);
            } else {
                term_set_fg(LUMIE_LIGHTRED);
                term_writeln("Usage: edit <filename>");
                term_set_fg(LUMIE_LIGHTGRAY);
            }
        } else if (lumie_strcmp(argv[0], "echo") == 0) {
            cmd_echo(argc > 1 ? argv[1] : NULL);
        } else if (lumie_strcmp(argv[0], "info") == 0) {
            cmd_info();
        } else if (lumie_strcmp(argv[0], "ver") == 0) {
            cmd_ver();
        } else if (lumie_strcmp(argv[0], "reboot") == 0) {
            if (confirm_action("Reboot system?")) {
                lumie_reboot();
            }
        } else if (lumie_strcmp(argv[0], "shutdown") == 0) {
            if (confirm_action("Shutdown system?")) {
                lumie_shutdown();
            }
        } else if (lumie_strcmp(argv[0], "wher") == 0) {
            cmd_wher(argc > 1 ? argv[1] : NULL, argc > 2 ? argv[2] : NULL);
        } else if (lumie_strcmp(argv[0], "wher1") == 0) {
            cmd_wher1(argc > 1 ? argv[1] : NULL);
        } else if (lumie_strcmp(argv[0], "extract") == 0) {
            extract_gzip_tar(argc > 1 ? argv[1] : NULL);
        } else if (lumie_strcmp(argv[0], "renet") == 0) {
            if (net_init() != 0) {
                term_set_fg(LUMIE_LIGHTRED);
                term_writeln("Network not available");
                term_set_fg(LUMIE_LIGHTGRAY);
            } else {
                net_renet_download(argc > 1 ? argv[1] : NULL);
            }
        } else {
            term_set_fg(LUMIE_LIGHTRED);
            term_write("Unknown command: ");
            term_writeln(argv[0]);
            term_set_fg(LUMIE_LIGHTGRAY);
        }
    }
}

/* printf implementation with format string support */
void shell_printf(const char *fmt, ...) {
    char buf[1024];
    int pos = 0;
    va_list args;
    va_start(args, fmt);
    for (int i = 0; fmt[i] && pos < 1023; i++) {
        if (fmt[i] != '%') { buf[pos++] = fmt[i]; continue; }
        i++;
        switch (fmt[i]) {
            case 's': {
                const char *s = va_arg(args, const char*);
                while (*s && pos < 1023) buf[pos++] = *s++;
                break;
            }
            case 'd': {
                int val = va_arg(args, int);
                char tmp[32];
                lumie_itoa(val, tmp, 10);
                for (int j = 0; tmp[j] && pos < 1023; j++) buf[pos++] = tmp[j];
                break;
            }
            case 'u': {
                u32 val = va_arg(args, u32);
                char tmp[32];
                lumie_itoa((i64)val, tmp, 10);
                for (int j = 0; tmp[j] && pos < 1023; j++) buf[pos++] = tmp[j];
                break;
            }
            case 'x':
            case 'X': {
                u32 val = va_arg(args, u32);
                char tmp[32];
                lumie_itoa((i64)val, tmp, 16);
                for (int j = 0; tmp[j] && pos < 1023; j++) buf[pos++] = tmp[j];
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                buf[pos++] = c;
                break;
            }
            case '%':
                buf[pos++] = '%';
                break;
        }
    }
    va_end(args);
    buf[pos] = 0;
    term_write(buf);
}
