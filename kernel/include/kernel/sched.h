#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include <kernel/isr.h>

#define SCHED_QUANTUM_MS 100

void sched_init(void);
void sched_user_enter(void);
void sched_user_exit(void);
int sched_user_active(void);
void sched_tick(void);
int sched_preempt_pending(void);
void sched_preempt_ack(void);
void sched_note_yield(void);
struct proc;
void sched_check_preempt(isr_regs_t* regs);
struct proc* sched_pick_ready(void);

#endif
