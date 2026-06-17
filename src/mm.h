#ifndef __MM_H__
#define __MM_H__

#include "efi.h"

#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

void mm_init(efi_boot_services *bs, efi_handle image_handle);
void mm_init_post_ebs(void);

void *mm_alloc_pages(u32 count);
void mm_free_pages(void *addr, u32 count);

void *kmalloc(u64 size);
void kfree(void *ptr);
u64 mm_get_free_mem(void);

u64 mm_get_map_key(void);
u64 mm_get_desc_size(void);
u32 mm_get_desc_ver(void);

#endif
