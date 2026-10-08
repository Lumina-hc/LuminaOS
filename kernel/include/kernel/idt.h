#ifndef IDT_H
#define IDT_H

#include <stdint.h>

#define IDT_TYPE_INT_GATE 0x0E
#define IDT_TYPE_TRAP_GATE 0x0F

#define IDT_FLAG_PRESENT 0x80
#define IDT_FLAG_DPL(n) ((uint8_t)(((n) & 3) << 5))
#define IDT_FLAG_GATE(type) (uint8_t)(IDT_FLAG_PRESENT | IDT_FLAG_DPL(0) | (type))

void idt_set_gate_ist(uint8_t num, uint64_t base, uint16_t selector, uint8_t ist, uint8_t flags);
void idt_set_gate(uint8_t num, uint64_t base, uint16_t selector, uint8_t flags);
void idt_init(void);

#endif
