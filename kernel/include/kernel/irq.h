#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include "isr.h"

typedef void (*irq_handler_t)(void);

void irq_install_handler(int irq, irq_handler_t handler);
void irq_uninstall_handler(int irq);
void irq_init(void);
void irq_handler(isr_regs_t* regs);

#endif
