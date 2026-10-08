#ifndef IO_H
#define IO_H

#include <stdint.h>

#define COM1 0x3F8

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void io_wait(void)
{
    outb(0x80, 0);
}

static inline void debug_putc(char c)
{
    outb(0xE9, c);
}

static inline void debug_puts(const char* s)
{
    while (*s) debug_putc(*s++);
}

static inline void serial_putc(char c)
{
    while ((inb(COM1 + 5) & 0x20) == 0);
    outb(COM1, c);
}

static inline void serial_puts(const char* s)
{
    while (*s) {
        serial_putc(*s++);
        if (*(s-1) == '\n') serial_putc('\r');
    }
}

#endif
