#include <kernel/idt.h>

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt_entries[256];
static idt_ptr_t idt_ptr;

extern void idt_flush(uint64_t);

void idt_set_gate_ist(uint8_t num, uint64_t base, uint16_t selector, uint8_t ist, uint8_t flags)
{
    idt_entries[num].offset_low = (uint16_t)(base & 0xFFFF);
    idt_entries[num].offset_mid = (uint16_t)((base >> 16) & 0xFFFF);
    idt_entries[num].offset_high = (uint32_t)(base >> 32);
    idt_entries[num].selector = selector;
    idt_entries[num].ist = (uint8_t)(ist & 0x7);
    idt_entries[num].type_attr = flags;
    idt_entries[num].zero = 0;
}

void idt_set_gate(uint8_t num, uint64_t base, uint16_t selector, uint8_t flags)
{
    idt_set_gate_ist(num, base, selector, 0, flags);
}

void idt_init(void)
{
    idt_ptr.limit = sizeof(idt_entries) - 1;
    idt_ptr.base = (uint64_t)&idt_entries[0];

    idt_flush((uint64_t)&idt_ptr);
}
