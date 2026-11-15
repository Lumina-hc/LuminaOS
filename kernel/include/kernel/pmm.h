#ifndef PMM_H
#define PMM_H

#include <stdint.h>

#define PMM_PAGE_SIZE  4096
#define PMM_MAX_PHYS   0x2000000
#define PMM_TOTAL_PAGES (PMM_MAX_PHYS / PMM_PAGE_SIZE)
#define PMM_BITMAP_SIZE (PMM_TOTAL_PAGES / 8)

void pmm_init(void);
void pmm_reserve(uintptr_t addr, uint32_t size);
uintptr_t pmm_alloc(void);
uintptr_t pmm_alloc_zero(void);
uintptr_t pmm_alloc_contig(uint32_t pages);
void pmm_free(uintptr_t addr);
uint32_t pmm_free_page_count(void);
uint32_t pmm_used_page_count(void);

#endif
