#include "kernel/timer.h"
#include "kernel/irq.h"
#include "kernel/io.h"
#include "kernel/sched.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_FREQ     1193182

static volatile uint32_t tick_count;

static void timer_handler(void)
{
    tick_count++;
    sched_tick();
}

void timer_init(void)
{
    uint32_t divisor = PIT_FREQ / TIMER_HZ;
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
    irq_install_handler(0, timer_handler);
}

uint32_t timer_ticks(void)
{
    return tick_count;
}

void timer_wait(uint32_t ms)
{
    uint32_t target = tick_count + (ms * TIMER_HZ) / 1000;
    while (tick_count < target) {
        __asm__ volatile ("sti; hlt");
    }
}
