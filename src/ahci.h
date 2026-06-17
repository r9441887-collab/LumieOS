#ifndef __AHCI_H__
#define __AHCI_H__

#include "efi.h"

int ahci_init(void);
int ahci_read_sectors(u32 lba, u32 count, void *buffer);
int ahci_write_sectors(u32 lba, u32 count, const void *buffer);
u64 ahci_get_sector_count(void);
int ahci_is_ready(void);

#endif
