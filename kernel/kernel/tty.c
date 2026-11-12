#include "kernel/tty.h"
#include "kernel/io.h"

static uint16_t* const vga_memory = (uint16_t*)0xB8000;
static size_t tty_row;
static size_t tty_col;
static uint8_t tty_color;
static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

static inline uint16_t vga_entry(char c, uint8_t color)
{
    return (uint16_t)c | (uint16_t)color << 8;
}

static void tty_update_cursor(void)
{
    uint16_t pos = (uint16_t)(tty_row * VGA_WIDTH + tty_col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void tty_init(void)
{
    tty_row = 0;
    tty_col = 0;
    tty_color = 0x07;
    tty_clear();
    tty_update_cursor();
}

void tty_clear(void)
{
    for (size_t y = 0; y < VGA_HEIGHT; y++)
    {
        for (size_t x = 0; x < VGA_WIDTH; x++)
        {
            vga_memory[y * VGA_WIDTH + x] = vga_entry(' ', tty_color);
        }
    }
    tty_row = 0;
    tty_col = 0;
    tty_update_cursor();
}

void tty_setcolor(uint8_t color)
{
    tty_color = color;
}

void tty_scroll(void)
{
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++)
    {
        for (size_t x = 0; x < VGA_WIDTH; x++)
        {
            vga_memory[y * VGA_WIDTH + x] = vga_memory[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (size_t x = 0; x < VGA_WIDTH; x++)
    {
        vga_memory[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', tty_color);
    }
}

void tty_putchar(char c)
{
    debug_putc(c);

    if (c == '\n')
    {
        tty_col = 0;
        tty_row++;
        if (tty_row >= VGA_HEIGHT)
        {
            tty_scroll();
            tty_row = VGA_HEIGHT - 1;
        }
        tty_update_cursor();
        return;
    }

    if (c == '\t')
    {
        tty_col = (tty_col + 8) & ~7;
        if (tty_col >= VGA_WIDTH)
        {
            tty_col = 0;
            tty_row++;
            if (tty_row >= VGA_HEIGHT)
            {
                tty_scroll();
                tty_row = VGA_HEIGHT - 1;
            }
        }
        tty_update_cursor();
        return;
    }

    if (c == '\b')
    {
        if (tty_col > 0)
        {
            tty_col--;
            tty_update_cursor();
        }
        return;
    }

    if (tty_col >= VGA_WIDTH)
    {
        tty_col = 0;
        tty_row++;
        if (tty_row >= VGA_HEIGHT)
        {
            tty_scroll();
            tty_row = VGA_HEIGHT - 1;
        }
    }

    vga_memory[tty_row * VGA_WIDTH + tty_col] = vga_entry(c, tty_color);
    tty_col++;
    tty_update_cursor();
}

void tty_write(const char* data, size_t size)
{
    for (size_t i = 0; i < size; i++)
        tty_putchar(data[i]);
}

void tty_puts(const char* str)
{
    while (*str)
        tty_putchar(*str++);
}
