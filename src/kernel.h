#ifndef __KERNEL_H__
#define __KERNEL_H__

#include "efi.h"

/* Global UEFI references – set in kernel.c, usable everywhere */
extern efi_system_table    *g_ST;
extern efi_handle           g_ImageHandle;
extern efi_boot_services   *g_BS;
extern efi_runtime_services *g_RT;

int lumie_efi_register_boot_entry(void);
void exit_boot_services(void);
int lumie_ps2_available(void);

#endif
