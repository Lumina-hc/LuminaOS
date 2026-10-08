#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_PRESENT 0x1
#define PAGE_RW      0x2
#define PAGE_USER    0x4
#define PAGE_PWT     0x8
#define PAGE_PCD     0x10
#define PAGE_ACCESSED 0x20
#define PAGE_DIRTY   0x40
#define PAGE_LARGE   0x80
#define PAGE_GLOBAL  0x100

#define PAGE_ADDR_MASK 0x000FFFFFFFFFF000ULL

#define PAGE_SIZE_4K   0x1000ULL
#define PAGE_SIZE_2M   0x200000ULL

#define USER_BASE      0x0000000008000000ULL
#define USER_SPACE_TOP 0x0000000080000000ULL

void paging_init(void);

uint64_t paging_create_user_dir(void);
void paging_switch_user_pt(uint64_t pml4_phys);
void paging_destroy_user_dir(uint64_t pml4_phys);

uint64_t paging_map_page(uint64_t vaddr, uint64_t phys, uint64_t flags);
void paging_unmap_page(uint64_t vaddr);
uint64_t paging_alloc_user_pages(uint64_t vaddr, uint64_t pages);

uint64_t paging_map_user_page(uint64_t vaddr, uint64_t phys, uint64_t flags);
void paging_unmap_user_page(uint64_t vaddr);

uint64_t paging_get_kernel_dir(void);

#endif
