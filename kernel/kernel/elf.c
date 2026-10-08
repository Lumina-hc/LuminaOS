#include <kernel/elf.h>
#include <kernel/paging.h>
#include <kernel/fat16.h>
#include <kernel/pmm.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define MAX_ELF_FILE_SIZE (128 * 1024)

static int elf_valid_header(elf64_header_t* hdr)
{
    if (hdr->e_ident[0] != 0x7F) return 0;
    if (hdr->e_ident[1] != 'E')  return 0;
    if (hdr->e_ident[2] != 'L')  return 0;
    if (hdr->e_ident[3] != 'F')  return 0;
    if (hdr->e_ident[4] != 2)    return 0;
    if (hdr->e_ident[5] != 1)    return 0;
    if (hdr->e_ident[6] != 1)    return 0;
    if (hdr->e_type != 2)        return 0;
    if (hdr->e_machine != 62)    return 0;
    if (hdr->e_phentsize != sizeof(elf64_phdr_t)) return 0;
    return 1;
}

int elf_load(const char* path, uint64_t* entry_out, uint64_t* brk_out)
{
    uint32_t file_size = 0;
    if (fat16_get_file_size(path, &file_size) != 0)
        return -1;
    if (file_size == 0 || file_size > MAX_ELF_FILE_SIZE)
        return -1;

    uint32_t buf_pages = (file_size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (buf_pages == 0) buf_pages = 1;
    if (buf_pages > 32) return -1;

    uint32_t phys_addrs[32];
    uint64_t buf_vaddr = 0;
    for (uint32_t i = 0; i < buf_pages; i++) {
        uint32_t phys = pmm_alloc_zero();
        if (!phys) {
            for (uint32_t j = 0; j < i; j++)
                pmm_free(phys_addrs[j]);
            return -1;
        }
        phys_addrs[i] = phys;
        if (i == 0)
            buf_vaddr = phys;
    }

    for (uint32_t i = 1; i < buf_pages; i++) {
        if (phys_addrs[i] != phys_addrs[i - 1] + PAGE_SIZE) {
            for (uint32_t j = 0; j < i; j++)
                pmm_free(phys_addrs[j]);
            return -1;
        }
    }

    uint32_t got = 0;
    if (fat16_read_file(path, (void*)(uintptr_t)buf_vaddr, file_size, &got) != 0) {
        for (uint32_t i = 0; i < buf_pages; i++)
            pmm_free(phys_addrs[i]);
        return -1;
    }
    if (got < sizeof(elf64_header_t)) {
        for (uint32_t i = 0; i < buf_pages; i++)
            pmm_free(phys_addrs[i]);
        return -1;
    }

    elf64_header_t* hdr = (elf64_header_t*)(uintptr_t)buf_vaddr;
    if (!elf_valid_header(hdr)) {
        for (uint32_t i = 0; i < buf_pages; i++)
            pmm_free(phys_addrs[i]);
        return -1;
    }

    uint64_t max_addr = 0;

    for (uint32_t i = 0; i < hdr->e_phnum; i++) {
        uint64_t ph_off = hdr->e_phoff + (uint64_t)i * hdr->e_phentsize;
        if (ph_off + sizeof(elf64_phdr_t) > file_size)
            continue;

        elf64_phdr_t* ph = (elf64_phdr_t*)(uintptr_t)(buf_vaddr + ph_off);

        if (ph->p_type != PT_LOAD)
            continue;

        uint64_t vaddr = ph->p_vaddr;
        uint64_t memsz = ph->p_memsz;
        uint64_t filesz = ph->p_filesz;
        uint64_t p_offset = ph->p_offset;

        if (memsz == 0)
            continue;
        if (vaddr < USER_BASE || vaddr >= USER_SPACE_TOP)
            continue;
        if (vaddr + memsz > USER_SPACE_TOP)
            continue;
        if (p_offset + filesz > file_size)
            continue;

        uint64_t page_start = vaddr & ~((uint64_t)PAGE_SIZE - 1);
        uint64_t page_end = (vaddr + memsz + PAGE_SIZE - 1) & ~((uint64_t)PAGE_SIZE - 1);
        uint64_t num_pages = (page_end - page_start) / PAGE_SIZE;

        if (num_pages == 0) continue;
        if (num_pages > 256) return -1;

        uint32_t seg_phys[256];

        for (uint64_t p = 0; p < num_pages; p++) {
            uint64_t page_vaddr = page_start + p * PAGE_SIZE;
            uint32_t phys = pmm_alloc_zero();
            if (!phys) {
                for (uint32_t j = 0; j < buf_pages; j++)
                    pmm_free(phys_addrs[j]);
                return -1;
            }
            if (!paging_map_user_page(page_vaddr, phys,
                                      PAGE_PRESENT | PAGE_RW | PAGE_USER)) {
                pmm_free(phys);
                for (uint32_t j = 0; j < buf_pages; j++)
                    pmm_free(phys_addrs[j]);
                return -1;
            }
            seg_phys[p] = phys;
        }

        uint64_t seg_lo = vaddr;
        uint64_t seg_hi = vaddr + filesz;

        for (uint64_t p = 0; p < num_pages; p++) {
            uint64_t dst_page = page_start + p * PAGE_SIZE;
            uint64_t lo = dst_page > seg_lo ? dst_page : seg_lo;
            uint64_t hi = dst_page + PAGE_SIZE < seg_hi ? dst_page + PAGE_SIZE : seg_hi;
            if (lo >= hi)
                continue;

            uint64_t src_off = p_offset + (lo - vaddr);
            uint64_t chunk = hi - lo;
            if (src_off + chunk > file_size) {
                if (src_off >= file_size)
                    continue;
                chunk = file_size - src_off;
            }

            uint8_t* dst = (uint8_t*)(uintptr_t)lo;
            uint8_t* src = (uint8_t*)(uintptr_t)(buf_vaddr + src_off);
            for (uint64_t b = 0; b < chunk; b++)
                dst[b] = src[b];
        }

        if (!(ph->p_flags & PF_W)) {
            uint64_t final_flags = PAGE_PRESENT | PAGE_USER;
            for (uint64_t p = 0; p < num_pages; p++)
                paging_map_user_page(page_start + p * PAGE_SIZE,
                                     seg_phys[p], final_flags);
        }

        uint64_t end_addr = vaddr + memsz;
        if (end_addr > max_addr)
            max_addr = end_addr;
    }

    for (uint32_t i = 0; i < buf_pages; i++)
        pmm_free(phys_addrs[i]);

    if (max_addr == 0)
        return -1;

    *entry_out = hdr->e_entry;
    *brk_out = (max_addr + PAGE_SIZE - 1) & ~((uint64_t)PAGE_SIZE - 1);
    return 0;
}
