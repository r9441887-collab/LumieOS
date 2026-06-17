#ifndef __LOADER_H__
#define __LOADER_H__

#include "efi.h"
#include "lumie.h"

void loader_drv_clear(u32 color);
void loader_drv_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);
void loader_drv_draw_str(u32 x, u32 y, u32 fg, u32 bg, const char *str);
void loader_drv_progress(u32 x, u32 y, u32 w, u32 h, u32 fg, u32 bg, int pct);

void loader_kbd_init(efi_system_table *st);
int  loader_kbhit(void);
int  loader_getchar(void);

void loader_mouse_init(efi_system_table *st);
int  loader_mouse_poll(int *dx, int *dy, u8 *buttons);

void loader_boot_screen(void);
void loader_boot_menu(void);
void loader_install_screen(void);

void lumie_loader_start(efi_handle image_handle, efi_system_table *system_table);

/* Block device info */
typedef struct {
    efi_handle handle;
    char label[64];
    u64 block_count;
    u32 block_size;
    u8 is_removable;
    u8 is_partition;
} loader_block_device;

int  loader_enum_block_devices(loader_block_device *devices, int max_devices);

#endif
