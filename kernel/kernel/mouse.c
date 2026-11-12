#include "kernel/mouse.h"
#include "kernel/irq.h"
#include "kernel/io.h"
#define PS2_CMD  0x64
#define PS2_DATA 0x60

static volatile int mouse_avail;
static volatile int mouse_pos_x;
static volatile int mouse_pos_y;
static volatile int mouse_btn_left;
static volatile int mouse_btn_right;
static volatile int mouse_click_left;
static int mouse_max_x = 799;
static int mouse_max_y = 599;

static void mouse_wait_write(void)
{
    for (int i = 0; i < 100000; i++) {
        if (!(inb(PS2_CMD) & 0x02)) return;
    }
}

static void mouse_wait_read(void)
{
    for (int i = 0; i < 100000; i++) {
        if (inb(PS2_CMD) & 0x01) return;
    }
}
static void mouse_cmd(uint8_t cmd)
{
    mouse_wait_write();
    outb(PS2_CMD, 0xD4);
    mouse_wait_write();
    outb(PS2_DATA, cmd);
    mouse_wait_read();
    inb(PS2_DATA);
}

static void mouse_callback(void)
{
    uint8_t st = inb(PS2_CMD);
    if ((st & 0x21) != 0x21)
        return;
    uint8_t data = inb(PS2_DATA);
    static uint8_t packet[3];
    static int cycle = 0;

    if (cycle == 0 && !(data & 0x08))
        return;

    packet[cycle] = data;
    cycle++;
    if (cycle == 3) {
        cycle = 0;
        if (!(packet[0] & 0x08)) return;

        int dx = (int)(int8_t)packet[1];
        int dy = (int)(int8_t)packet[2];

        mouse_pos_x += dx;
        mouse_pos_y -= dy;
        if (mouse_pos_x < 0) mouse_pos_x = 0;
        if (mouse_pos_y < 0) mouse_pos_y = 0;
        if (mouse_pos_x > mouse_max_x) mouse_pos_x = mouse_max_x;
        if (mouse_pos_y > mouse_max_y) mouse_pos_y = mouse_max_y;

        mouse_btn_left = packet[0] & 0x01;
        mouse_btn_right = packet[0] & 0x02;
        if (mouse_btn_left) mouse_click_left = 1;
    }
}

void mouse_init(void)
{
    mouse_wait_write();
    outb(PS2_CMD, 0xA8);
    mouse_wait_read();
    inb(PS2_DATA);

    mouse_wait_write();
    outb(PS2_CMD, 0x20);
    mouse_wait_read();
    uint8_t status = inb(PS2_DATA);
    status |= 0x02;
    mouse_wait_write();
    outb(PS2_CMD, 0x60);
    mouse_wait_write();
    outb(PS2_DATA, status);

    mouse_cmd(0xF6);
    mouse_cmd(0xF4);

    mouse_avail = 1;

    irq_install_handler(12, mouse_callback);
}

void mouse_set_bounds(int w, int h)
{
    if (w > 1) mouse_max_x = w - 1;
    if (h > 1) mouse_max_y = h - 1;
    mouse_pos_x = mouse_max_x / 2;
    mouse_pos_y = mouse_max_y / 2;
}

void mouse_set_pos(int x, int y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > mouse_max_x) x = mouse_max_x;
    if (y > mouse_max_y) y = mouse_max_y;
    mouse_pos_x = x;
    mouse_pos_y = y;
}

int mouse_available(void)
{
    return mouse_avail;
}

int mouse_x(void)
{
    return mouse_pos_x;
}

int mouse_y(void)
{
    return mouse_pos_y;
}

int mouse_left(void)
{
    return mouse_btn_left;
}

int mouse_right(void)
{
    return mouse_btn_right;
}

int mouse_click_peek(void)
{
    return mouse_click_left;
}

int mouse_click_consume(void)
{
    if (mouse_click_left) {
        mouse_click_left = 0;
        return 1;
    }
    return 0;
}
