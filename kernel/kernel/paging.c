#include "kernel/paging.h"
#include "kernel/pmm.h"
#include <stdint.h>

#define IDENTITY_ENTRIES 64
#define MMIO_BASE_3G     0xC0000000ULL

static uint64_t kernel_pml4_phys;

static uint64_t* to_ptr(uint64_t phys)
{
    return (uint64_t*)phys;
}

static uint64_t read_cr3(void)
{
    uint64_t v;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(v));
    return v;
}

static void write_cr3(uint64_t v)
{
    __asm__ volatile ("mov %0, %%cr3" :: "r"(v) : "memory");
}

static void invlpg(uint64_t vaddr)
{
    __asm__ volatile ("invlpg (%0)" :: "r"(vaddr) : "memory");
}

static uint64_t* walk_create(uint64_t* table, int idx)
{
    uint64_t e = table[idx];

    if (!(e & PAGE_PRESENT)) {
        uint64_t* next = to_ptr(pmm_alloc_zero());
        if (!next) return 0;
        table[idx] = (uint64_t)next | PAGE_PRESENT | PAGE_RW | PAGE_USER;
        return next;
    }

    if (e & PAGE_LARGE) return 0;

    return to_ptr(e & PAGE_ADDR_MASK);
}

static uint64_t* walk_pd(uint64_t* pd, uint64_t vaddr)
{
    int idx = (int)((vaddr >> 21) & 511);
    uint64_t e = pd[idx];

    if (!(e & PAGE_PRESENT)) {
        uint64_t* pt = to_ptr(pmm_alloc_zero());
        if (!pt) return 0;
        pd[idx] = (uint64_t)pt | PAGE_PRESENT | PAGE_RW | PAGE_USER;
        return pt;
    }

    if (!(e & PAGE_LARGE))
        return to_ptr(e & PAGE_ADDR_MASK);

    uint64_t* pt = to_ptr(pmm_alloc_zero());
    if (!pt) return 0;

    uint64_t base = e & 0x000FFFFFFFE00000ULL;
    uint64_t attrs = (e & 0xFFF) & ~PAGE_LARGE;

    for (int i = 0; i < 512; i++)
        pt[i] = (base + (uint64_t)i * PAGE_SIZE_4K) | attrs;

    pd[idx] = (uint64_t)pt | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    return pt;
}

void paging_init(void)
{
    uint64_t* pml4 = to_ptr(pmm_alloc_zero());
    uint64_t* pdpt = to_ptr(pmm_alloc_zero());
    uint64_t* pd0  = to_ptr(pmm_alloc_zero());
    uint64_t* pd3  = to_ptr(pmm_alloc_zero());

    pml4[0] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    pdpt[0] = (uint64_t)pd0  | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    pdpt[3] = (uint64_t)pd3  | PAGE_PRESENT | PAGE_RW | PAGE_USER;

    for (int i = 0; i < IDENTITY_ENTRIES; i++)
        pd0[i] = ((uint64_t)i * PAGE_SIZE_2M) | PAGE_PRESENT | PAGE_RW | PAGE_LARGE;

    for (int i = 0; i < 512; i++)
        pd3[i] = (MMIO_BASE_3G + (uint64_t)i * PAGE_SIZE_2M)
                 | PAGE_PRESENT | PAGE_RW | PAGE_LARGE;

    kernel_pml4_phys = (uint64_t)pml4;
    write_cr3(kernel_pml4_phys);
}

uint64_t paging_get_kernel_dir(void)
{
    return kernel_pml4_phys;
}

uint64_t paging_create_user_dir(void)
{
    uint64_t new_pml4_phys = pmm_alloc_zero();
    if (!new_pml4_phys) return 0;
    uint64_t new_pdpt_phys = pmm_alloc_zero();
    if (!new_pdpt_phys) { pmm_free(new_pml4_phys); return 0; }
    uint64_t new_pd0_phys = pmm_alloc_zero();
    if (!new_pd0_phys) { pmm_free(new_pdpt_phys); pmm_free(new_pml4_phys); return 0; }

    uint64_t* new_pml4 = to_ptr(new_pml4_phys);
    uint64_t* new_pdpt = to_ptr(new_pdpt_phys);
    uint64_t* new_pd0  = to_ptr(new_pd0_phys);

    uint64_t* kpml4  = to_ptr(kernel_pml4_phys);
    uint64_t* kpdpt  = to_ptr(kpml4[0] & PAGE_ADDR_MASK);
    uint64_t* kpd0   = to_ptr(kpdpt[0] & PAGE_ADDR_MASK);

    for (int i = 0; i < IDENTITY_ENTRIES; i++)
        new_pd0[i] = kpd0[i];

    for (int i = 1; i < 512; i++)
        new_pdpt[i] = kpdpt[i];
    new_pdpt[0] = new_pd0_phys | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    new_pml4[0] = new_pdpt_phys | PAGE_PRESENT | PAGE_RW | PAGE_USER;

    return new_pml4_phys;
}

void paging_switch_user_pt(uint64_t pml4_phys)
{
    if (pml4_phys)
        write_cr3(pml4_phys);
}

void paging_destroy_user_dir(uint64_t pml4_phys)
{
    if (!pml4_phys || pml4_phys == kernel_pml4_phys) return;

    uint64_t* pml4 = to_ptr(pml4_phys);
    uint64_t e0 = pml4[0];
    if (e0 & PAGE_PRESENT) {
        uint64_t* pdpt = to_ptr(e0 & PAGE_ADDR_MASK);
        for (int pi = 0; pi <= 1; pi++) {
            uint64_t pe = pdpt[pi];
            if (!(pe & PAGE_PRESENT) || (pe & PAGE_LARGE)) continue;
            uint64_t* pd = to_ptr(pe & PAGE_ADDR_MASK);
            int start = (pi == 0) ? IDENTITY_ENTRIES : 0;
            for (int di = start; di < 512; di++) {
                uint64_t de = pd[di];
                if (!(de & PAGE_PRESENT) || (de & PAGE_LARGE)) continue;
                uint64_t* pt = to_ptr(de & PAGE_ADDR_MASK);
                for (int j = 0; j < 512; j++)
                    if (pt[j] & PAGE_PRESENT)
                        pmm_free(pt[j] & PAGE_ADDR_MASK);
                pmm_free((uint64_t)pt);
            }
            pmm_free((uint64_t)pd);
        }
        pmm_free((uint64_t)pdpt);
    }
    pmm_free(pml4_phys);
}

uint64_t paging_map_page(uint64_t vaddr, uint64_t phys, uint64_t flags)
{
    uint64_t* pml4 = to_ptr(read_cr3() & PAGE_ADDR_MASK);

    uint64_t* pdpt = walk_create(pml4, (int)((vaddr >> 39) & 511));
    if (!pdpt) return 0;

    uint64_t* pd = walk_create(pdpt, (int)((vaddr >> 30) & 511));
    if (!pd) return 0;

    uint64_t* pt = walk_pd(pd, vaddr);
    if (!pt) return 0;

    pt[(vaddr >> 12) & 511] = (phys & PAGE_ADDR_MASK) | flags;
    invlpg(vaddr);
    return vaddr;
}

void paging_unmap_page(uint64_t vaddr)
{
    uint64_t* pml4 = to_ptr(read_cr3() & PAGE_ADDR_MASK);

    uint64_t e = pml4[(vaddr >> 39) & 511];
    if (!(e & PAGE_PRESENT)) return;
    uint64_t* pdpt = to_ptr(e & PAGE_ADDR_MASK);

    e = pdpt[(vaddr >> 30) & 511];
    if (!(e & PAGE_PRESENT) || (e & PAGE_LARGE)) return;
    uint64_t* pd = to_ptr(e & PAGE_ADDR_MASK);

    e = pd[(vaddr >> 21) & 511];
    if (!(e & PAGE_PRESENT) || (e & PAGE_LARGE)) return;
    uint64_t* pt = to_ptr(e & PAGE_ADDR_MASK);

    pt[(vaddr >> 12) & 511] = 0;
    invlpg(vaddr);
}

void paging_map_region(uint64_t base, uint64_t size)
{
    uint64_t* pml4 = to_ptr(read_cr3() & PAGE_ADDR_MASK);
    uint64_t start = base & ~(PAGE_SIZE_2M - 1);
    uint64_t end = (base + size + PAGE_SIZE_2M - 1) & ~(PAGE_SIZE_2M - 1);
    if (end <= start) end = start + PAGE_SIZE_2M;

    for (uint64_t va = start; va < end; va += PAGE_SIZE_2M) {
        int pml4i = (int)((va >> 39) & 511);
        int pdpti = (int)((va >> 30) & 511);
        int pdi   = (int)((va >> 21) & 511);

        uint64_t e = pml4[pml4i];
        uint64_t* pdpt;
        if (e & PAGE_PRESENT) {
            pdpt = to_ptr(e & PAGE_ADDR_MASK);
        } else {
            pdpt = to_ptr(pmm_alloc_zero());
            if (!pdpt) return;
            pml4[pml4i] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_RW;
        }

        e = pdpt[pdpti];
        uint64_t* pd;
        if (e & PAGE_PRESENT) {
            pd = to_ptr(e & PAGE_ADDR_MASK);
        } else {
            pd = to_ptr(pmm_alloc_zero());
            if (!pd) return;
            pdpt[pdpti] = (uint64_t)pd | PAGE_PRESENT | PAGE_RW;
        }

        if (pd[pdi] & PAGE_PRESENT) continue;
        pd[pdi] = (va & ~(PAGE_SIZE_2M - 1)) | PAGE_PRESENT | PAGE_RW | PAGE_LARGE;
    }

    write_cr3(read_cr3());
}

uint64_t paging_map_user_page(uint64_t vaddr, uint64_t phys, uint64_t flags)
{
    if (vaddr < USER_BASE || vaddr >= USER_SPACE_TOP) return 0;
    return paging_map_page(vaddr, phys, flags | PAGE_USER);
}

void paging_unmap_user_page(uint64_t vaddr)
{
    if (vaddr < USER_BASE || vaddr >= USER_SPACE_TOP) return;
    paging_unmap_page(vaddr);
}

uint64_t paging_alloc_user_pages(uint64_t vaddr, uint64_t pages)
{
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t phys = pmm_alloc_zero();
        if (!phys) return 0;
        if (!paging_map_page(vaddr + i * PAGE_SIZE_4K, phys, PAGE_PRESENT | PAGE_RW | PAGE_USER))
            return 0;
    }
    return vaddr;
}
