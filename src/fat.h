#ifndef __FAT_H__
#define __FAT_H__

#include "efi.h"
#include "lumie.h"

int  fat_init();
int  fat_read_file(const char *path, void *buffer, u32 max_size);
int  fat_write_file(const char *path, const void *data, u32 size);
int  fat_list_dir(const char *path, lumie_dirent *entries, int max_entries);
int  fat_exists(const char *path);
int  fat_get_file_size(const char *path);
int  fat_delete(const char *path);
int  fat_mkdir(const char *path);
int  fat_install_bootloader(void);
void fat_set_bs(efi_boot_services *bs, efi_handle img, efi_system_table *st);
int  fat_set_device(efi_handle device_handle);
int  fat_use_ahci(void);

#endif
