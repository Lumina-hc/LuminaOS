#ifndef PROC_H
#define PROC_H

#include <stdint.h>

#define MAX_PROCS 32
#define USER_STACK_PAGES 16

typedef enum {
    PROC_UNUSED = 0,
    PROC_RUNNING,
    PROC_SLEEPING,
    PROC_ZOMBIE,
    PROC_EXITED
} proc_state_t;

typedef struct {
    int pid;
    proc_state_t state;
    uint64_t page_dir_phys;
    uint64_t entry_eip;
    uint64_t user_esp;
    uint64_t brk;
    uint64_t sleep_until;
    char name[32];
} proc_t;

void proc_init(void);
proc_t* proc_create(const char* elf_path, const char* name);
void proc_destroy(proc_t* proc);
proc_t* proc_current(void);
void proc_set_current(proc_t* proc);
proc_t* proc_from_pid(int pid);
void proc_sleep_current(uint32_t ms);
void proc_wake_check(void);

#endif
