#include "kernel/keyboard.h"
#include "kernel/irq.h"
#include "kernel/io.h"

#define KEYBOARD_BUFFER_SIZE 256

static volatile char kb_buffer[KEYBOARD_BUFFER_SIZE];
static volatile int kb_head = 0;
static volatile int kb_tail = 0;
static volatile int kb_shift = 0;

static const char kbd_us[128] =
{
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8',
    '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r',
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`', 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n',
    'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, '-', 0, 0, 0, '+', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0
};

static const char kbd_us_shift[128] =
{
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*',
    '(', ')', '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R',
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"', '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N',
    'M', '<', '>', '?', 0, '*', 0, ' ', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, '-', 0, 0, 0, '+', 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0
};

static void keyboard_handle(uint8_t scancode)
{
    if (scancode & 0x80)
    {
        scancode &= 0x7F;
        if (scancode == 0x2A || scancode == 0x36)
            kb_shift = 0;
        return;
    }

    if (scancode == 0x2A || scancode == 0x36)
    {
        kb_shift = 1;
        return;
    }

    char c = kb_shift ? kbd_us_shift[scancode] : kbd_us[scancode];

    if (c)
    {
        int next = (kb_head + 1) % KEYBOARD_BUFFER_SIZE;
        if (next != kb_tail)
        {
            kb_buffer[kb_head] = c;
            kb_head = next;
        }
    }
}

static void keyboard_callback(void)
{
    uint8_t st = inb(0x64);
    if ((st & 0x21) != 0x01)
        return;
    uint8_t scancode = inb(0x60);
    keyboard_handle(scancode);
}

void keyboard_init(void)
{
    irq_install_handler(1, keyboard_callback);
}

char keyboard_getchar(void)
{
    while (kb_head == kb_tail)
    {
    }

    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

int keyboard_getchar_nonblock(void)
{
    if (kb_head == kb_tail)
        return -1;

    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

int keyboard_peek(void)
{
    if (kb_head == kb_tail)
        return -1;
    return (unsigned char)kb_buffer[kb_tail];
}
