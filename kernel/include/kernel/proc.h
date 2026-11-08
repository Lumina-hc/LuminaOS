#ifndef PROC_H
#define PROC_H

#include <stdint.h>
#include <kernel/isr.h>
#include <kernel/heap.h>

#define USER_STACK_PAGES 16
#define PROC_NAME_MAX 32
#define MAX_FDS 8

typedef enum {
    PROC_UNUSED = 0,
    PROC_READY,
    PROC_RUNNING,
    PROC_SLEEPING,
    PROC_BLOCKED,
    PROC_SUSPENDED,
    PROC_ZOMBIE,
    PROC_EXITED
} proc_state_t;

typedef struct proc {
    int pid;
    proc_state_t state;
    uint64_t page_dir_phys;
    uint64_t entry_eip;
    uint64_t user_esp;
    uint64_t brk;
    uint64_t sleep_until;
    char name[32];
    isr_regs_t ctx;
    int minimized;
    int has_ctx;
    int arg;
    int exit_code;
    int gui;
    int ppid;
    int pending_exit;
    int fds[MAX_FDS];
    struct proc* next;
} proc_t;

void proc_init(void);
proc_t* proc_head(void);
proc_t* proc_alloc(void);
proc_t* proc_create(const char* elf_path, const char* name);
proc_t* proc_spawn(const char* elf_path, const char* name, int arg);
void proc_destroy(proc_t* proc);
void proc_exit(proc_t* proc, int code);
int proc_wait(int pid, int* code_out, int* pid_out);
proc_t* proc_current(void);
void proc_set_current(proc_t* proc);
proc_t* proc_from_pid(int pid);
int proc_fd_slot(proc_t* p);
void proc_set_fd(proc_t* p, int slot);
int proc_get_fd(proc_t* p, int slot);
void proc_clear_fd(proc_t* p, int slot);
void proc_sleep_current(uint32_t ms);
void proc_wake_check(void);
proc_t* proc_at(int idx);
int proc_count(void);
void proc_dbg_dump(void);

#endif
