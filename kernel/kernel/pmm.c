#include "kernel/pmm.h"
#include <stdint.h>

#define PMM_BITMAP_ADDR 0x90000

static uint32_t free_pages;

static void pmm_mark_used(uint32_t page)
{
    uint8_t* bm = (uint8_t*)PMM_BITMAP_ADDR;
    bm[page / 8] |= (uint8_t)(1 << (page % 8));
}

static void pmm_mark_free(uint32_t page)
{
    uint8_t* bm = (uint8_t*)PMM_BITMAP_ADDR;
    bm[page / 8] &= (uint8_t)~(1 << (page % 8));
}

static int pmm_is_used(uint32_t page)
{
    uint8_t* bm = (uint8_t*)PMM_BITMAP_ADDR;
    return (bm[page / 8] >> (page % 8)) & 1;
}

void pmm_init(void)
{
    uint8_t* bm = (uint8_t*)PMM_BITMAP_ADDR;
    for (uint32_t i = 0; i < PMM_BITMAP_SIZE; i++)
        bm[i] = 0;

    free_pages = 0;
    for (uint32_t page = 0; page < PMM_TOTAL_PAGES; page++) {
        if (page * PMM_PAGE_SIZE < 0x100000) {
            pmm_mark_used(page);
        } else {
            free_pages++;
        }
    }
}

void pmm_reserve(uintptr_t addr, uint32_t size)
{
    uint32_t first = addr / PMM_PAGE_SIZE;
    uint32_t last = (addr + size + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    for (uint32_t page = first; page < last; page++) {
        if (page >= PMM_TOTAL_PAGES) break;
        if (!pmm_is_used(page)) {
            pmm_mark_used(page);
            free_pages--;
        }
    }
}

uintptr_t pmm_alloc(void)
{
    uint8_t* bm = (uint8_t*)PMM_BITMAP_ADDR;
    for (uint32_t i = 0; i < PMM_BITMAP_SIZE; i++) {
        uint8_t byte = bm[i];
        if (byte != 0xFF) {
            for (int bit = 0; bit < 8; bit++) {
                if (!((byte >> bit) & 1)) {
                    uint32_t page = i * 8 + bit;
                    pmm_mark_used(page);
                    free_pages--;
                    return (uintptr_t)(page * PMM_PAGE_SIZE);
                }
            }
        }
    }
    return 0;
}

static void memset_k(void* dst, uint8_t value, uint32_t len)
{
    uint8_t* d = (uint8_t*)dst;
    for (uint32_t i = 0; i < len; i++)
        d[i] = value;
}

uintptr_t pmm_alloc_zero(void)
{
    uintptr_t addr = pmm_alloc();
    if (addr)
        memset_k((void*)addr, 0, PMM_PAGE_SIZE);
    return addr;
}

void pmm_free(uintptr_t addr)
{
    if (addr == 0) return;
    uint32_t page = addr / PMM_PAGE_SIZE;
    if (page >= PMM_TOTAL_PAGES) return;
    if (page * PMM_PAGE_SIZE < 0x100000) return;
    if (!pmm_is_used(page)) return;
    pmm_mark_free(page);
    free_pages++;
}

uint32_t pmm_free_page_count(void)
{
    return free_pages;
}

uint32_t pmm_used_page_count(void)
{
    return PMM_TOTAL_PAGES - free_pages;
}
