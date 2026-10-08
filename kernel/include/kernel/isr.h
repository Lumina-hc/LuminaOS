#ifndef ISR_H
#define ISR_H

#include <stdint.h>

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) isr_regs_t;

void isr_init(void);
void isr_handler(isr_regs_t* regs);

#endif
