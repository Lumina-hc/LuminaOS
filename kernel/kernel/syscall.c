#include <kernel/syscall.h>
#include <kernel/gfx.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>
#include <kernel/fat16.h>
#include <kernel/idt.h>
#include <kernel/pmm.h>
#include <kernel/heap.h>
#include <kernel/desktop.h>
#include <kernel/sched.h>
#include <kernel/timer.h>
#include <kernel/isr.h>
#include <stdint.h>

extern void syscall_asm(void);
extern void kernel_resume(void);

static long sc_exit(long a, long b, long c, long d)
{
    (void)a; (void)b; (void)c; (void)d;
    kernel_resume();
    return 0;
}

static long sc_putpixel(long x, long y, long color, long d)
{
    (void)d;
    gfx_begin();
    gfx_putpixel(x, y, (uint32_t)color);
    return 0;
}

static long sc_fill_rect(long x, long y, long w, long color)
{
    int h = (w >> 16) & 0xFFFF;
    w &= 0xFFFF;
    gfx_begin();
    gfx_fill_rect(x, y, w, h, (uint32_t)color);
    return 0;
}

static long sc_draw_rect(long x, long y, long w, long color)
{
    int h = (w >> 16) & 0xFFFF;
    w &= 0xFFFF;
    gfx_begin();
    gfx_draw_rect(x, y, w, h, (uint32_t)color);
    return 0;
}

static long sc_draw_char(long x, long y, long color, long d)
{
    gfx_begin();
    gfx_draw_char(x, y, (char)d, (uint32_t)color, GFX_TRANSPARENT);
    return 0;
}

static long sc_draw_text(long x, long y, long p, long d)
{
    gfx_begin();
    gfx_draw_text(x, y, (const char*)(uintptr_t)p, (uint32_t)d, GFX_TRANSPARENT);
    return 0;
}

static long sc_gfx_clear(long color, long b, long c, long d)
{
    (void)b; (void)c; (void)d;
    gfx_begin();
    gfx_clear((uint32_t)color);
    return 0;
}

static long sc_gfx_flush(long a, long b, long c, long d)
{
    (void)a; (void)b; (void)c; (void)d;
    gfx_flush();
    gfx_cursor_reset();
    gfx_draw_cursor_abs(mouse_x(), mouse_y());
    return 0;
}

static long sc_gfx_w(long a, long b, long c, long d)
{
    (void)a; (void)b; (void)c; (void)d;
    return gfx_width();
}

static long sc_gfx_h(long a, long b, long c, long d)
{
    (void)a; (void)b; (void)c; (void)d;
    return gfx_height();
}

static long sc_getchar(long a, long b, long c, long d)
{
    (void)a; (void)b; (void)c; (void)d;
    return keyboard_getchar();
}

static long sc_getchar_nb(long a, long b, long c, long d)
{
    (void)a; (void)b; (void)c; (void)d;
    return keyboard_getchar_nonblock();
}

static long sc_mouse_x(long a, long b, long c, long d)
{
    (void)a; (void)b; (void)c; (void)d;
    return mouse_x();
}
static long sc_mouse_y(long a, long b, long c, long d)
{
    (void)a; (void)b; (void)c; (void)d;
    return mouse_y();
}

static long sc_mouse_l(long a, long b, long c, long d)
{
    (void)a; (void)b; (void)c; (void)d;
    return mouse_left();
}

static long sc_mouse_r(long a, long b, long c, long d)
{
    (void)a; (void)b; (void)c; (void)d;
    return mouse_right();
}

static long sc_mouse_click(long a, long b, long c, long d)
{
    (void)a; (void)b; (void)c; (void)d;
    if (!mouse_click_peek())
        return 0;
    win_info_t wi;
    desktop_win_info(&wi);
    int mx = mouse_x();
    int my = mouse_y();
    if (mx >= wi.close_x && mx < wi.close_x + wi.close_w &&
        my >= wi.close_y && my < wi.close_y + wi.close_h)
        return 0;
    return mouse_click_consume();
}

static long sc_file_read(long a, long b, long c, long d)
{
    (void)d;
    uint32_t sz;
    static uint8_t buf[4096];
    if (fat16_read_file((const char*)(uintptr_t)a, buf, sizeof(buf), &sz) != 0)
        return -1;
    if (sz > (uint32_t)c) sz = c;
    for (uint32_t i = 0; i < sz; i++)
        ((uint8_t*)(uintptr_t)b)[i] = buf[i];
    return (int)sz;
}

static long sc_file_write(long a, long b, long c, long d)
{
    (void)d;
    return fat16_write_file((const char*)(uintptr_t)a, (const uint8_t*)(uintptr_t)b, (uint32_t)c);
}

static long sc_file_list(long a, long b, long c, long d)
{
    (void)b; (void)d;
    fat16_entry_t entries[64];
    int count = 0;
    if (fat16_list_dir(entries, 64, &count) != 0) return -1;
    dir_entry_t* out = (dir_entry_t*)(uintptr_t)a;
    int max = c / (int)sizeof(dir_entry_t);
    int n = 0;
    for (int i = 0; i < count && n < max; i++) {
        int len = 0;
        for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++)
            out[n].name[len++] = entries[i].name[j];
        if (entries[i].name[8] != ' ') {
            out[n].name[len++] = '.';
            for (int j = 8; j < 11 && entries[i].name[j] != ' '; j++)
                out[n].name[len++] = entries[i].name[j];
        }
        while (len < 12) out[n].name[len++] = ' ';
        out[n].attr = entries[i].attr;
        out[n].pad = 0;
        out[n].size = entries[i].size;
        n++;
    }
    return n;
}

static long sc_win_info(long a, long b, long c, long d)
{
    (void)b; (void)c; (void)d;
    win_info_t* info = (win_info_t*)(uintptr_t)a;
    if (!info) return -1;
    desktop_win_info(info);
    return 0;
}

static long sc_sysinfo(long a, long b, long c, long d)
{
    (void)b; (void)c; (void)d;
    sysinfo_t* info = (sysinfo_t*)(uintptr_t)a;
    if (!info) return -1;
    info->mem_total_kb = PMM_MAX_PHYS / 1024;
    info->mem_free_kb = (pmm_free_page_count() * PMM_PAGE_SIZE) / 1024;
    info->mem_used_kb = (pmm_used_page_count() * PMM_PAGE_SIZE) / 1024;
    info->heap_free_kb = heap_free_bytes() / 1024;
    info->heap_used_kb = heap_used_bytes() / 1024;
    return 0;
}

static long sc_win_should_close(long a, long b, long c, long d)
{
    (void)a; (void)b; (void)c; (void)d;
    if (keyboard_getchar_nonblock() == 27)
        return 1;
    if (mouse_click_peek()) {
        win_info_t wi;
        desktop_win_info(&wi);
        int mx = mouse_x();
        int my = mouse_y();
        if (mx >= wi.close_x && mx < wi.close_x + wi.close_w &&
            my >= wi.close_y && my < wi.close_y + wi.close_h) {
            mouse_click_consume();
            return 1;
        }
    }
    return 0;
}

static long sc_sleep(long ms, long b, long c, long d)
{
    (void)b; (void)c; (void)d;
    if (ms < 0) ms = 0;
    sched_note_yield();
    timer_wait((uint32_t)ms);
    return 0;
}

static char g_open_file[64];
static int g_has_open_file = 0;

static long sc_open_file(long a, long b, long c, long d)
{
    (void)b; (void)c; (void)d;
    const char* src = (const char*)(uintptr_t)a;
    int i = 0;
    while (src[i] && i < (int)sizeof(g_open_file) - 1) {
        g_open_file[i] = src[i];
        i++;
    }
    g_open_file[i] = '\0';
    g_has_open_file = 1;
    return 0;
}

static long sc_get_open_file(long a, long b, long c, long d)
{
    (void)b; (void)d;
    if (!g_has_open_file) return 0;
    char* dst = (char*)(uintptr_t)a;
    int i = 0;
    while (g_open_file[i] && i < c - 1) {
        dst[i] = g_open_file[i];
        i++;
    }
    dst[i] = '\0';
    g_has_open_file = 0;
    return i;
}

int syscall_pending_file(char* out, int max)
{
    if (!g_has_open_file) return 0;
    int i = 0;
    while (g_open_file[i] && i < max - 1) {
        out[i] = g_open_file[i];
        i++;
    }
    out[i] = '\0';
    return i;
}

static long sc_cursor(long a, long b, long c, long d)
{
    (void)a; (void)b; (void)c; (void)d;
    gfx_begin();
    gfx_draw_cursor_abs(mouse_x(), mouse_y());
    gfx_flush();
    return 0;
}

typedef long (*sc_fn_t)(long, long, long, long);

static const sc_fn_t syscall_table[SYSCALL_COUNT] = {
    [SYSCALL_EXIT]       = sc_exit,
    [SYSCALL_PUTPIXEL]   = sc_putpixel,
    [SYSCALL_FILL_RECT]  = sc_fill_rect,
    [SYSCALL_DRAW_RECT]  = sc_draw_rect,
    [SYSCALL_DRAW_CHAR]  = sc_draw_char,
    [SYSCALL_DRAW_TEXT]  = sc_draw_text,
    [SYSCALL_GFX_CLEAR]  = sc_gfx_clear,
    [SYSCALL_GFX_FLUSH]  = sc_gfx_flush,
    [SYSCALL_GFX_W]      = sc_gfx_w,
    [SYSCALL_GFX_H]      = sc_gfx_h,
    [SYSCALL_GETCHAR]    = sc_getchar,
    [SYSCALL_GETCHAR_NB] = sc_getchar_nb,
    [SYSCALL_MOUSE_X]    = sc_mouse_x,
    [SYSCALL_MOUSE_Y]    = sc_mouse_y,
    [SYSCALL_MOUSE_L]    = sc_mouse_l,
    [SYSCALL_MOUSE_R]    = sc_mouse_r,
    [SYSCALL_MOUSE_CLICK]= sc_mouse_click,
    [SYSCALL_FILE_READ]  = sc_file_read,
    [SYSCALL_FILE_WRITE] = sc_file_write,
    [SYSCALL_FILE_LIST]  = sc_file_list,
    [SYSCALL_SLEEP]      = sc_sleep,
    [SYSCALL_CURSOR]     = sc_cursor,
    [SYSCALL_WIN_INFO]   = sc_win_info,
    [SYSCALL_SYSINFO]    = sc_sysinfo,
    [SYSCALL_WIN_SHOULD_CLOSE] = sc_win_should_close,
    [SYSCALL_OPEN_FILE] = sc_open_file,
    [SYSCALL_GET_OPEN_FILE] = sc_get_open_file,
};

void syscall_init(void)
{
    idt_set_gate(0x80, (uint64_t)(uintptr_t)syscall_asm, 0x08, 0xEE);
}

long syscall_dispatch(long num, long a1, long a2, long a3, long a4)
{
    sched_note_yield();
    if (num < 0 || num >= SYSCALL_COUNT || !syscall_table[num])
        return -1;
    return syscall_table[num](a1, a2, a3, a4);
}

long syscall_asm_handler(isr_regs_t* regs)
{
    long ret = syscall_dispatch((long)regs->rax, (long)regs->rdi, (long)regs->rsi,
                               (long)regs->rdx, (long)regs->rcx);
    regs->rax = (uint64_t)ret;
    return ret;
}