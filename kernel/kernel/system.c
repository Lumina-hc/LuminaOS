#include <kernel/system.h>
#include <kernel/io.h>
#include <kernel/gfx.h>

static void cpu_halt(void)
{
    __asm__ volatile ("cli; hlt");
}

static void sys_delay_ms(void)
{
    /* rough busy-wait loop, ~1ms per 1M iterations on QEMU TCG */
    for (volatile int i = 0; i < 1000000; i++) ;
}

void system_halt(void)
{
    gfx_begin();
    gfx_fill_rect(0, 0, gfx_width(), gfx_height(), 0x000000);
    gfx_draw_text(gfx_width() / 2 - 70, gfx_height() / 2 - 8, "System halted", 0xFFFFFF, 0x000000);
    gfx_flush();

    /* PM1a_CNT_BLK = 0x604. QEMU powers off when SLP_EN set and SLP_TYP=0
       (case 0 == soft power off), so try that first. */
    outw(0x604, (0 << 10) | (1 << 13));
    sys_delay_ms();

    /* standard S5 for real hardware */
    outw(0x604, (5 << 10) | (1 << 13));
    sys_delay_ms();

    /* if still running, give up and stop the CPU */
    cpu_halt();
}

void system_reboot(void)
{
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);   /* keyboard controller reset */

    outb(0xCF9, 0x06);  /* PCI / QEMU reset register */
    sys_delay_ms();

    outb(0xCF9, 0x0E);
    cpu_halt();
}
