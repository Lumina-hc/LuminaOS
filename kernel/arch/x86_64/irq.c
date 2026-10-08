#include <kernel/irq.h>
#include <kernel/io.h>
#include <kernel/idt.h>
#include <kernel/isr.h>
#include <kernel/gdt.h>

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

#define APIC_BASE_ADDR  0xFEE00000ULL
#define APIC_REG_EOI    0x0B0
#define APIC_REG_LVT_TIMER 0x320
#define APIC_REG_LVT_THERM 0x330
#define APIC_REG_LVT_PERF  0x340
#define APIC_REG_LINT0  0x350
#define APIC_REG_LINT1  0x360
#define APIC_REG_TIMER_INIT 0x380
#define APIC_REG_SPURIOUS   0x0F0

#define LVT_MASK        (1u << 16)
#define LVT_VECTOR(n)   ((uint32_t)(n))
#define LVT_MODE_FIXED  0x000u
#define LVT_MODE_NMI    0x400u
#define LVT_MODE_EXTINT 0x700u
#define LVT_PERIODIC    (1u << 17)
#define LVT_ONESHOT     0u

static irq_handler_t irq_routines[16] = {0};

static void* const irq_stubs[16] = {
    (void*)irq0,  (void*)irq1,  (void*)irq2,  (void*)irq3,
    (void*)irq4,  (void*)irq5,  (void*)irq6,  (void*)irq7,
    (void*)irq8,  (void*)irq9,  (void*)irq10, (void*)irq11,
    (void*)irq12, (void*)irq13, (void*)irq14, (void*)irq15
};

static volatile uint32_t* apic_reg(uint32_t off)
{
    return (volatile uint32_t*)(APIC_BASE_ADDR + off);
}

void irq_install_handler(int irq, irq_handler_t handler)
{
    if (irq >= 0 && irq < 16)
        irq_routines[irq] = handler;
}

void irq_uninstall_handler(int irq)
{
    if (irq >= 0 && irq < 16)
        irq_routines[irq] = 0;
}

static void irq_remap(void)
{
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();

    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();

    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();

    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();

    outb(0x21, 0xF8);
    io_wait();
    outb(0xA1, 0xEF);
    io_wait();
}

static void pit_init(void)
{
    const uint16_t divisor = 11932;

    outb(0x43, 0x36);
    io_wait();
    outb(0x40, (uint8_t)(divisor & 0xFF));
    io_wait();
    outb(0x40, (uint8_t)(divisor >> 8));
    io_wait();
}

static void apic_init(void)
{
    *apic_reg(APIC_REG_SPURIOUS) = 0x000000FFu | (1u << 8);

    *apic_reg(APIC_REG_LVT_TIMER) = LVT_MASK | LVT_VECTOR(32);
    *apic_reg(APIC_REG_LVT_THERM) = LVT_MASK;
    *apic_reg(APIC_REG_LVT_PERF)  = LVT_MASK;
    *apic_reg(APIC_REG_TIMER_INIT) = 0;

    *apic_reg(APIC_REG_LINT1) = LVT_MASK;

    *apic_reg(APIC_REG_LINT0) = LVT_MASK;
    *apic_reg(APIC_REG_LINT0) = LVT_MODE_EXTINT;
}

static void apic_eoi(void)
{
    *apic_reg(APIC_REG_EOI) = 0;
}

void irq_init(void)
{
    irq_remap();
    pit_init();
    apic_init();

    for (int i = 0; i < 16; i++)
        idt_set_gate((uint8_t)(32 + i), (uint64_t)irq_stubs[i], GDT_KERNEL_CS,
                     IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
}

void irq_handler(isr_regs_t* regs)
{
    uint64_t irq_no = regs->vector - 32;

    if (irq_no < 16 && irq_routines[irq_no])
        irq_routines[irq_no]();

    if (irq_no < 16) {
        if (irq_no >= 8)
            outb(0xA0, 0x20);
        outb(0x20, 0x20);
    } else {
        apic_eoi();
    }
}
