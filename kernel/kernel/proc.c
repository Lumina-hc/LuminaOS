#include "../include/kernel/proc.h"
#include "../include/kernel/elf.h"
#include "../include/kernel/paging.h"
#include "../include/kernel/pmm.h"
#include "../include/kernel/timer.h"
#include "../include/kernel/heap.h"
#include <stdint.h>

#define PAGE_SIZE 4096
#define USER_STACK_TOP 0x7FF00000

extern void kernel_resume(void);

static proc_t* proc_head_ptr = 0;
static proc_t* current_proc = 0;
static int next_pid = 1;

static void proc_link(proc_t* p)
{
    p->next = proc_head_ptr;
    proc_head_ptr = p;
}

static void proc_unlink(proc_t* p)
{
    if (proc_head_ptr == p) {
        proc_head_ptr = p->next;
        p->next = 0;
        return;
    }
    for (proc_t* it = proc_head_ptr; it; it = it->next) {
        if (it->next == p) {
            it->next = p->next;
            break;
        }
    }
    p->next = 0;
}

proc_t* proc_head(void)
{
    return proc_head_ptr;
}

void proc_init(void)
{
    proc_head_ptr = 0;
    current_proc = 0;
    next_pid = 1;
}

proc_t* proc_current(void)
{
    return current_proc;
}

void proc_set_current(proc_t* proc)
{
    current_proc = proc;
}

proc_t* proc_from_pid(int pid)
{
    for (proc_t* p = proc_head_ptr; p; p = p->next)
        if (p->pid == pid)
            return p;
    return 0;
}

proc_t* proc_alloc(void)
{
    proc_t* p = (proc_t*)kmalloc(sizeof(proc_t));
    if (!p) return 0;
    p->next = 0;
    p->pid = next_pid++;
    p->state = PROC_READY;
    p->gui = 1;
    p->arg = 0;
    p->page_dir_phys = 0;
    p->entry_eip = 0;
    p->user_esp = 0;
    p->brk = 0;
    p->sleep_until = 0;
    p->minimized = 0;
    p->has_ctx = 0;
    p->exit_code = 0;
    p->ppid = 0;
    p->pending_exit = 0;
    for (int i = 0; i < MAX_FDS; i++)
        p->fds[i] = -1;
    p->ctx = (isr_regs_t){0};
    p->name[0] = '\0';
    proc_link(p);
    return p;
}

proc_t* proc_at(int idx)
{
    if (idx < 0) return 0;
    proc_t* p = proc_head_ptr;
    while (p && idx > 0) {
        p = p->next;
        idx--;
    }
    return p;
}

int proc_count(void)
{
    int c = 0;
    for (proc_t* p = proc_head_ptr; p; p = p->next)
        if (p->state != PROC_ZOMBIE && p->state != PROC_EXITED) c++;
    return c;
}

void proc_wake_check(void)
{
    uint32_t now = timer_ticks();
    for (proc_t* p = proc_head_ptr; p; p = p->next)
        if (p->state == PROC_SLEEPING && p->sleep_until <= now)
            p->state = PROC_READY;
}

int proc_fd_slot(proc_t* p)
{
    if (!p) return -1;
    for (int i = 0; i < MAX_FDS; i++)
        if (p->fds[i] < 0)
            return i;
    return -1;
}

void proc_set_fd(proc_t* p, int slot)
{
    if (!p || slot < 0 || slot >= MAX_FDS) return;
    p->fds[slot] = 1;
}

int proc_get_fd(proc_t* p, int slot)
{
    if (!p || slot < 0 || slot >= MAX_FDS) return 0;
    return p->fds[slot];
}

void proc_clear_fd(proc_t* p, int slot)
{
    if (!p || slot < 0 || slot >= MAX_FDS) return;
    p->fds[slot] = -1;
}

void proc_destroy(proc_t* proc)
{
    if (!proc) return;
    proc_unlink(proc);
    if (current_proc == proc)
        current_proc = 0;
    if (proc->page_dir_phys)
        paging_destroy_user_dir(proc->page_dir_phys);
    kfree(proc);
}

void proc_exit(proc_t* proc, int code)
{
    if (!proc) return;
    proc->exit_code = code;
    proc->state = PROC_ZOMBIE;
    proc->pending_exit = 1;
    for (int i = 0; i < MAX_FDS; i++)
        proc_clear_fd(proc, i);
}

int proc_wait(int pid, int* code_out, int* pid_out)
{
    proc_t* self = proc_current();
    if (!self) return -1;
    for (;;) {
        int matched = 0;
        for (proc_t* p = proc_head_ptr; p; p = p->next) {
            if (p->state != PROC_ZOMBIE || !p->pending_exit) continue;
            if (pid != -1 && p->pid != pid) continue;
            if (p->ppid != self->pid) continue;
            matched = 1;
            if (pid_out) *pid_out = p->pid;
            if (code_out) *code_out = p->exit_code;
            p->state = PROC_EXITED;
            p->pending_exit = 0;
            int ret = p->pid;
            proc_destroy(p);
            return ret;
        }
        if (pid != -1 && !matched)
            return -1;
        self->state = PROC_BLOCKED;
        kernel_resume();
    }
}

proc_t* proc_create(const char* elf_path, const char* name)
{
    proc_t* proc = proc_alloc();
    if (!proc) return 0;

    int n = 0;
    while (name && name[n] && n < 31) {
        proc->name[n] = name[n];
        n++;
    }
    proc->name[n] = '\0';

    uint64_t dir_phys = paging_create_user_dir();
    if (!dir_phys) {
        proc_destroy(proc);
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
        proc->state = PROC_UNUSED;
        proc_destroy(proc);
        return 0;
    }

    proc->entry_eip = entry;
    proc->brk = brk;

    uint64_t stack_bottom = (uint64_t)USER_STACK_TOP - (uint64_t)USER_STACK_PAGES * PAGE_SIZE;
    for (uint64_t p = 0; p < USER_STACK_PAGES; p++) {
        uint64_t phys = pmm_alloc_zero();
        if (!phys) {
            for (uint64_t j = 0; j < p; j++)
                paging_unmap_user_page(stack_bottom + j * PAGE_SIZE);
            paging_switch_user_pt(saved_cr3);
            proc->state = PROC_UNUSED;
            proc_destroy(proc);
            return 0;
        }
        if (!paging_map_user_page(stack_bottom + p * PAGE_SIZE, phys,
                                  PAGE_PRESENT | PAGE_RW | PAGE_USER)) {
            pmm_free(phys);
            for (uint64_t j = 0; j < p; j++)
                paging_unmap_user_page(stack_bottom + j * PAGE_SIZE);
            paging_switch_user_pt(saved_cr3);
            proc->state = PROC_UNUSED;
            proc_destroy(proc);
            return 0;
        }
    }
    proc->user_esp = USER_STACK_TOP - 0x1000;

    paging_switch_user_pt(saved_cr3);
    proc->has_ctx = 0;
    proc->minimized = 0;

    proc_t* parent = proc_current();
    proc->ppid = parent ? parent->pid : 0;
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

void proc_sleep_current(uint32_t ms)
{
    if (!current_proc) return;
    current_proc->state = PROC_SLEEPING;
    current_proc->sleep_until = timer_ticks() + (uint32_t)ms;
}
