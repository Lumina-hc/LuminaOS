#include <kernel/idt.h>
#include <kernel/isr.h>
#include <kernel/io.h>
#include <kernel/tty.h>

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

static void reg64(const char* name, uint64_t v)
{
    tty_puts(name);
    tty_puts("=0x");
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t n = (uint8_t)((v >> i) & 0xF);
        tty_putchar(n < 10 ? ('0' + n) : ('A' + n - 10));
    }
    tty_puts("\n");
}

void isr_handler(isr_regs_t* regs)
{
    tty_puts("\n[EXCEPTION] ");
    reg64("vec ", regs->vector);
    reg64("err ", regs->error);
    reg64("rip ", regs->rip);
    reg64("cs  ", regs->cs);
    reg64("rsp ", regs->rsp);
    reg64("ss  ", regs->ss);
    reg64("rflags ", regs->rflags);

    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    reg64("cr2  ", cr2);

    for (;;) {
        __asm__ volatile ("cli");
        __asm__ volatile ("hlt");
    }
}

void isr_init(void)
{
    for (int i = 0; i < 32; i++)
        idt_set_gate_ist((uint8_t)i, 0, 0x08, 0, 0x00);

    idt_set_gate_ist(0,  (uint64_t)isr0,  0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(1,  (uint64_t)isr1,  0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(2,  (uint64_t)isr2,  0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(3,  (uint64_t)isr3,  0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(4,  (uint64_t)isr4,  0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(5,  (uint64_t)isr5,  0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(6,  (uint64_t)isr6,  0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(7,  (uint64_t)isr7,  0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(8,  (uint64_t)isr8,  0x08, 1, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(9,  (uint64_t)isr9,  0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(10, (uint64_t)isr10, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(11, (uint64_t)isr11, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(12, (uint64_t)isr12, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(13, (uint64_t)isr13, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(14, (uint64_t)isr14, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(15, (uint64_t)isr15, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(16, (uint64_t)isr16, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(17, (uint64_t)isr17, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(18, (uint64_t)isr18, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(19, (uint64_t)isr19, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(20, (uint64_t)isr20, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(21, (uint64_t)isr21, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(22, (uint64_t)isr22, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(23, (uint64_t)isr23, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(24, (uint64_t)isr24, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(25, (uint64_t)isr25, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(26, (uint64_t)isr26, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(27, (uint64_t)isr27, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(28, (uint64_t)isr28, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(29, (uint64_t)isr29, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(30, (uint64_t)isr30, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
    idt_set_gate_ist(31, (uint64_t)isr31, 0x08, 0, IDT_FLAG_GATE(IDT_TYPE_INT_GATE));
}
