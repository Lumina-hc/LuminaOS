#include <kernel/sched.h>
#include <kernel/timer.h>
#include <kernel/proc.h>
#include <kernel/isr.h>

extern void kernel_resume(void);

static volatile int user_active;
static volatile uint32_t user_start_tick;
static volatile int preempt_pending;

void sched_init(void)
{
    user_active = 0;
    user_start_tick = 0;
    preempt_pending = 0;
}

void sched_user_enter(void)
{
    user_active = 1;
    user_start_tick = timer_ticks();
    preempt_pending = 0;
}

void sched_user_exit(void)
{
    user_active = 0;
    preempt_pending = 0;
}

int sched_user_active(void)
{
    return user_active;
}

void sched_tick(void)
{
    if (user_active) {
        uint32_t elapsed = timer_ticks() - user_start_tick;
        if (elapsed >= SCHED_QUANTUM_MS)
            preempt_pending = 1;
    }
}

int sched_preempt_pending(void)
{
    return preempt_pending;
}

void sched_preempt_ack(void)
{
    preempt_pending = 0;
}

void sched_note_yield(void)
{
    user_start_tick = timer_ticks();
    preempt_pending = 0;
}

void sched_check_preempt(isr_regs_t* regs)
{
    if (!preempt_pending) return;
    if ((regs->cs & 3) != 3) return;
    proc_t* p = proc_current();
    if (!p || p->state != PROC_RUNNING) return;
    preempt_pending = 0;
    p->ctx = *regs;
    p->has_ctx = 1;
    p->state = PROC_READY;
    kernel_resume();
}

struct proc* sched_pick_ready(void)
{
    for (proc_t* pr = proc_head(); pr; pr = pr->next)
        if (pr->state == PROC_READY) return pr;
    return 0;
}
