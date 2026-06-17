#include "shell.h"
#include "terminal.h"
#include "keyboard.h"
#include "fat.h"
#include "gop.h"
#include "editor.h"
#include "net.h"
#include "extract.h"
#include "kernel.h"
#include "mouse.h"
#include "mm.h"

#define LINE_BUF_SIZE 4096
#define MAX_ARGS 64

static char line_buf[LINE_BUF_SIZE];
static int line_pos = 0;
static char cwd[256] = "/";
static int mouse_visible = 0;

static void resolve_path(const char *input, char *output, int max_out) {
    if (!input || input[0] == 0) {
        lumie_strcpy(output, cwd);
        return;
    }
    if (input[0] == '/') {
        int ilen = lumie_strlen(input);
        if (ilen >= max_out) ilen = max_out - 1;
        lumie_memcpy(output, input, ilen);
        output[ilen] = 0;
        return;
    }
    int cwd_len = lumie_strlen(cwd);
    if (cwd_len >= max_out) cwd_len = max_out - 1;
    lumie_memcpy(output, cwd, cwd_len);
    output[cwd_len] = 0;
    int len = cwd_len;
    if (len > 0 && output[len-1] != '/') {
        if (len < max_out - 1) { output[len] = '/'; output[len+1] = 0; len++; }
    }
    int ilen = lumie_strlen(input);
    if (len + ilen >= max_out) ilen = max_out - 1 - len;
    if (ilen > 0) lumie_memcpy(output + len, input, ilen);
    output[len + ilen] = 0;
}

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

static void cmd_pwd() {
    term_writeln(cwd);
}

static void cmd_cd(const char *path) {
    if (!path || path[0] == 0) {
        lumie_strcpy(cwd, "/");
        return;
    }

    char resolved[256];
    resolve_path(path, resolved, 256);

    int len = lumie_strlen(resolved);
    if (len > 1 && resolved[len-1] == '/') resolved[len-1] = 0;

    if (!fat_exists(resolved)) {
        term_set_fg(LUMIE_LIGHTRED);
        term_write("cd: directory not found: ");
        term_writeln(path);
        term_set_fg(LUMIE_WHITE);
        return;
    }

    lumie_dirent check;
    if (fat_list_dir(resolved, &check, 1) < 0) {
        term_set_fg(LUMIE_LIGHTRED);
        term_write("cd: not a directory: ");
        term_writeln(path);
        term_set_fg(LUMIE_WHITE);
        return;
    }

    lumie_strcpy(cwd, resolved);
    if (cwd[0] == 0) { cwd[0] = '/'; cwd[1] = 0; }
}

static void cmd_rm(const char *path) {
    if (!path) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Usage: rm <file>");
        term_set_fg(LUMIE_WHITE);
        return;
    }
    char resolved[256];
    resolve_path(path, resolved, 256);

    int ret = fat_delete(resolved);
    if (ret == -2) {
        term_set_fg(LUMIE_LIGHTRED);
        term_write("rm: directory not empty: ");
        term_writeln(path);
        term_set_fg(LUMIE_WHITE);
    } else if (ret != 0) {
        term_set_fg(LUMIE_LIGHTRED);
        term_write("rm: failed to delete: ");
        term_writeln(path);
        term_set_fg(LUMIE_WHITE);
    }
}

static void cmd_mkdir(const char *path) {
    if (!path) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Usage: mkdir <directory>");
        term_set_fg(LUMIE_WHITE);
        return;
    }
    char resolved[256];
    resolve_path(path, resolved, 256);

    if (fat_mkdir(resolved) != 0) {
        term_set_fg(LUMIE_LIGHTRED);
        term_write("mkdir: failed to create: ");
        term_writeln(path);
        term_set_fg(LUMIE_WHITE);
    }
}

static void cmd_ps() {
    term_set_fg(LUMIE_LIGHTCYAN);
    term_writeln("PID  NAME");
    term_set_fg(LUMIE_WHITE);
    term_writeln("  0  shell");
    term_writeln("  1  notepad");
}

static void cmd_notepad() {
    char fname[256];
    term_set_fg(LUMIE_LIGHTCYAN);
    term_write("Notepad - Enter filename to edit: ");
    term_set_fg(LUMIE_WHITE);

    int pos = 0;
    lumie_memset(fname, 0, 256);
    while (1) {
        int c = kbd_getchar();
        if (c == '\n') {
            term_writeln("");
            break;
        }
        if (c == '\b') {
            if (pos > 0) { pos--; term_write("\b \b"); }
            continue;
        }
        if (c >= ' ' && c <= '~' && pos < 255) {
            fname[pos++] = (char)c;
            term_putchar((char)c);
        }
    }

    if (pos > 0) {
        char resolved[256];
        resolve_path(fname, resolved, 256);
        editor_run(resolved);
    }
}

static void cmd_help() {
    term_set_fg(LUMIE_LIGHTCYAN);
    term_writeln("LumieOS (Windows Edition) Commands:");
    term_set_fg(LUMIE_WHITE);
    term_writeln("  help            - Show this help");
    term_writeln("  clear/cls       - Clear screen");
    term_writeln("  ls/dir [path]   - List directory contents");
    term_writeln("  cd <path>       - Change directory");
    term_writeln("  pwd             - Print working directory");
    term_writeln("  cat/type <file> - Display file contents");
    term_writeln("  edit <file>     - Open text editor");
    term_writeln("  notepad         - Open Notepad (text editor)");
    term_writeln("  echo <text>     - Print text");
    term_writeln("  rm/del <file>   - Delete file");
    term_writeln("  rmdir <dir>     - Delete empty directory");
    term_writeln("  mkdir <dir>     - Create directory");
    term_writeln("  ps              - List running processes");
    term_writeln("  info            - System information");
    term_writeln("  ver             - OS version");
    term_writeln("  reboot          - Restart system");
    term_writeln("  shutdown        - Power off");
    term_writeln("  wher <dir> <pat> - Find files recursively");
    term_writeln("  wher1 <pat>      - Find everywhere from root");
    term_writeln("  renet <name>    - Download via HTTP");
    term_writeln("  extract <file>  - Decompress tar.gz/tar.xz");
}

static void cmd_clear() {
    term_clear(LUMIE_BLUE);
}

static void cmd_ls(const char *path) {
    lumie_dirent *entries = (lumie_dirent*)kmalloc(256 * sizeof(lumie_dirent));
    if (!entries) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Out of memory");
        term_set_fg(LUMIE_WHITE);
        return;
    }
    char resolved[256];
    if (path) resolve_path(path, resolved, 256);
    const char *dir = path ? resolved : cwd;
    int count = fat_list_dir(dir, entries, 256);

    if (count < 0) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Error: cannot list directory");
        term_set_fg(LUMIE_WHITE);
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

    term_set_fg(LUMIE_WHITE);
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
    kfree(entries);
}

static void cmd_cat(const char *file) {
    if (!file) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Usage: cat <filename>");
        term_set_fg(LUMIE_WHITE);
        return;
    }

    char resolved[256];
    resolve_path(file, resolved, 256);

    int size = fat_get_file_size(resolved);
    if (size < 0) {
        term_set_fg(LUMIE_LIGHTRED);
        term_write("Error: file '");
        term_write(file);
        term_writeln("' not found");
        term_set_fg(LUMIE_WHITE);
        return;
    }

    if (size > 64 * 1024) {
        term_set_fg(LUMIE_YELLOW);
        term_write("File too large (");
        char sz[32];
        lumie_itoa(size, sz, 10);
        term_write(sz);
        term_writeln(" bytes). Max 64KB.");
        term_set_fg(LUMIE_WHITE);
        return;
    }

    u32 alloc_sz = size < 4096 ? 4096 : (u32)(size + 1);
    char *buf = (char*)kmalloc(alloc_sz);
    if (!buf) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Out of memory");
        term_set_fg(LUMIE_WHITE);
        return;
    }

    int read = fat_read_file(resolved, buf, alloc_sz - 1);
    if (read < 0) {
        kfree(buf);
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Error reading file");
        term_set_fg(LUMIE_WHITE);
        return;
    }

    buf[read] = 0;
    term_write(buf);
    kfree(buf);
    if (read > 0 && buf[read-1] != '\n') term_writeln("");
}

static int str_contains(const char *str, const char *sub) {
    if (!str || !sub) return 0;
    int sl = lumie_strlen(str), subl = lumie_strlen(sub);
    if (subl > sl) return 0;
    for (int i = 0; i <= sl - subl; i++) {
        int match = 1;
        for (int j = 0; j < subl; j++) {
            char a = str[i+j], b = sub[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

static void cmd_echo(const char *text) {
    if (!text) return;

    if (str_contains(text, "catch") || str_contains(text, "balls")) {
        term_set_fg(LUMIE_LIGHTCYAN);
        term_writeln("  You found the Easter Egg!");
        term_writeln("");
        term_set_fg(LUMIE_YELLOW);
        term_writeln("       ___===___");
        term_writeln("      /  O   O  \\");
        term_set_fg(LUMIE_GREEN);
        term_writeln("     |   \\___/   |");
        term_writeln("      \\  _____  /");
        term_set_fg(LUMIE_RED);
        term_writeln("      /         \\");
        term_set_fg(LUMIE_MAGENTA);
        term_writeln("     |  0     0  |");
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("      \\   U    /   CATCH THE BALLS!");
        term_set_fg(LUMIE_WHITE);
        term_writeln("       \\______/");
        term_writeln("");
        term_set_fg(LUMIE_LIGHTGREEN);
        term_writeln("  Try the game: Catch the Balls!");
        term_set_fg(LUMIE_WHITE);
        return;
    }

    term_writeln(text);
}

static void cmd_info() {
    fb_info *fb = gop_get_fb();
    char buf[64];

    term_set_fg(LUMIE_LIGHTRED);
    term_writeln("      ██╗     ██╗   ██╗███╗   ███╗██╗███████╗ ██████╗ ███████╗");
    term_set_fg(LUMIE_YELLOW);
    term_writeln("      ██║     ██║   ██║████╗ ████║██║██╔════╝██╔═══██╗██╔════╝");
    term_set_fg(LUMIE_LIGHTGREEN);
    term_writeln("      ██║     ██║   ██║██╔████╔██║██║█████╗  ██║   ██║███████╗");
    term_set_fg(LUMIE_LIGHTCYAN);
    term_writeln("      ██║     ██║   ██║██║╚██╔╝██║██║██╔══╝  ██║   ██║╚════██║");
    term_set_fg(LUMIE_LIGHTMAGENTA);
    term_writeln("      ███████╗╚██████╔╝██║ ╚═╝ ██║██║███████╗╚██████╔╝███████║");
    term_set_fg(LUMIE_LIGHTCYAN);
    term_writeln("      ╚══════╝ ╚═════╝ ╚═╝     ╚═╝╚═╝╚══════╝ ╚═════╝ ╚══════╝");
    term_writeln("");

    term_set_fg(LUMIE_LIGHTCYAN);
    term_writeln("  ===== LumieOS Windows Edition =====");
    term_set_fg(LUMIE_WHITE);

    term_set_fg(LUMIE_LIGHTGREEN);
    term_write("  Resolution:   ");
    term_set_fg(LUMIE_WHITE);
    lumie_itoa(fb->width, buf, 10);
    term_write(buf);
    term_write("x");
    lumie_itoa(fb->height, buf, 10);
    term_writeln(buf);

    term_set_fg(LUMIE_LIGHTGREEN);
    term_write("  Framebuffer:  ");
    term_set_fg(LUMIE_WHITE);
    term_write("0x");
    lumie_itoa(fb->base, buf, 16);
    term_write(buf);
    term_write(" (");
    lumie_itoa(fb->size / 1024, buf, 10);
    term_write(buf);
    term_writeln(" KB)");

    term_set_fg(LUMIE_LIGHTGREEN);
    term_write("  Terminal:     ");
    term_set_fg(LUMIE_WHITE);
    lumie_itoa(term_get_width(), buf, 10);
    term_write(buf);
    term_write("x");
    lumie_itoa(term_get_height(), buf, 10);
    term_writeln(buf);

    term_set_fg(LUMIE_LIGHTGREEN);
    term_write("  Free Memory:  ");
    term_set_fg(LUMIE_WHITE);
    u64 free_mem = mm_get_free_mem();
    lumie_itoa((int)(free_mem / 1024), buf, 10);
    term_write(buf);
    term_writeln(" KB");

    term_set_fg(LUMIE_LIGHTCYAN);
    term_writeln("  ===================================");
    term_set_fg(LUMIE_WHITE);
}

static int lumieos_installed(void);

static void cmd_ver() {
    term_set_fg(LUMIE_WHITE);
    term_writeln("LumieOS v0.1 - Windows Edition (64-bit UEFI)");
    if (lumieos_installed()) {
        term_set_fg(LUMIE_LIGHTCYAN);
        term_writeln("Installed on: SSD (FAT32)");
        term_set_fg(LUMIE_WHITE);
    } else {
        term_set_fg(LUMIE_YELLOW);
        term_writeln("Not installed - run installer");
        term_set_fg(LUMIE_WHITE);
    }
}

static int match_name(const char *pattern, const char *name) {
    int plen = lumie_strlen(pattern);
    int nlen = lumie_strlen(name);
    if (plen == 0) return 0;

    if (pattern[0] == '*' && pattern[plen-1] == '*') {
        char mid[256];
        int m = 0;
        for (int i = 1; i < plen - 1 && m < 255; i++) mid[m++] = pattern[i];
        mid[m] = 0;
        return lumie_strstr(name, mid) != NULL;
    }

    if (pattern[0] == '*') {
        if (nlen < plen - 1) return 0;
        return lumie_strcmp(name + nlen - (plen - 1), pattern + 1) == 0;
    }

    if (pattern[plen-1] == '*') {
        return lumie_strncmp(name, pattern, plen - 1) == 0;
    }

    return lumie_strcmp(pattern, name) == 0;
}

#define WHER_STACK_SIZE 256
#define WHER_PATH_MAX 256

static void cmd_wher(const char *dir, const char *pattern) {
    if (!pattern) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Usage: wher <directory> <pattern>");
        term_set_fg(LUMIE_WHITE);
        return;
    }

    char base[WHER_PATH_MAX];
    if (!dir || dir[0] == 0) {
        lumie_strcpy(base, "/");
    } else {
        lumie_strcpy(base, dir);
    }

    char *stack_mem = (char*)kmalloc(WHER_STACK_SIZE * WHER_PATH_MAX);
    if (!stack_mem) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Out of memory");
        term_set_fg(LUMIE_WHITE);
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
            int cur_len = lumie_strlen(cur);
            if (cur_len >= WHER_PATH_MAX) cur_len = WHER_PATH_MAX - 1;
            lumie_memcpy(full, cur, cur_len);
            full[cur_len] = 0;
            int flen = cur_len;
            if (full[flen-1] != '/') {
                if (flen < WHER_PATH_MAX - 1) {
                    full[flen] = '/';
                    full[flen+1] = 0;
                    flen++;
                }
            }
            int nlen = lumie_strlen(entries[i].name);
            if (flen + nlen >= WHER_PATH_MAX) nlen = WHER_PATH_MAX - 1 - flen;
            if (nlen > 0) lumie_memcpy(full + flen, entries[i].name, nlen);
            full[flen + nlen] = 0;

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
                term_set_fg(LUMIE_WHITE);
                term_writeln("");
                found++;
            }
        }
    }

    term_set_fg(LUMIE_WHITE);
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
    term_set_fg(LUMIE_WHITE);
    kfree(stack_mem);
}

static void cmd_wher1(const char *pattern) {
    if (!pattern) {
        term_set_fg(LUMIE_LIGHTRED);
        term_writeln("Usage: wher1 <pattern>");
        term_set_fg(LUMIE_WHITE);
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

/* ===== Check if LumieOS is installed ===== */
static int lumieos_installed(void) {
    return fat_exists("/system/kernel.lkrn");
}

void shell_run() {
    char *argv[MAX_ARGS];
    int show_prompt = 1;

    term_set_fg(LUMIE_LIGHTCYAN);
    term_writeln("LumieOS v0.1 - Windows Edition");
    term_set_fg(LUMIE_WHITE);
    term_writeln("Type 'help' for commands.");
    term_writeln("");

    while (1) {
        /* Update mouse */
        mouse_state ms;
        if (mouse_poll(&ms)) {
            if (mouse_visible) {
                mouse_restore(ms.x - ms.dx, ms.y - ms.dy);
            }
            mouse_draw(ms.x, ms.y);
            mouse_visible = 1;
        } else if (mouse_visible) {
            int mx, my;
            mouse_get_pos(&mx, &my);
            mouse_restore(mx, my);
            mouse_draw(mx, my);
            mouse_visible = 1;
        }

        if (show_prompt) {
            term_set_fg(LUMIE_LIGHTCYAN);
            term_write("lumie");
            term_set_fg(LUMIE_WHITE);
            term_write("@");
            term_set_fg(LUMIE_LIGHTCYAN);
            term_write("windows");
            term_set_fg(LUMIE_WHITE);
            term_write(":");
            term_set_fg(LUMIE_LIGHTCYAN);
            term_write(cwd);
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
        } else if (lumie_strcmp(argv[0], "cd") == 0) {
            cmd_cd(argc > 1 ? argv[1] : NULL);
        } else if (lumie_strcmp(argv[0], "pwd") == 0) {
            cmd_pwd();
        } else if (lumie_strcmp(argv[0], "edit") == 0) {
            if (argc > 1) {
                char resolved[256];
                resolve_path(argv[1], resolved, 256);
                lumie_edit(resolved);
            } else {
                term_set_fg(LUMIE_LIGHTRED);
                term_writeln("Usage: edit <filename>");
                term_set_fg(LUMIE_WHITE);
            }
        } else if (lumie_strcmp(argv[0], "notepad") == 0) {
            cmd_notepad();
        } else if (lumie_strcmp(argv[0], "rm") == 0 || lumie_strcmp(argv[0], "del") == 0) {
            cmd_rm(argc > 1 ? argv[1] : NULL);
        } else if (lumie_strcmp(argv[0], "rmdir") == 0) {
            cmd_rm(argc > 1 ? argv[1] : NULL);
        } else if (lumie_strcmp(argv[0], "mkdir") == 0) {
            cmd_mkdir(argc > 1 ? argv[1] : NULL);
        } else if (lumie_strcmp(argv[0], "ps") == 0) {
            cmd_ps();
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
                term_set_fg(LUMIE_WHITE);
            } else {
                net_renet_download(argc > 1 ? argv[1] : NULL);
            }
        } else {
            term_set_fg(LUMIE_LIGHTRED);
            term_write("Unknown command: ");
            term_writeln(argv[0]);
            term_set_fg(LUMIE_WHITE);
        }
    }
}

void shell_printf(const char *fmt, ...) {
    char buf[1024];
    int pos = 0;
    va_list args;
    va_start(args, fmt);
    for (int i = 0; fmt[i] && pos < 1023; i++) {
        if (fmt[i] != '%') { buf[pos++] = fmt[i]; continue; }
        i++;
        if (!fmt[i]) break;
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
