/* LumieOS - 64-bit UEFI Operating System
 * Main entry point
 */

#include "efi.h"
#include "lumie.h"
#include "gop.h"
#include "keyboard.h"
#include "terminal.h"
#include "fat.h"
#include "shell.h"
#include "editor.h"

/* Global UEFI table references – non-static for net.c, etc. */
efi_system_table    *g_ST           = NULL;
efi_handle           g_ImageHandle  = NULL;
efi_boot_services   *g_BS           = NULL;
static efi_runtime_services *g_RT   = NULL;

/* Stall wrapper - needed by lumie_stall */
void lumie_stall(u64 microseconds) {
    if (g_BS) {
        ((efi_bs_stall)g_BS->Stall)(microseconds);
    }
}

/* lumie.h implementation using our drivers */
void lumie_init(efi_handle image_handle, efi_system_table *system_table) {
    g_ST = system_table;
    g_ImageHandle = image_handle;
    g_BS = system_table->BootServices;
    g_RT = system_table->RuntimeServices;

    /* Clear UEFI console */
    efi_simple_text_output_protocol *con_out = system_table->ConOut;
    if (con_out) {
        con_out->ClearScreen(con_out);
        con_out->EnableCursor(con_out, FALSE);
    }

    /* Initialize GOP framebuffer */
    efi_status status = gop_init(image_handle, system_table);
    if (EFI_ERROR(status)) {
        /* Fallback to UEFI console if GOP fails */
        if (con_out) {
            con_out->OutputString(con_out, L"GOP init failed!\r\n");
        }
        return;
    }

    /* Initialize keyboard */
    kbd_init(system_table);

    /* Initialize terminal */
    term_init();

    /* Initialize FAT */
    fat_set_bs(g_BS, image_handle, system_table);
    fat_init();

    /* Clear screen to black */
    term_clear(LUMIE_BLACK);

    /* Show boot message */
    term_set_fg(LUMIE_CYAN);
    term_write("   _    _                     ___  ___  \n");
    term_write("  | |  | |                    |  \\/  | \n");
    term_write("  | |  | |_ __ ___   ___ ___  | .  . | ___  ___ \n");
    term_write("  | |  | | '_ ` _ \\ / _ / __| | |\\/| |/ _ \\/ _ \\\n");
    term_write("  | |__| | | | | | |  __\\__ \\ | |  | |  __/ (_) |\n");
    term_write("   \\____/|_| |_| |_|\\___|___/ \\_|  |_/\\___|\\___/ \n");
    term_set_fg(LUMIE_GREEN);
    term_writeln("  LumieOS v0.1 - 64-bit UEFI");
    term_writeln("");

    term_set_fg(LUMIE_DARKGRAY);
    term_writeln("Initializing");
    term_writeln("");

    /* Wait a moment */
    lumie_stall(1000000);
}

/* UEFI Application Entry Point */
__attribute__((ms_abi)) efi_status efi_main(efi_handle image_handle, efi_system_table *system_table) {
    /* Initialize the OS */
    lumie_init(image_handle, system_table);

    /* Clear for shell */
    term_clear(LUMIE_BLACK);

    /* Show welcome */
    term_set_fg(LUMIE_GREEN);
    term_writeln("LumieOS started successfully!");
    term_writeln("");

    /* Run the shell */
    shell_run();

    /* Should never reach here */
    return EFI_SUCCESS;
}

/* lumie.h API implementation - delegates to terminal */
void lumie_clear(lumie_color bg) { term_clear(bg); }
void lumie_set_fg(lumie_color c) { term_set_fg(c); }
void lumie_set_bg(lumie_color c) { term_set_bg(c); }
void lumie_set_pos(int x, int y) { term_set_pos(x, y); }
void lumie_putchar(char c) { term_putchar(c); }
void lumie_write(const char *str) { term_write(str); }
void lumie_writeln(const char *str) { term_writeln(str); }
int lumie_getchar() { return kbd_getchar(); }
int lumie_kbhit() { return kbd_kbhit(); }
int lumie_get_width() { return term_get_width(); }
int lumie_get_height() { return term_get_height(); }

int lumie_fs_init() { return fat_init(); }
int lumie_fs_read(const char *path, void *buffer, u32 max_size) { return fat_read_file(path, buffer, max_size); }
int lumie_fs_write(const char *path, const void *data, u32 size) { return fat_write_file(path, data, size); }
int lumie_fs_list(const char *path, lumie_dirent *entries, int max_entries) { return fat_list_dir(path, entries, max_entries); }
int lumie_fs_exists(const char *path) { return fat_exists(path); }
int lumie_edit(const char *filename) { editor_run(filename); return 0; }
void lumie_shell_run() { shell_run(); }

void lumie_reboot() {
    term_writeln("Rebooting...");
    lumie_stall(500000);
    if (g_RT) {
        efi_rt_reset_system reset = (efi_rt_reset_system)g_RT->ResetSystem;
        reset(EfiResetWarm, EFI_SUCCESS, 0, NULL);
    }
}

void lumie_shutdown() {
    term_writeln("Shutting down...");
    lumie_stall(500000);
    if (g_RT) {
        efi_rt_reset_system reset = (efi_rt_reset_system)g_RT->ResetSystem;
        reset(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
    }
}

void lumie_printf(const char *fmt, ...) {
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
