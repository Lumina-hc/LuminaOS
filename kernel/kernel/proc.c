#include <kernel/proc.h>
#include <kernel/elf.h>
#include <kernel/paging.h>
#include <kernel/pmm.h>
#include <kernel/timer.h>
#include <kernel/gdt.h>
#include <kernel/tty.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define USER_BASE_ADDR 0x08048000
#define USER_STACK_TOP 0x7FF00000

static void memzero(void* dst, uint32_t n)
{
    uint8_t* p = (uint8_t*)dst;
    for (uint32_t i = 0; i < n; i++) p[i] = 0;
}

static proc_t procs[MAX_PROCS];
static proc_t* current_proc = 0;
static int next_pid = 1;

void proc_init(void)
{
    for (int i = 0; i < MAX_PROCS; i++) {
        procs[i].pid = 0;
        procs[i].state = PROC_UNUSED;
    }
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
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].state != PROC_UNUSED && procs[i].pid == pid)
            return &procs[i];
    }
    return 0;
}

static proc_t* proc_alloc(void)
{
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].state == PROC_UNUSED) {
            memzero(&procs[i], sizeof(proc_t));
            procs[i].pid = next_pid++;
            procs[i].state = PROC_READY;
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

void proc_destroy(proc_t* proc)
{
    if (!proc || proc->state == PROC_UNUSED) return;

    if (proc->page_dir_phys)
        paging_destroy_user_dir(proc->page_dir_phys);

    if (current_proc == proc)
        current_proc = 0;

    proc->state = PROC_UNUSED;
    proc->pid = 0;
    proc->has_ctx = 0;
    proc->minimized = 0;
    proc->arg = 0;
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

static void dbg_hex64(uint64_t v)
{
    static const char hx[] = "0123456789ABCDEF";
    char b[19];
    b[0] = '0';
    b[1] = 'x';
    for (int i = 0; i < 16; i++)
        b[2 + i] = hx[(v >> (60 - i * 4)) & 0xF];
    b[18] = '\0';
    tty_puts(b);
}

void proc_dbg_dump(void)
{
    tty_puts("base ");  dbg_hex64((uint64_t)(uintptr_t)procs); tty_puts("\n");
    tty_puts("cur  ");  dbg_hex64((uint64_t)(uintptr_t)current_proc); tty_puts("\n");
    tty_puts("next ");  dbg_hex64((uint64_t)(uint32_t)next_pid); tty_puts("\n");
    tty_puts("kd   ");  dbg_hex64(paging_get_kernel_dir()); tty_puts("\n");
    tty_puts("free ");  dbg_hex64((uint64_t)pmm_free_page_count()); tty_puts("\n");
    if (!current_proc) return;
    uint64_t* r = (uint64_t*)(uintptr_t)current_proc;
    for (int i = 0; i < 35; i++) {
        tty_puts("q");
        tty_putchar((char)('0' + (i / 10)));
        tty_putchar((char)('0' + (i % 10)));
        tty_puts(" ");
        dbg_hex64(r[i]);
        tty_puts("\n");
    }
}
