#include "kernel.h"
#include "efi.h"
#include "lumie.h"
#include "gop.h"
#include "keyboard.h"
#include "terminal.h"
#include "fat.h"
#include "shell.h"
#include "editor.h"
#include "mouse.h"
#include "loader.h"
#include "mm.h"
#include "ahci.h"
#include "pit.h"

efi_system_table    *g_ST           = NULL;
efi_handle           g_ImageHandle  = NULL;
efi_boot_services   *g_BS           = NULL;
efi_runtime_services *g_RT          = NULL;

void lumie_stall(u64 microseconds) {
    if (g_BS) {
        ((efi_bs_stall)g_BS->Stall)(microseconds);
    } else {
        while (microseconds >= 1000) {
            pit_stall(1000);
            microseconds -= 1000;
        }
        if (microseconds > 0)
            pit_stall((u32)microseconds);
    }
}

void lumie_init(efi_handle image_handle, efi_system_table *system_table) {
    g_ST = system_table;
    g_ImageHandle = image_handle;
    g_BS = system_table->BootServices;
    g_RT = system_table->RuntimeServices;
}

efi_status efi_main(efi_handle image_handle, efi_system_table *system_table) {
    lumie_loader_start(image_handle, system_table);
    return EFI_SUCCESS;
}

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

void lumie_panic(const char *msg, const char *file, int line) {
    term_clear(LUMIE_BLUE);
    term_set_fg(LUMIE_WHITE);
    term_set_bg(LUMIE_BLUE);

    int rows = term_get_height();

    term_set_pos(2, 2);
    term_set_fg(LUMIE_WHITE);
    term_write("LumieOS");
    term_set_fg(LUMIE_LIGHTCYAN);
    term_writeln(" (Windows Edition)");

    term_set_fg(LUMIE_WHITE);
    term_set_pos(2, 4);
    term_write("A fatal error has occurred.");
    term_set_pos(2, 5);
    term_write("The operating system needs to restart.");

    term_set_pos(2, 7);
    term_write("Error: ");
    term_writeln(msg);

    if (file) {
        term_set_pos(2, 9);
        term_write("At: ");
        term_write(file);
        if (line > 0) {
            char lbuf[32];
            lumie_itoa(line, lbuf, 10);
            term_write(":");
            term_write(lbuf);
        }
    }

    term_set_pos(2, rows - 3);
    term_write("Press any key to restart...");

    kbd_getchar();

    term_clear(LUMIE_BLUE);
    if (g_RT) {
        efi_rt_reset_system reset = (efi_rt_reset_system)g_RT->ResetSystem;
        reset(EfiResetWarm, EFI_SUCCESS, 0, NULL);
    }
}

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

int lumie_efi_register_boot_entry(void) {
    if (!g_BS || !g_RT) return -1;

    efi_guid loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    efi_loaded_image_protocol *loaded_image;
    efi_status status = ((efi_bs_handle_protocol)g_BS->HandleProtocol)(g_ImageHandle, &loaded_image_guid, (void**)&loaded_image);
    if (EFI_ERROR(status) || !loaded_image || !loaded_image->DeviceHandle) return -1;

    efi_guid dp_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
    efi_device_path_protocol *dev_path;
    status = ((efi_bs_handle_protocol)g_BS->HandleProtocol)(loaded_image->DeviceHandle, &dp_guid, (void**)&dev_path);
    if (EFI_ERROR(status) || !dev_path) return -1;

    u16 file_path[] = L"\\EFI\\LumieOS\\BOOTX64.EFI";
    u16 desc[] = L"LumieOS";

    u32 dp_len = 0;
    efi_device_path_protocol *dp = dev_path;
    while (1) {
        if (dp->Type == DEVICE_PATH_TYPE_END && dp->SubType == END_ENTIRE_DEVICE_PATH) {
            dp_len += sizeof(efi_device_path_protocol);
            break;
        }
        dp_len += dp->Length;
        dp = (efi_device_path_protocol*)((u8*)dp + dp->Length);
    }

    u32 fp_chars = 0;
    while (file_path[fp_chars]) fp_chars++;
    u32 filepath_node_len = sizeof(efi_device_path_protocol) + (fp_chars + 1) * 2;
    if (filepath_node_len & 1) filepath_node_len++;
    u32 full_dp_len = dp_len + filepath_node_len + sizeof(efi_device_path_protocol);

    u32 d_chars = 0;
    while (desc[d_chars]) d_chars++;
    u32 desc_len = (d_chars + 1) * 2;
    u32 option_size = sizeof(efi_load_option) + desc_len + full_dp_len;
    u8 *option_data = NULL;
    status = ((efi_bs_allocate_pool)g_BS->AllocatePool)(EFI_BOOT_SERVICES_DATA, option_size, (void**)&option_data);
    if (EFI_ERROR(status) || !option_data) return -1;
    lumie_memset(option_data, 0, option_size);

    efi_load_option *option = (efi_load_option*)option_data;
    option->Attributes = LOAD_OPTION_ACTIVE | LOAD_OPTION_CATEGORY_APP;
    option->FilePathListLength = full_dp_len;

    u8 *ptr = option_data + sizeof(efi_load_option);
    lumie_memcpy(ptr, desc, desc_len);
    ptr += desc_len;

    lumie_memcpy(ptr, dev_path, dp_len);
    ptr += dp_len;

    efi_device_path_protocol *fp_node = (efi_device_path_protocol*)ptr;
    fp_node->Type = DEVICE_PATH_TYPE_MEDIA;
    fp_node->SubType = MEDIA_FILEPATH_DP;
    fp_node->Length = filepath_node_len;
    lumie_memcpy(ptr + sizeof(efi_device_path_protocol), file_path, filepath_node_len - sizeof(efi_device_path_protocol));
    ptr += filepath_node_len;

    efi_device_path_protocol *end_node = (efi_device_path_protocol*)ptr;
    end_node->Type = DEVICE_PATH_TYPE_END;
    end_node->SubType = END_ENTIRE_DEVICE_PATH;
    end_node->Length = sizeof(efi_device_path_protocol);

    efi_guid global_guid = EFI_GLOBAL_VARIABLE_GUID;

    u16 boot_order_buf[128];
    u64 boot_order_size = sizeof(boot_order_buf);
    u32 bo_attrs;
    u16 *boot_order = NULL;
    u64 existing_count = 0;
    status = ((efi_rt_get_variable)g_RT->GetVariable)(L"BootOrder", &global_guid, &bo_attrs, &boot_order_size, boot_order_buf);
    if (!EFI_ERROR(status)) {
        existing_count = boot_order_size / 2;
        if (existing_count > 0) {
            boot_order = boot_order_buf;
        }
    }

    u16 new_boot_num = 0;
    for (u16 candidate = 0; candidate < 0xFF; candidate++) {
        u16 name_buf[9];
        name_buf[0] = 'B'; name_buf[1] = 'o'; name_buf[2] = 'o'; name_buf[3] = 't';
        u8 hex_digits[] = "0123456789ABCDEF";
        name_buf[4] = hex_digits[(candidate >> 12) & 0xF];
        name_buf[5] = hex_digits[(candidate >> 8) & 0xF];
        name_buf[6] = hex_digits[(candidate >> 4) & 0xF];
        name_buf[7] = hex_digits[candidate & 0xF];
        name_buf[8] = 0;

        u64 size = 0;
        efi_status gs = ((efi_rt_get_variable)g_RT->GetVariable)(name_buf, &global_guid, NULL, &size, NULL);
        if (EFI_ERROR(gs) && (gs == EFI_NOT_FOUND || size == 0)) {
            new_boot_num = candidate;
            break;
        }
    }

    u16 bootvar_name[9] = {0};
    bootvar_name[0] = 'B'; bootvar_name[1] = 'o'; bootvar_name[2] = 'o'; bootvar_name[3] = 't';
    u8 hex[] = "0123456789ABCDEF";
    bootvar_name[4] = hex[(new_boot_num >> 12) & 0xF];
    bootvar_name[5] = hex[(new_boot_num >> 8) & 0xF];
    bootvar_name[6] = hex[(new_boot_num >> 4) & 0xF];
    bootvar_name[7] = hex[new_boot_num & 0xF];
    bootvar_name[8] = 0;

    status = ((efi_rt_set_variable)g_RT->SetVariable)(bootvar_name, &global_guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
        option_size, option_data);
    ((efi_bs_free_pool)g_BS->FreePool)(option_data);
    if (EFI_ERROR(status)) return -1;

    u16 new_boot_order_buf[129];
    new_boot_order_buf[0] = new_boot_num;
    u64 new_boot_order_size = 2;
    if (boot_order && existing_count > 0) {
        if (existing_count > 128) existing_count = 128;
        lumie_memcpy(new_boot_order_buf + 1, boot_order, existing_count * 2);
        new_boot_order_size = (existing_count + 1) * 2;
    }

    status = ((efi_rt_set_variable)g_RT->SetVariable)(L"BootOrder", &global_guid,
        EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
        new_boot_order_size, new_boot_order_buf);

    return EFI_ERROR(status) ? -1 : 0;
}

void exit_boot_services(void) {
    if (!g_BS) return;

    efi_bs_exit_boot_services ebs = (efi_bs_exit_boot_services)g_BS->ExitBootServices;
    efi_status status;
    u64 map_key = mm_get_map_key();
    u64 desc_size = mm_get_desc_size();
    u32 desc_ver = mm_get_desc_ver();

    int retries = 3;
    while (retries--) {
        status = ebs(g_ImageHandle, map_key);
        if (!EFI_ERROR(status)) { g_BS = NULL; break; }

        u64 mmap_size = 0;
        efi_memory_descriptor *mmap_buf = NULL;
        u64 new_key = 0;

        typedef efi_status (*bs_get_memory_map_t)(u64*, efi_memory_descriptor*, u64*, u64*, u32*);
        bs_get_memory_map_t get_mmap = (bs_get_memory_map_t)g_BS->GetMemoryMap;

        mmap_size = 0;
        get_mmap(&mmap_size, NULL, &new_key, &desc_size, &desc_ver);
        mmap_size += desc_size * 16;

        efi_status st = ((efi_bs_allocate_pool)g_BS->AllocatePool)(EFI_BOOT_SERVICES_DATA, mmap_size, (void**)&mmap_buf);
        if (EFI_ERROR(st) || !mmap_buf) break;

        st = get_mmap(&mmap_size, mmap_buf, &new_key, &desc_size, &desc_ver);
        if (EFI_ERROR(st)) { break; }

        map_key = new_key;
    }
    g_BS = NULL;
}

int lumie_ps2_available(void) {
    return 1;
}

void lumie_printf(const char *fmt, ...) {
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
