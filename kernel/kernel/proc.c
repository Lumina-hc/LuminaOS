#include "kernel/proc.h"
#include "kernel/elf.h"
#include "kernel/paging.h"
#include "kernel/pmm.h"
#include "kernel/timer.h"
#include "kernel/gdt.h"
#include "kernel/tty.h"
#include "kernel/syscall.h"
#include <stdint.h>

#define PAGE_SIZE 4096
#define USER_BASE_ADDR 0x08048000
#define USER_STACK_TOP 0x7FF00000

extern void kernel_resume(void);

static void memzero(void* dst, uint32_t n)
{
    uint8_t* p = (uint8_t*)dst;
    for (uint32_t i = 0; i < n; i++) p[i] = 0;
}

static proc_t procs[MAX_PROCS];
static proc_t* current_proc = 0;
static int next_pid = 1;

int proc_fd_slot(proc_t* p, int* skip)
{
    int start = *skip + 1;
    for (int fd = start; fd < 16; fd++) {
        if (p->fds[fd] == -1) {
            *skip = fd;
            return fd;
        }
    }
    *skip = 15;
    return -1;
}

static proc_t* proc_alloc(void)
{
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].state == PROC_UNUSED) {
            memzero(&procs[i], sizeof(proc_t));
            procs[i].pid = next_pid++;
            procs[i].state = PROC_READY;
            procs[i].ppid = 0;
            for (int f = 0; f < 16; f++)
                procs[i].fds[f] = -1;
            return &procs[i];
        }
    }
    return 0;
}

proc_t* proc_create(const char* elf_path, const char* name)
{
    proc_t* proc = proc_alloc();
    if (!proc) return 0;
    proc->gui = 1;
    proc->arg = 0;

    int n = 0;
    while (name && name[n] && n < 31) {
        proc->name[n] = name[n];
        n++;
    }
    proc->name[n] = '\0';

    uint64_t dir_phys = paging_create_user_dir();
    if (!dir_phys) {
        proc->state = PROC_UNUSED;
        proc->pid = 0;
        return 0;
    }
    proc->page_dir_phys = dir_phys;

    uint64_t saved_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(saved_cr3));
    paging_switch_user_pt(dir_phys);

    uint64_t entry = 0;
    uint64_t brk = 0;
    if (elf_load(elf_path, &entry, &brk) != 0) {
        paging_switch_user_pt(saved_cr3);
        paging_destroy_user_dir(dir_phys);
        proc->state = PROC_UNUSED;
        proc->pid = 0;
        return 0;
    }

    proc->entry_eip = entry;
    proc->brk = brk;

    uint64_t stack_bottom = (uint64_t)USER_STACK_TOP - (uint64_t)USER_STACK_PAGES * PAGE_SIZE;
    for (uint64_t p = 0; p < USER_STACK_PAGES; p++) {
        uint64_t phys = pmm_alloc_zero();
        if (!phys) {
            paging_switch_user_pt(saved_cr3);
            paging_destroy_user_dir(dir_phys);
            proc->state = PROC_UNUSED;
            proc->pid = 0;
            return 0;
        }
        paging_map_user_page(stack_bottom + p * PAGE_SIZE, phys,
                             PAGE_PRESENT | PAGE_RW | PAGE_USER);
    }
    proc->user_esp = USER_STACK_TOP - 0x1000;

    paging_switch_user_pt(saved_cr3);
    proc->has_ctx = 0;
    proc->minimized = 0;

    return proc;
}

proc_t* proc_spawn(const char* elf_path, const char* name, int arg)
{
    proc_t* proc = proc_create(elf_path, name);
    if (!proc) return 0;
    proc->gui = 0;
    proc->arg = arg;
    return proc;
}

static int proc_fork_copy_pages(proc_t* parent)
{
    uint64_t saved_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(saved_cr3));
    paging_switch_user_pt(parent->page_dir_phys);

    uint64_t range_end = parent->brk + USER_STACK_PAGES * PAGE_SIZE;
    for (uint64_t va = USER_BASE_ADDR; va < range_end; va += PAGE_SIZE) {
        uint64_t* pml4 = (uint64_t*)parent->page_dir_phys;
        int p4i = (int)((va >> 39) & 511);
        int p3i = (int)((va >> 30) & 511);
        int p2i = (int)((va >> 21) & 511);
        int p1i = (int)((va >> 12) & 511);

        uint64_t e = pml4[p4i];
        if (!(e & PAGE_PRESENT)) continue;
        uint64_t* pdpt = (uint64_t*)(e & PAGE_ADDR_MASK);
        e = pdpt[p3i];
        if (!(e & PAGE_PRESENT) || (e & PAGE_LARGE)) continue;
        uint64_t* pd = (uint64_t*)(e & PAGE_ADDR_MASK);
        e = pd[p2i];
        if (!(e & PAGE_PRESENT) || (e & PAGE_LARGE)) continue;
        uint64_t* pt = (uint64_t*)(e & PAGE_ADDR_MASK);
        e = pt[p1i];
        if (!(e & PAGE_PRESENT)) continue;

        uint64_t src_phys = e & PAGE_ADDR_MASK;
        uint64_t dst_phys = pmm_alloc_zero();
        if (!dst_phys) {
            paging_switch_user_pt(saved_cr3);
            return 1;
        }
        uint8_t* src = (uint8_t*)(uintptr_t)src_phys;
        uint8_t* dst = (uint8_t*)(uintptr_t)dst_phys;
        for (int i = 0; i < PAGE_SIZE; i++)
            dst[i] = src[i];

        if (!paging_map_user_page(va, dst_phys, PAGE_PRESENT | PAGE_RW | PAGE_USER)) {
            pmm_free(dst_phys);
            paging_switch_user_pt(saved_cr3);
            return 1;
        }
    }

    paging_switch_user_pt(saved_cr3);
    return 0;
}

proc_t* proc_fork(proc_t* parent)
{
    if (!parent) return 0;

    proc_t* child = proc_alloc();
    if (!child) return 0;

    child->ppid = parent->pid;
    child->arg = parent->arg;
    child->gui = parent->gui;
    child->exit_code = 0;
    child->minimized = 0;
    child->has_ctx = 0;

    for (int i = 0; i < 16; i++)
        child->fds[i] = parent->fds[i];

    uint64_t n = 0;
    while (parent->name[n] && n < 31) {
        char c = parent->name[n];
        child->name[n] = c;
        n++;
    }
    child->name[n] = '\0';

    uint64_t dir_phys = paging_create_user_dir();
    if (!dir_phys) {
        child->state = PROC_UNUSED;
        child->pid = 0;
        for (int i = 0; i < 16; i++)
            child->fds[i] = -1;
        return 0;
    }
    child->page_dir_phys = dir_phys;

    proc_t* saved_current = current_proc;
    proc_set_current(parent);

    int copy_err = proc_fork_copy_pages(parent);

    proc_set_current(saved_current);

    if (copy_err) {
        paging_destroy_user_dir(dir_phys);
        child->state = PROC_UNUSED;
        child->pid = 0;
        for (int i = 0; i < 16; i++)
            child->fds[i] = -1;
        return 0;
    }

    child->entry_eip = parent->entry_eip;
    child->user_esp = parent->user_esp;
    child->brk = parent->brk;
    child->state = PROC_READY;

    return child;
}

int proc_wait(int pid, int* exit_code)
{
    proc_t* parent = proc_current();
    if (!parent) return -1;

    while (1) {
        int found = 0;
        for (int i = 0; i < MAX_PROCS; i++) {
            proc_t* p = &procs[i];
            if (p->state == PROC_UNUSED) continue;
            if (p->ppid != parent->pid) continue;
            if (pid != 0 && pid != p->pid) continue;

            if (p->state == PROC_ZOMBIE) {
                if (exit_code) *exit_code = p->exit_code;
                int wpid = p->pid;
                proc_destroy(p);
                return wpid;
            }
            found = 1;
        }
        if (!found) return -1;
        parent->state = PROC_BLOCKED;
        parent->has_ctx = 1;
        proc_set_current(parent);
        kernel_resume();
    }
}

int proc_get_fd(proc_t* p, int fd)
{
    if (!p) return -1;
    if (fd < 0 || fd >= 16) return -1;
    int owner = p->fds[fd];
    if (owner == -1) return -1;
    return owner;
}

void proc_set_fd(proc_t* p, int fd, int val)
{
    if (!p) return;
    if (fd < 0 || fd >= 16) return;
    p->fds[fd] = val;
}

int proc_wake_pipe_owner(int pipe_id, int which)
{
    int target = pipe_id * 2 + which;
    for (int i = 0; i < MAX_PROCS; i++) {
        proc_t* p = &procs[i];
        if (p->state != PROC_BLOCKED) continue;
        for (int f = 0; f < 16; f++) {
            if (p->fds[f] == target) {
                p->state = PROC_READY;
                return p->pid;
            }
        }
    }
    return -1;
}

proc_t* proc_from_pid(int pid)
{
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].state != PROC_UNUSED && procs[i].pid == pid)
            return &procs[i];
    }
    return 0;
}

proc_t* proc_current(void)
{
    return current_proc;
}

void proc_set_current(proc_t* proc)
{
    current_proc = proc;
}

void proc_destroy(proc_t* proc)
{
    if (!proc || proc->state == PROC_UNUSED) return;

    if (proc->page_dir_phys)
        paging_destroy_user_dir(proc->page_dir_phys);

    if (current_proc == proc)
        current_proc = 0;

    proc->state = PROC_UNUSED;
    proc->pid = 0;
    proc->ppid = 0;
    proc->has_ctx = 0;
    proc->minimized = 0;
    proc->arg = 0;
    for (int i = 0; i < 16; i++)
        proc->fds[i] = -1;
}

void proc_sleep_current(uint32_t ms)
{
    if (!current_proc) return;
    current_proc->state = PROC_SLEEPING;
    current_proc->sleep_until = timer_ticks() + (uint32_t)ms;
}

void proc_wake_check(void)
{
    uint32_t now = timer_ticks();
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].state == PROC_SLEEPING && procs[i].sleep_until <= now) {
            procs[i].state = PROC_READY;
        }
    }
}

proc_t* proc_at(int idx)
{
    if (idx < 0 || idx >= MAX_PROCS) return 0;
    return &procs[idx];
}

int proc_count(void)
{
    int c = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].state != PROC_UNUSED && procs[i].state != PROC_ZOMBIE) c++;
    }
    return c;
}
