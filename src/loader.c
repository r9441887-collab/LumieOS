#include "loader.h"
#include "efi.h"
#include "lumie.h"
#include "gop.h"
#include "keyboard.h"
#include "mouse.h"
#include "terminal.h"
#include "fat.h"
#include "shell.h"
#include "kernel.h"
#include "mm.h"
#include "ahci.h"
#include "ps2kbd.h"
#include "pit.h"
#include "drivembeds.h"

static efi_system_table *ld_st = NULL;
static efi_simple_text_input_protocol *ld_con_in = NULL;
static efi_boot_services *ld_bs = NULL;
static efi_simple_pointer_protocol *ld_pointer = NULL;

static int ld_prev_buttons = 0;
static int ld_click_x = -1;
static int ld_click_y = -1;

static u32 ld_make_color(u8 r, u8 g, u8 b) {
    fb_info *fb = gop_get_fb();
    if (fb && fb->pixel_format == 0)
        return r | (g << 8) | (b << 16);
    return (r << 16) | (g << 8) | b;
}

void loader_drv_clear(u32 color) {
    u32 w = gop_get_width();
    u32 h = gop_get_height();
    gop_fill_rect(0, 0, w, h, color);
}

void loader_drv_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
    gop_fill_rect(x, y, w, h, color);
}

void loader_drv_draw_str(u32 x, u32 y, u32 fg, u32 bg, const char *str) {
    gop_draw_string(x, y, fg, bg, str);
}

void loader_drv_progress(u32 x, u32 y, u32 w, u32 h, u32 fg, u32 bg, int pct) {
    if (pct > 100) pct = 100;
    if (pct < 0) pct = 0;
    gop_fill_rect(x, y, w, h, bg);
    if (pct > 0) {
        int filled = (w * pct) / 100;
        if (filled < 1) filled = 1;
        gop_fill_rect(x, y, filled, h, fg);
    }
}

void loader_kbd_init(efi_system_table *st) {
    if (!st) return;
    ld_st = st;
    ld_con_in = st->ConIn;
    ld_bs = st->BootServices;
}

int loader_kbhit(void) {
    if (!ld_con_in) return 0;
    efi_input_key key;
    return ld_con_in->ReadKeyStroke(ld_con_in, &key) == EFI_SUCCESS;
}

int loader_getchar(void) {
    if (!ld_con_in || !ld_bs) return 0;
    while (1) {
        efi_input_key key;
        efi_status st = ld_con_in->ReadKeyStroke(ld_con_in, &key);
        if (!EFI_ERROR(st)) {
            if (key.UnicodeChar == 0x0D) return '\n';
            if (key.UnicodeChar == 0x08) return '\b';
            if (key.UnicodeChar >= 0x01 && key.UnicodeChar <= 0x7E)
                return (char)key.UnicodeChar;
            if (key.ScanCode >= 0x01 && key.ScanCode <= 0x0B)
                return 0xE0 + key.ScanCode;
        } else {
            if (ld_con_in->WaitForKey) {
                u64 index;
                ((efi_bs_wait_for_event)ld_bs->WaitForEvent)(1, &ld_con_in->WaitForKey, &index);
            } else {
                ((efi_bs_stall)ld_bs->Stall)(1000);
            }
        }
    }
}

void loader_mouse_init(efi_system_table *st) {
    if (!st || !st->BootServices) return;
    efi_boot_services *bs = st->BootServices;
    efi_guid pointer_guid = EFI_SIMPLE_POINTER_PROTOCOL_GUID;
    efi_status status = ((efi_bs_locate_protocol)bs->LocateProtocol)(&pointer_guid, NULL, (void**)&ld_pointer);
    if (!EFI_ERROR(status) && ld_pointer) {
        typedef efi_status (*ptr_reset)(void*, u8);
        ((ptr_reset)ld_pointer->Reset)(ld_pointer, FALSE);
    }
}

int loader_mouse_poll(int *dx, int *dy, u8 *buttons) {
    if (!ld_pointer) return 0;
    efi_simple_pointer_state ps;
    lumie_memset(&ps, 0, sizeof(ps));
    efi_status st = ld_pointer->GetState(ld_pointer, &ps);
    if (EFI_ERROR(st)) return 0;
    if (ps.RelativeMovementX == 0 && ps.RelativeMovementY == 0 && ps.Buttons == 0)
        return 0;
    if (dx) *dx = (int)(ps.RelativeMovementX >> 8);
    if (dy) *dy = (int)(ps.RelativeMovementY >> 8);
    if (buttons) {
        *buttons = 0;
        if (ps.Buttons & EFI_SIMPLE_POINTER_LEFT_BUTTON) *buttons |= 1;
        if (ps.Buttons & EFI_SIMPLE_POINTER_RIGHT_BUTTON) *buttons |= 2;
    }
    return 1;
}

static u32 cursor_bg[16][16];
static int cursor_x, cursor_y;

static const u8 cursor_bits[16][2] = {
    {0x80,0x00},{0xC0,0x00},{0xE0,0x00},{0xF0,0x00},
    {0xF8,0x00},{0xFC,0x00},{0xFE,0x00},{0xFF,0x00},
    {0xFF,0x80},{0xFE,0xC0},{0xFC,0xE0},{0xF0,0x70},
    {0xE0,0x38},{0xC0,0x1C},{0x80,0x0E},{0x00,0x04},
};

static void loader_cursor_restore(void) {
    u32 w = gop_get_width();
    u32 h = gop_get_height();
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 16; col++) {
            int px = cursor_x + col;
            int py = cursor_y + row;
            if (px < 0 || px >= (int)w || py < 0 || py >= (int)h) continue;
            gop_put_pixel(px, py, cursor_bg[row][col]);
        }
    }
}

static void loader_cursor_draw(void) {
    u32 w = gop_get_width();
    u32 h = gop_get_height();
    u32 blue = ld_make_color(0x00, 0x55, 0xFF);
    u32 white = ld_make_color(0xFF, 0xFF, 0xFF);
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 16; col++) {
            int px = cursor_x + col;
            int py = cursor_y + row;
            if (px < 0 || px >= (int)w || py < 0 || py >= (int)h) continue;
            cursor_bg[row][col] = gop_get_pixel(px, py);
            u16 bits = ((u16)cursor_bits[row][0] << 8) | cursor_bits[row][1];
            if (bits & (0x8000 >> col)) {
                u32 color = (row == 0 || col == 0) ? white : blue;
                gop_put_pixel(px, py, color);
            }
        }
    }
}

static void loader_poll_mouse(void) {
    int dx = 0, dy = 0;
    u8 btns = 0;
    if (loader_mouse_poll(&dx, &dy, &btns)) {
        loader_cursor_restore();
        cursor_x += dx;
        cursor_y += dy;
        if (cursor_x < 0) cursor_x = 0;
        if (cursor_y < 0) cursor_y = 0;
        if (cursor_x >= (int)gop_get_width()) cursor_x = (int)gop_get_width() - 1;
        if (cursor_y >= (int)gop_get_height()) cursor_y = (int)gop_get_height() - 1;
        loader_cursor_draw();
        /* Detect left button click (transition 0→1) */
        if ((btns & 1) && !(ld_prev_buttons & 1)) {
            ld_click_x = cursor_x;
            ld_click_y = cursor_y;
        } else {
            ld_click_x = -1;
            ld_click_y = -1;
        }
        ld_prev_buttons = btns;
    }
}

static int loader_get_click(int *x, int *y) {
    if (ld_click_x >= 0 && ld_click_y >= 0) {
        if (x) *x = ld_click_x;
        if (y) *y = ld_click_y;
        ld_click_x = -1;
        ld_click_y = -1;
        return 1;
    }
    return 0;
}

static int lumieos_installed(void) {
    return fat_exists("/system/kernel.lkrn");
}



int loader_enum_block_devices(loader_block_device *devices, int max_devices) {
    efi_guid block_io_guid = EFI_BLOCK_IO_GUID;
    u64 handle_count = 0;
    efi_handle *handles = NULL;
    efi_status status = ((efi_bs_locate_handle_buffer)g_BS->LocateHandleBuffer)(
        EFI_LOCATE_BY_PROTOCOL, &block_io_guid, NULL, &handle_count, &handles);
    if (EFI_ERROR(status) || !handles) return 0;

    int count = 0;
    for (u64 i = 0; i < handle_count && count < max_devices; i++) {
        efi_block_io_protocol *bio = NULL;
        status = ((efi_bs_handle_protocol)g_BS->HandleProtocol)(handles[i], &block_io_guid, (void**)&bio);
        if (EFI_ERROR(status) || !bio || !bio->Media) continue;

        /* Check if media is present */
        if (!bio->Media->MediaPresent) {
            if (!bio->Media->RemovableMedia) continue;
        }

        devices[count].handle = handles[i];
        devices[count].block_count = (bio->Media->LastBlock + 1) - (bio->Media->LowestAlignedLba);
        devices[count].block_size = bio->Media->BlockSize ? bio->Media->BlockSize : 512;
        devices[count].is_removable = bio->Media->RemovableMedia ? 1 : 0;
        devices[count].is_partition = bio->Media->LogicalPartition ? 1 : 0;

        /* Detect FAT32 by reading BPB sector 0 */
        int is_fat32 = 0;
        u8 sector[512];
        efi_status rs = bio->ReadBlocks(bio, bio->Media->MediaId, 0, 512, sector);
        if (!EFI_ERROR(rs)) {
            u32 spf = *(u32*)(sector + 36);
            if (spf != 0) is_fat32 = 1;
        }

        lumie_strcpy(devices[count].label, devices[count].is_removable ? "USB Flash" : "SSD");
        if (devices[count].is_partition) {
            if (is_fat32) {
                char vol[12];
                lumie_memcpy(vol, sector + 43, 11);
                vol[11] = 0;
                lumie_strcat(devices[count].label, " [FAT32: ");
                lumie_strcat(devices[count].label, vol);
                lumie_strcat(devices[count].label, "]");
            } else {
                lumie_strcat(devices[count].label, " (Partition)");
            }
        } else if (devices[count].is_removable) {
            lumie_strcat(devices[count].label, " (Removable)");
        } else {
            lumie_strcat(devices[count].label, " (Fixed)");
        }

        count++;
    }

    if (handles) ((efi_bs_free_pool)g_BS->FreePool)(handles);
    return count;
}

static int loader_show_device_menu(loader_block_device *devices, int count) {
    u32 bg = ld_make_color(0x00, 0x00, 0x80);
    u32 white = ld_make_color(0xFF, 0xFF, 0xFF);
    u32 lcyan = ld_make_color(0x55, 0xFF, 0xFF);
    u32 yellow = ld_make_color(0xFF, 0xFF, 0x00);

    u32 scr_w = gop_get_width();
    u32 scr_h = gop_get_height();
    int logo_y = scr_h / 5;
    int line_h = 20;

    int sel = 0;
    int page_offset = 0;
    int max_visible = (scr_h - logo_y - 60) / line_h;
    if (max_visible < 1) max_visible = 1;

    while (1) {
        loader_drv_clear(bg);

        loader_drv_draw_str(scr_w/2 - 4*8, logo_y, lcyan, bg, "LumieOS");
        loader_drv_draw_str(scr_w/2 - 10*8, logo_y + line_h, white, bg, "Select Boot Device:");

        int y = logo_y + 3 * line_h;

        if (page_offset > 0)
            loader_drv_draw_str(scr_w/2 - 5*8, y - line_h, yellow, bg, "[more above]");

        for (int i = page_offset; i < count && i < page_offset + max_visible; i++) {
            char buf[128];
            lumie_strcpy(buf, "  ");
            if (i == sel) lumie_strcat(buf, "> ");
            else lumie_strcat(buf, "  ");

            char num[8];
            lumie_itoa(i + 1, num, 10);
            lumie_strcat(buf, num);
            lumie_strcat(buf, ". ");
            lumie_strcat(buf, devices[i].label);

            char sz[32];
            u64 total_mb = (devices[i].block_count * devices[i].block_size) / (1024 * 1024);
            lumie_itoa((int)total_mb, sz, 10);
            lumie_strcat(buf, " (");
            lumie_strcat(buf, sz);
            lumie_strcat(buf, " MB)");

            u32 color = (i == sel) ? yellow : white;
            loader_drv_draw_str(scr_w/4, y, color, bg, buf);
            y += line_h;
        }

        if (page_offset + max_visible < count)
            loader_drv_draw_str(scr_w/4, y, yellow, bg, "  [more below]");

        loader_drv_draw_str(scr_w/4, scr_h - 3 * line_h, white, bg,
            "ENTER: select   ESC: cancel   UP/DOWN: navigate");

        /* Handle input */
        while (1) {
            loader_poll_mouse();
            int cx, cy;
            if (loader_get_click(&cx, &cy)) {
                int y = logo_y + 3 * line_h;
                for (int i = page_offset; i < count && i < page_offset + max_visible; i++) {
                    if (cy >= y && cy < y + line_h && (u32)cx >= scr_w/4 && (u32)cx < scr_w/4 + 50*8) {
                        sel = i;
                        return sel;
                    }
                    y += line_h;
                }
            }
            if (!loader_kbhit()) continue;
            int c = loader_getchar();
            if (c == '\n') return sel;
            if (c == 0x1B) return -1;
            if (c == 0xE2) { /* up */
                if (sel > 0) sel--;
                if (sel < page_offset) page_offset = sel;
                break;
            }
            if (c == 0xE3) { /* down */
                if (sel < count - 1) sel++;
                if (sel >= page_offset + max_visible) page_offset = sel - max_visible + 1;
                break;
            }
        }
    }
}

void loader_boot_screen(void) {
    u32 bg = ld_make_color(0x00, 0x00, 0x80);
    u32 white = ld_make_color(0xFF, 0xFF, 0xFF);
    u32 cyan = ld_make_color(0x00, 0xFF, 0xFF);
    u32 lcyan = ld_make_color(0x55, 0xFF, 0xFF);
    u32 dkcyan = ld_make_color(0x00, 0x88, 0x88);
    u32 green = ld_make_color(0x00, 0xCC, 0x00);
    u32 yellow = ld_make_color(0xFF, 0xFF, 0x00);
    u32 red = ld_make_color(0xFF, 0x00, 0x00);

    u32 scr_w = gop_get_width();
    u32 scr_h = gop_get_height();

    cursor_x = scr_w / 2;
    cursor_y = scr_h / 2;

    loader_drv_clear(bg);

    int logo_y = scr_h / 5;
    int line_h = 20;

    /* === Boot Menu === */
    loader_drv_draw_str(scr_w/2 - 4*8, logo_y, lcyan, bg, "LumieOS");
    loader_drv_draw_str(scr_w/2 - 8*8, logo_y + line_h, white, bg, "(Windows Edition)");

    int menu_y = logo_y + 3 * line_h;
    int installed = lumieos_installed();

    loader_drv_draw_str(scr_w/4, menu_y, white, bg, "1. Boot LumieOS from current drive");
    loader_drv_draw_str(scr_w/4, menu_y + line_h, white, bg, "2. Install LumieOS");
    loader_drv_draw_str(scr_w/4, menu_y + 2*line_h, white, bg, "3. Boot from another device");
    loader_drv_draw_str(scr_w/4, menu_y + 3*line_h, red, bg, "4. Boot from USB (stay on this device)");

    loader_drv_draw_str(scr_w/4, scr_h - 3*line_h, yellow, bg,
        installed ? "Press 1-4 to select, or wait to boot automatically"
                  : "Press 2 to install, or 4 to boot from USB");

    /* Wait for input with timeout */
    int easter_egg_cb = 0;
    int choice = 4;
    for (int t = 0; t < 100; t++) {
        loader_poll_mouse();
        int cx, cy;
        if (loader_get_click(&cx, &cy)) {
            int item_y = menu_y;
            for (int i = 1; i <= 4; i++) {
                if (cy >= item_y && cy < item_y + line_h && (u32)cx >= scr_w/4 && (u32)cx < scr_w/4 + 40*8) {
                    choice = i;
                    goto choice_done;
                }
                item_y += line_h;
            }
        }
        if (loader_kbhit()) {
            int c = loader_getchar();
            if (c == '1') { choice = 1; break; }
            if (c == '2') { choice = 2; break; }
            if (c == '3') { choice = 3; break; }
            if (c == '4') { choice = 4; break; }
            if (c == 'c' || c == 'C') easter_egg_cb = 1;
        }
        lumie_stall(50000);
    }
    choice_done:

    loader_cursor_restore();

    if (choice == 2) {
        /* Install */
        loader_drv_clear(bg);
        loader_install_screen();
        return;
    }

    if (choice == 3) {
        /* Show device menu */
        loader_block_device devices[16];
        int dev_count = loader_enum_block_devices(devices, 16);
        if (dev_count > 0) {
            int sel = loader_show_device_menu(devices, dev_count);
            if (sel >= 0 && sel < dev_count) {
                if (fat_set_device(devices[sel].handle) == 0) {
                    if (lumieos_installed()) {
                        /* Boot from selected device */
                        loader_drv_clear(bg);
                        loader_drv_draw_str(scr_w/2 - 10*8, scr_h/2, green, bg,
                            "Booting from selected device...");
                        lumie_stall(1000000);
                        term_clear(LUMIE_BLUE);
                        term_set_bg(LUMIE_BLUE);
                        term_set_fg(LUMIE_WHITE);
                        shell_run();
                        return;
                    } else {
                        loader_drv_clear(bg);
                        loader_drv_draw_str(scr_w/2 - 12*8, scr_h/2, yellow, bg,
                            "LumieOS not found on selected device.");
                        loader_drv_draw_str(scr_w/4, scr_h/2 + 24, white, bg,
                            "Press any key to return...");
                        loader_getchar();
                        loader_drv_clear(bg);
                        loader_boot_screen();
                        return;
                    }
                }
            }
        }
        loader_drv_clear(bg);
        loader_boot_screen();
        return;
    }

    /* Choices 1 or 4: boot from current device */
    if (!installed) {
        loader_drv_clear(bg);
        loader_drv_draw_str(scr_w/2 - 16*8, scr_h/3, yellow, bg,
            "LumieOS not found on this device.");
        loader_drv_draw_str(scr_w/2 - 16*8, scr_h/3 + line_h, white, bg,
            "Press 2 to install LumieOS, or");
        loader_drv_draw_str(scr_w/2 - 16*8, scr_h/3 + 2*line_h, white, bg,
            "3 to select another boot device.");
        loader_drv_draw_str(scr_w/2 - 12*8, scr_h/2, yellow, bg,
            "Press any key to return...");
        loader_getchar();
        loader_drv_clear(bg);
        return;
    }

    int bar_y = logo_y + 3 * line_h;
    int bar_w = scr_w / 2;
    int bar_x = (scr_w - bar_w) / 2;
    int status_y = bar_y + 24;

    int pct = 0;
    int phase = 0;
    const char *phases[] = {
        "Checking filesystem...",
        "Loading keyboard driver...",
        "Loading mouse driver...",
        "Loading filesystem driver...",
        "Loading kernel...",
        "Loading shell...",
        NULL
    };

    while (pct < 100) {
        loader_poll_mouse();

        pct += 1;
        if (pct > 100) pct = 100;

        int new_phase = (pct * 6) / 100;
        if (new_phase > 5) new_phase = 5;
        if (new_phase != phase) {
            phase = new_phase;
                    if (phases[phase]) {
                        char status[128];
                        if (phase == 3 && easter_egg_cb) {
                            lumie_strcpy(status, "Catching balls...");
                        } else {
                            lumie_strcpy(status, phases[phase]);
                        }

                        char sz[16];
                        lumie_itoa(pct, sz, 10);
                        lumie_strcat(status, " (");
                        lumie_strcat(status, sz);
                        lumie_strcat(status, "%)");

                        loader_drv_fill_rect(bar_x, status_y, bar_w, 16, bg);
                        loader_drv_draw_str(bar_x, status_y, cyan, bg, status);

                    if (phase == 3 && easter_egg_cb) {
                        loader_drv_draw_str(scr_w/2 + 40, status_y, yellow, bg, "[CATCH!]");
                    } else {
                        switch (phase) {
                            case 1:
                                if (fat_exists("/drivers/kbd.ldrv"))
                                    loader_drv_draw_str(scr_w/2 + 40, status_y, green, bg, "[OK]");
                                else
                                    loader_drv_draw_str(scr_w/2 + 40, status_y, yellow, bg, "[built-in]");
                                break;
                            case 2:
                                if (fat_exists("/drivers/mouse.ldrv"))
                                    loader_drv_draw_str(scr_w/2 + 40, status_y, green, bg, "[OK]");
                                else
                                    loader_drv_draw_str(scr_w/2 + 40, status_y, yellow, bg, "[built-in]");
                                break;
                            case 3:
                                if (fat_exists("/drivers/fs.ldrv"))
                                    loader_drv_draw_str(scr_w/2 + 40, status_y, green, bg, "[OK]");
                                else
                                    loader_drv_draw_str(scr_w/2 + 40, status_y, yellow, bg, "[built-in]");
                                break;
                            case 4:
                                if (fat_exists("/system/kernel.lkrn"))
                                    loader_drv_draw_str(scr_w/2 + 40, status_y, green, bg, "[OK]");
                                else
                                    loader_drv_draw_str(scr_w/2 + 40, status_y, yellow, bg, "[not found]");
                                break;
                            case 5:
                                if (fat_exists("/system/shell.lsh"))
                                    loader_drv_draw_str(scr_w/2 + 40, status_y, green, bg, "[OK]");
                                else
                                    loader_drv_draw_str(scr_w/2 + 40, status_y, yellow, bg, "[built-in]");
                                break;
                        }
                    }
            }
        }

        loader_drv_progress(bar_x, bar_y, bar_w, 16, green, dkcyan, pct);

        if (loader_kbhit()) {
            int c = loader_getchar();
            (void)c;
            loader_drv_fill_rect(bar_x, status_y, bar_w, 16, bg);
            loader_drv_draw_str(bar_x, status_y, yellow, bg, "Bypassing boot screen...");
            break;
        }

        lumie_stall(20000);
    }

    loader_cursor_restore();
    loader_drv_fill_rect(bar_x, bar_y, bar_w, 16, bg);
    loader_drv_fill_rect(bar_x, status_y, bar_w, 16, bg);
}

void loader_install_screen(void) {
    u32 bg = ld_make_color(0x00, 0x00, 0x80);
    u32 white = ld_make_color(0xFF, 0xFF, 0xFF);
    u32 cyan = ld_make_color(0x00, 0xFF, 0xFF);
    u32 lcyan = ld_make_color(0x55, 0xFF, 0xFF);
    u32 green = ld_make_color(0x00, 0xCC, 0x00);
    u32 yellow = ld_make_color(0xFF, 0xFF, 0x00);

    u32 scr_w = gop_get_width();
    u32 scr_h = gop_get_height();

    cursor_x = scr_w / 2;
    cursor_y = scr_h / 2;

    int logo_y = scr_h / 4;
    int line_h = 20;

    loader_drv_draw_str(scr_w/2 - 4*8, logo_y, lcyan, bg, "LumieOS");
    loader_drv_draw_str(scr_w/2 - 8*8, logo_y + line_h, white, bg, "(Windows Edition)");

    /* Device selection */
    loader_block_device install_devices[16];
    int dev_count = loader_enum_block_devices(install_devices, 16);
    int target_device = -1;

    if (dev_count == 0) {
        loader_drv_draw_str(scr_w/2 - 14*8, logo_y + 3*line_h, yellow, bg,
            "No block devices found. Press any key to return.");
        loader_getchar();
        loader_cursor_restore();
        loader_drv_clear(bg);
        return;
    }

    /* If only one device, use it. If multiple, show menu. */
    if (dev_count == 1) {
        target_device = 0;
    } else {
        loader_drv_draw_str(scr_w/2 - 10*8, logo_y + 3*line_h, white, bg,
            "Select installation target device:");
        target_device = loader_show_device_menu(install_devices, dev_count);
        loader_cursor_restore();
        loader_drv_clear(bg);
        if (target_device < 0) return;
    }

    /* Switch FAT to the target device */
    loader_cursor_restore();
    loader_drv_clear(bg);
    loader_drv_draw_str(scr_w/2 - 12*8, logo_y, white, bg,
        "Preparing installation target...");
    lumie_stall(500000);

    if (fat_set_device(install_devices[target_device].handle) != 0) {
        loader_drv_draw_str(scr_w/2 - 14*8, logo_y + 2*line_h, yellow, bg,
            "Failed to access target device. Press any key.");
        loader_getchar();
        loader_cursor_restore();
        loader_drv_clear(bg);
        return;
    }

    loader_drv_clear(bg);
    loader_drv_draw_str(scr_w/2 - 10*8, logo_y, white, bg, "Installing LumieOS...");

    /* Install drivers and kernel from embedded data */
    loader_drv_clear(bg);
    loader_drv_draw_str(scr_w/2 - 10*8, logo_y, white, bg, "Installing LumieOS...");

    int bar_y = logo_y + 2 * line_h;
    int bar_w = scr_w / 2;
    int bar_x = (scr_w - bar_w) / 2;
    int status_y = bar_y + 24;

    int drv_count = DRV_EMBED_COUNT;
    int total_steps = drv_count + 2;
    int step = 0;

    /* Create directories */
    if (!fat_exists("/system")) fat_mkdir("/system");
    if (!fat_exists("/drivers")) fat_mkdir("/drivers");

    /* Install kernel */
    loader_drv_draw_str(bar_x, status_y, cyan, bg, "Installing kernel...");
    loader_drv_progress(bar_x, bar_y, bar_w, 16, green, bg, 0);
    loader_poll_mouse();

    efi_guid loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    efi_loaded_image_protocol *loaded_image;
    efi_status lstatus = ((efi_bs_handle_protocol)g_BS->HandleProtocol)(
        g_ImageHandle, &loaded_image_guid, (void**)&loaded_image);
    if (!EFI_ERROR(lstatus) && loaded_image && loaded_image->ImageBase && loaded_image->ImageSize > 0) {
        u8 *packed = NULL;
        u32 packed_sz = 0;
        if (lumie_pack_module(loaded_image->ImageBase, (u32)loaded_image->ImageSize,
            LUMIE_MAGIC_LKRN, 0, "LumieOS Kernel", &packed, &packed_sz) == 0 && packed) {
            fat_write_file("/system/kernel.lkrn", packed, packed_sz);
            ((efi_bs_free_pool)g_BS->FreePool)(packed);
        }
    }
    step++;
    loader_drv_progress(bar_x, bar_y, bar_w, 16, green, bg, (step * 100) / total_steps);

    /* Install drivers */
    for (int i = 0; i < drv_count; i++) {
        loader_poll_mouse();

        char drv_path[64];
        int is_shell = (lumie_strcmp(drv_embed_table[i].name, "sh") == 0);

        if (is_shell) {
            lumie_strcpy(drv_path, "/system/shell.lsh");
        } else {
            lumie_strcpy(drv_path, "/drivers/");
            lumie_strcat(drv_path, drv_embed_table[i].name);
            lumie_strcat(drv_path, ".ldrv");
        }

        u32 magic = is_shell ? LUMIE_MAGIC_LSH : LUMIE_MAGIC_LDRV;
        u8 *packed = NULL;
        u32 packed_sz = 0;
        if (lumie_pack_module(drv_embed_table[i].data, drv_embed_table[i].size,
            magic, drv_embed_table[i].subtype,
            drv_embed_table[i].name, &packed, &packed_sz) == 0 && packed) {
            fat_write_file(drv_path, packed, packed_sz);
            ((efi_bs_free_pool)g_BS->FreePool)(packed);
        }

        loader_drv_fill_rect(bar_x, status_y, bar_w, 16, bg);
        char step_text[64];
        lumie_strcpy(step_text, "Installing driver: ");
        lumie_strcat(step_text, drv_embed_table[i].name);
        if (is_shell) lumie_strcat(step_text, " (shell)");
        loader_drv_draw_str(bar_x, status_y, cyan, bg, step_text);

        step++;
        loader_drv_progress(bar_x, bar_y, bar_w, 16, green, bg, (step * 100) / total_steps);
        lumie_stall(50000);
    }

    /* Install bootloader */
    loader_poll_mouse();
    loader_drv_fill_rect(bar_x, status_y, bar_w, 16, bg);
    loader_drv_draw_str(bar_x, status_y, cyan, bg, "Installing bootloader...");
    fat_install_bootloader();
    lumie_efi_register_boot_entry();

    loader_drv_progress(bar_x, bar_y, bar_w, 16, green, bg, 100);
    loader_drv_fill_rect(bar_x, status_y + 24, bar_w, 16, bg);
    loader_drv_draw_str(bar_x, status_y + 24, green, bg, "Installation complete! Press any key to continue...");

    while (1) {
        loader_poll_mouse();
        if (loader_kbhit()) {
            loader_getchar();
            break;
        }
    }

    loader_cursor_restore();
    loader_drv_clear(bg);
}

void lumie_loader_start(efi_handle image_handle, efi_system_table *system_table) {
    g_ST = system_table;
    g_ImageHandle = image_handle;
    g_BS = system_table->BootServices;
    g_RT = system_table->RuntimeServices;

    efi_simple_text_output_protocol *con_out = system_table->ConOut;
    if (con_out) {
        con_out->ClearScreen(con_out);
        con_out->EnableCursor(con_out, FALSE);
    }

    efi_status status = gop_init(image_handle, system_table);
    if (EFI_ERROR(status)) {
        if (con_out)
            con_out->OutputString(con_out, L"GOP init failed!\r\n");
        return;
    }

    /* Initialize our own subsystems while UEFI Boot Services are still available */
    mm_init(g_BS, image_handle);
    ahci_init();
    pit_init(1000);

    loader_kbd_init(system_table);
    loader_mouse_init(system_table);

    kbd_init(system_table);
    term_init();
    fat_set_bs(g_BS, image_handle, system_table);
    fat_init();
    mouse_init(system_table);

    loader_boot_screen();

    /* Transition: switch to own drivers, then ExitBootServices */
    kbd_switch_to_ps2();
    if (ahci_is_ready()) {
        fat_use_ahci();
    }
    exit_boot_services();

    term_clear(LUMIE_BLUE);
    term_set_bg(LUMIE_BLUE);
    term_set_fg(LUMIE_WHITE);

    shell_run();
}
