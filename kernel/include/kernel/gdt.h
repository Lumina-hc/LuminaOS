#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define GDT_KERNEL_CS 0x08
#define GDT_KERNEL_DS 0x10
#define GDT_USER_CS   0x18
#define GDT_USER_DS   0x20
#define GDT_TSS       0x28

void gdt_init(void);
void tss_set_stack(uint64_t rsp0);
void tss_set_kernel_stack(uint64_t rsp);

#endif
