#ifndef PROC_H
#define PROC_H

#include <stdint.h>
#include "isr.h"

#define MAX_PROCS 32
#define USER_STACK_PAGES 16

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
    int ppid;
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
    int fds[16];
} proc_t;

void proc_init(void);
proc_t* proc_create(const char* elf_path, const char* name);
proc_t* proc_spawn(const char* elf_path, const char* name, int arg);
proc_t* proc_fork(proc_t* parent);
int proc_wait(int pid, int* exit_code);
int proc_get_fd(proc_t* p, int fd);
void proc_set_fd(proc_t* p, int fd, int val);
int proc_wake_pipe_owner(int pipe_id, int which);
int proc_fd_slot(proc_t* p, int* skip);
void proc_destroy(proc_t* proc);
proc_t* proc_current(void);
void proc_set_current(proc_t* proc);
proc_t* proc_from_pid(int pid);
void proc_sleep_current(uint32_t ms);
void proc_wake_check(void);
proc_t* proc_at(int idx);
int proc_count(void);

#endif
