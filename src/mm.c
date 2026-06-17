#include "mm.h"
#include "lumie.h"
#include "kernel.h"

#define MM_MAX_PAGES 262144
#define HEAP_POOL_SIZE (64 * 1024 * 1024)

static u64 g_mem_map_key = 0;
static u64 g_mem_desc_size = 0;
static u32 g_mem_desc_ver = 0;

static u64 *g_page_stack = NULL;
static u64 g_page_stack_top = 0;
static u64 g_page_stack_capacity = 0;

typedef struct heap_hdr {
    struct heap_hdr *next;
    u64 size;
    int free;
} heap_hdr;

static u8 *g_heap_start = NULL;
static heap_hdr *g_heap_first = NULL;

u64 mm_get_map_key(void) { return g_mem_map_key; }
u64 mm_get_desc_size(void) { return g_mem_desc_size; }
u32 mm_get_desc_ver(void) { return g_mem_desc_ver; }

void mm_init(efi_boot_services *bs, efi_handle image_handle) {
    (void)image_handle;

    efi_memory_descriptor *mmap_buf = NULL;
    u64 mmap_size = 0;
    u64 map_key = 0;
    u64 desc_size = 0;
    u32 desc_ver = 0;

    typedef efi_status (*bs_get_memory_map_t)(u64*, efi_memory_descriptor*, u64*, u64*, u32*);
    bs_get_memory_map_t get_mmap = (bs_get_memory_map_t)bs->GetMemoryMap;
    typedef efi_status (*bs_allocate_pages_t)(u32, u32, u64, u64*);
    bs_allocate_pages_t alloc_pages = (bs_allocate_pages_t)bs->AllocatePages;
    typedef efi_status (*bs_free_pages_t)(u64, u64);
    bs_free_pages_t free_pages = (bs_free_pages_t)bs->FreePages;

    mmap_size = 0;
    get_mmap(&mmap_size, NULL, &map_key, &desc_size, &desc_ver);
    mmap_size += desc_size * 64;

    u64 page_addr = 0;
    u64 pages_needed = (mmap_size + desc_size * 64 + PAGE_SIZE - 1) / PAGE_SIZE;
    efi_status st = alloc_pages(0, EFI_BOOT_SERVICES_DATA, pages_needed, &page_addr);
    if (EFI_ERROR(st)) return;
    mmap_buf = (efi_memory_descriptor*)(usize)page_addr;

    st = get_mmap(&mmap_size, mmap_buf, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(st)) { free_pages(page_addr, pages_needed); return; }

    u64 total_desc = mmap_size / desc_size;
    u64 avail_pages = 0;
    for (u64 i = 0; i < total_desc; i++) {
        efi_memory_descriptor *d = (efi_memory_descriptor*)((u8*)mmap_buf + i * desc_size);
        if (d->Type == 7) {
            if (d->PhysicalStart >= 0x100000) {
                avail_pages += d->NumberOfPages;
            }
        }
    }

    if (avail_pages > MM_MAX_PAGES) avail_pages = MM_MAX_PAGES;

    u64 stack_pages = (avail_pages * sizeof(u64) + PAGE_SIZE - 1) / PAGE_SIZE;
    u64 heap_pages = HEAP_POOL_SIZE / PAGE_SIZE;
    u64 total_needed = stack_pages + heap_pages;

    u64 alloc_addr = 0;
    st = alloc_pages(0, 0, total_needed, &alloc_addr);
    if (EFI_ERROR(st)) { free_pages(page_addr, pages_needed); return; }

    g_page_stack = (u64*)(usize)alloc_addr;
    g_page_stack_capacity = avail_pages;
    g_page_stack_top = 0;

    u64 heap_phys = alloc_addr + stack_pages * PAGE_SIZE;
    g_heap_start = (u8*)(usize)heap_phys;

    /* Now re-get the memory map after our allocations */
    mmap_size = 0;
    get_mmap(&mmap_size, NULL, &map_key, &desc_size, &desc_ver);
    mmap_size += desc_size * 16;
    pages_needed = (mmap_size + PAGE_SIZE - 1) / PAGE_SIZE;
    st = alloc_pages(0, EFI_BOOT_SERVICES_DATA, pages_needed, &page_addr);
    if (EFI_ERROR(st)) return;
    mmap_buf = (efi_memory_descriptor*)(usize)page_addr;

    st = get_mmap(&mmap_size, mmap_buf, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(st)) { free_pages(page_addr, pages_needed); return; }

    g_mem_map_key = map_key;
    g_mem_desc_size = desc_size;
    g_mem_desc_ver = desc_ver;

    total_desc = mmap_size / desc_size;
    for (u64 i = 0; i < total_desc && g_page_stack_top < g_page_stack_capacity; i++) {
        efi_memory_descriptor *d = (efi_memory_descriptor*)((u8*)mmap_buf + i * desc_size);
        if (d->Type == 7 && d->PhysicalStart >= 0x100000) {
            u64 start = d->PhysicalStart;
            u64 count = d->NumberOfPages;
            u64 end = start + count * PAGE_SIZE;
            if (start < alloc_addr + total_needed * PAGE_SIZE) {
                if (end <= alloc_addr) {
                    /* Completely before our allocation - add all */
                    for (u64 j = 0; j < count && g_page_stack_top < g_page_stack_capacity; j++)
                        g_page_stack[g_page_stack_top++] = start + j * PAGE_SIZE;
                } else if (start >= alloc_addr + total_needed * PAGE_SIZE) {
                    /* Completely after our allocation - add all */
                    for (u64 j = 0; j < count && g_page_stack_top < g_page_stack_capacity; j++)
                        g_page_stack[g_page_stack_top++] = start + j * PAGE_SIZE;
                } else {
                    /* Overlaps our allocation - split */
                    u64 before_start = start;
                    u64 before_end = alloc_addr;
                    if (before_end > before_start) {
                        u64 before_count = (before_end - before_start) / PAGE_SIZE;
                        for (u64 j = 0; j < before_count && g_page_stack_top < g_page_stack_capacity; j++)
                            g_page_stack[g_page_stack_top++] = before_start + j * PAGE_SIZE;
                    }
                    u64 after_start = alloc_addr + total_needed * PAGE_SIZE;
                    u64 after_end = end;
                    if (after_end > after_start) {
                        u64 after_count = (after_end - after_start) / PAGE_SIZE;
                        for (u64 j = 0; j < after_count && g_page_stack_top < g_page_stack_capacity; j++)
                            g_page_stack[g_page_stack_top++] = after_start + j * PAGE_SIZE;
                    }
                }
            } else {
                for (u64 j = 0; j < count && g_page_stack_top < g_page_stack_capacity; j++)
                    g_page_stack[g_page_stack_top++] = start + j * PAGE_SIZE;
            }
        }
    }

    /* Init heap */
    heap_hdr *first = (heap_hdr*)g_heap_start;
    first->next = NULL;
    first->size = HEAP_POOL_SIZE - sizeof(heap_hdr);
    first->free = 1;
    g_heap_first = first;

    free_pages((u64)(usize)mmap_buf, pages_needed);
}

void mm_init_post_ebs(void) {
}

void *mm_alloc_pages(u32 count) {
    if (g_page_stack_top < count) return NULL;
    u64 phys = 0;
    if (count == 1) {
        phys = g_page_stack[--g_page_stack_top];
    } else {
        u64 first_page = g_page_stack[g_page_stack_top - count];
        for (u32 i = 0; i < count; i++) {
            u64 p = g_page_stack[--g_page_stack_top];
            if (i == 0) phys = p;
            if (p != first_page + i * PAGE_SIZE) {
                return NULL;
            }
        }
    }
    return (void*)(usize)phys;
}

void mm_free_pages(void *addr, u32 count) {
    u64 phys = (u64)(usize)addr;
    for (u32 i = 0; i < count && g_page_stack_top < g_page_stack_capacity; i++) {
        g_page_stack[g_page_stack_top++] = phys + i * PAGE_SIZE;
    }
}

static heap_hdr *find_free_block(heap_hdr **last, u64 size) {
    heap_hdr *cur = g_heap_first;
    while (cur && !(cur->free && cur->size >= size)) {
        *last = cur;
        cur = cur->next;
    }
    return cur;
}

static heap_hdr *split_block(heap_hdr *h, u64 size) {
    if (h->size < size + sizeof(heap_hdr) + 32) return h;
    heap_hdr *new_hdr = (heap_hdr*)((u8*)h + sizeof(heap_hdr) + size);
    new_hdr->size = h->size - size - sizeof(heap_hdr);
    new_hdr->free = 1;
    new_hdr->next = h->next;
    h->size = size;
    h->next = new_hdr;
    return h;
}

void *kmalloc(u64 size) {
    if (size == 0) size = 1;
    size = (size + 7) & ~7;
    heap_hdr *last = g_heap_first;
    heap_hdr *h = find_free_block(&last, size);
    if (!h) return NULL;
    h = split_block(h, size);
    h->free = 0;
    return (void*)((u8*)h + sizeof(heap_hdr));
}

void kfree(void *ptr) {
    if (!ptr) return;
    heap_hdr *h = (heap_hdr*)((u8*)ptr - sizeof(heap_hdr));
    h->free = 1;
    heap_hdr *cur = g_heap_first;
    while (cur) {
        if (cur->free && cur->next && cur->next->free) {
            cur->size += sizeof(heap_hdr) + cur->next->size;
            cur->next = cur->next->next;
        }
        cur = cur->next;
    }
}

u64 mm_get_free_mem(void) {
    u64 total = 0;
    heap_hdr *cur = g_heap_first;
    while (cur) {
        if (cur->free) total += cur->size;
        cur = cur->next;
    }
    total += g_page_stack_top * PAGE_SIZE;
    return total;
}
