#include "kernel/desktop.h"
#include "kernel/gfx.h"
#include "kernel/mouse.h"
#include "kernel/keyboard.h"
#include "kernel/system.h"
#include "kernel/proc.h"
#include "kernel/syscall.h"
#include "kernel/paging.h"
#include "kernel/timer.h"
#include "kernel/gdt.h"
#include "kernel/io.h"
#include "kernel/sched.h"
#include "kernel/tty.h"
#include <stdint.h>

#define FONT_W 8
#define FONT_H 16

#define TASKBAR_H 44
#define ICON_W 64
#define ICON_H 64
#define ICON_LABEL_H 20
#define ICON_CELL_X 96
#define ICON_CELL_Y 100

#define USER_BASE_ADDR 0x08048000
#define USER_STACK_TOP 0x7FF00000
#define USER_STACK_PAGES 16

/* Modern palette */
#define COL_WALL_TOP 0x14213D
#define COL_WALL_MID 0x22355A
#define COL_WALL_BOT 0x0E1A30
#define COL_TASKBAR  0x161B26
#define COL_TASKBAR2 0x1E2533
#define COL_ACCENT   0x4C8DFF
#define COL_ACCENT2  0x7B5BFF
#define COL_TEXT     0xE8ECF4
#define COL_TEXT_DIM 0x9AA4B8
#define COL_LINE     0x2A3346

extern void enter_user_mode(uint64_t rip, uint64_t rsp, uint64_t cs, uint64_t ss, uint64_t rflags);
extern void kernel_resume(void);
extern void resume_user_mode(isr_regs_t* ctx);

typedef struct {
    const char* name;
    uint32_t color;
    const char* elf;
    int action;
} desktop_icon_t;

enum {
    APP_NONE = 0,
    APP_LAUNCH,
    APP_REBOOT,
    APP_HALT
};

static const desktop_icon_t icons[] = {
    { "Files",    0x4C8DFF, "FILES.ELF",    APP_LAUNCH },
    { "Notepad",  0x39C48B, "NOTEPAD.ELF",  APP_LAUNCH },
    { "About",    0xE0913A, "ABOUT.ELF",    APP_LAUNCH },
    { "System",   0x9B8CFF, "SYSTEM.ELF",   APP_LAUNCH },
    { "Terminal", 0x2FBF9F, "TERMINAL.ELF", APP_LAUNCH },
    { "Reboot",   0xE05A5A, 0,              APP_REBOOT },
    { "Shutdown", 0x8A93A6, 0,              APP_HALT  },
};
#define ICON_COUNT ((int)(sizeof(icons) / sizeof(icons[0])))

/* Minimized (suspended) applications shown as taskbar pills. */
#define MAX_MIN 6
#define MAX_PILLS 8

typedef struct {
    int x, y, w, h;
    int action;        /* 0 none, 1 launch terminal, 2 restore app */
    proc_t* proc;
} dt_pill_t;

static dt_pill_t g_pills[MAX_PILLS];
static int g_pill_count = 0;

static proc_t* g_min[MAX_MIN];
static int g_min_count = 0;
static const char* g_user = "guest";
static proc_t* g_foreground = 0;
static int g_hover_icon = -1;

#define DT_MAX_WIN 4

typedef struct {
    proc_t* proc;
    int x, y, w, h;
} dt_win_t;

static dt_win_t g_wins[DT_MAX_WIN];

#define DT_TRACE_N 24
typedef struct {
    int pid;
    uint64_t rip;
    uint64_t rsp;
} dt_trace_t;

static dt_trace_t g_trace[DT_TRACE_N];
static int g_trace_i;

static void dt_trace_note(proc_t* p, uint64_t rip, uint64_t rsp)
{
    dt_trace_t* t = &g_trace[g_trace_i % DT_TRACE_N];
    t->pid = p ? p->pid : 0;
    t->rip = rip;
    t->rsp = rsp;
    g_trace_i++;
}

static void dt_trace_dump(void)
{
    tty_puts("--- trace ---\n");
    int n = g_trace_i < DT_TRACE_N ? g_trace_i : DT_TRACE_N;
    for (int i = 0; i < n; i++) {
        dt_trace_t* t = &g_trace[(g_trace_i - n + i) % DT_TRACE_N];
        tty_puts("  t pid=");
        tty_putchar('0' + (t->pid % 10));
        tty_puts(" rip=0x");
        for (int b = 60; b >= 0; b -= 4) {
            uint8_t d = (uint8_t)((t->rip >> b) & 0xF);
            tty_putchar(d < 10 ? ('0' + d) : ('A' + d - 10));
        }
        tty_puts(" rsp=0x");
        for (int b = 60; b >= 0; b -= 4) {
            uint8_t d = (uint8_t)((t->rsp >> b) & 0xF);
            tty_putchar(d < 10 ? ('0' + d) : ('A' + d - 10));
        }
        tty_puts("\n");
    }
}

void desktop_dbg_dump(void)
{
    dt_trace_dump();
}

static void dt_slot_rect(int idx, int* px, int* py, int* pw, int* ph);
static dt_win_t* dt_win_of(proc_t* p);
static int dt_win_has_free(void);
static dt_win_t* dt_win_alloc(proc_t* p);
static void dt_win_release(proc_t* p, int* rx, int* ry, int* rw, int* rh);
static dt_win_t* dt_win_at(int x, int y);
static void dt_win_client(const dt_win_t* w, int* px, int* py, int* pw, int* ph);
static proc_t* dt_pick_focus(void);
static void dt_paint_rect(int rx, int ry, int rw, int rh);
static void dt_flush_rect(int x, int y, int w, int h);
static void dt_repaint_rect(int x, int y, int w, int h);
static void dt_paint_full(void);
static void dt_refresh_frame(proc_t* p);
static void dt_focus(proc_t* p);
static void dt_draw_window_frame(const dt_win_t* w, const char* title, int focused, int with_body);

proc_t* desktop_foreground(void)
{
    return g_foreground;
}

static void dt_add_min(proc_t* p)
{
    if (g_min_count < MAX_MIN) g_min[g_min_count++] = p;
}

static void dt_remove_min(proc_t* p)
{
    for (int i = 0; i < g_min_count; i++) {
        if (g_min[i] == p) {
            for (int j = i; j < g_min_count - 1; j++) g_min[j] = g_min[j + 1];
            g_min_count--;
            return;
        }
    }
}

static int scr_w(void) { int w = gfx_width();  return w > 0 ? w : 800; }
static int scr_h(void) { int h = gfx_height(); return h > 0 ? h : 600; }

static int icon_x(int i) { return 28 + (i % 3) * ICON_CELL_X; }
static int icon_y(int i) { return 28 + (i / 3) * ICON_CELL_Y; }

static void dt_slot_rect(int idx, int* px, int* py, int* pw, int* ph)
{
    int W = scr_w(), H = scr_h();
    int gx = 344, gy = 24, gap = 12;
    int aw = W - gx - 16;
    int ah = H - TASKBAR_H - gy - 16;
    int ww = (aw - gap) / 2;
    int wh = (ah - gap) / 2;
    if (ww < 260) ww = 260;
    if (wh < 200) wh = 200;
    int col = idx % 2, row = idx / 2;
    *px = gx + col * (ww + gap);
    *py = gy + row * (wh + gap);
    *pw = ww;
    *ph = wh;
}

static dt_win_t* dt_win_of(proc_t* p)
{
    if (!p) return 0;
    for (int i = 0; i < DT_MAX_WIN; i++)
        if (g_wins[i].proc == p) return &g_wins[i];
    return 0;
}

static int dt_win_has_free(void)
{
    for (int i = 0; i < DT_MAX_WIN; i++)
        if (!g_wins[i].proc) return 1;
    return 0;
}

static dt_win_t* dt_win_alloc(proc_t* p)
{
    for (int i = 0; i < DT_MAX_WIN; i++) {
        if (!g_wins[i].proc) {
            g_wins[i].proc = p;
            dt_slot_rect(i, &g_wins[i].x, &g_wins[i].y, &g_wins[i].w, &g_wins[i].h);
            return &g_wins[i];
        }
    }
    return 0;
}

static void dt_win_release(proc_t* p, int* rx, int* ry, int* rw, int* rh)
{
    for (int i = 0; i < DT_MAX_WIN; i++) {
        if (g_wins[i].proc == p) {
            if (rx) { *rx = g_wins[i].x; *ry = g_wins[i].y; *rw = g_wins[i].w; *rh = g_wins[i].h; }
            g_wins[i].proc = 0;
            return;
        }
    }
    if (rx) *rw = 0;
}

static dt_win_t* dt_win_at(int x, int y)
{
    for (int i = 0; i < DT_MAX_WIN; i++) {
        dt_win_t* w = &g_wins[i];
        if (!w->proc) continue;
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h) return w;
    }
    return 0;
}

static void dt_win_client(const dt_win_t* w, int* px, int* py, int* pw, int* ph)
{
    *px = w->x + 1;
    *py = w->y + 41;
    *pw = w->w - 2;
    *ph = w->h - 42;
}

static proc_t* dt_pick_focus(void)
{
    for (int i = DT_MAX_WIN - 1; i >= 0; i--)
        if (g_wins[i].proc) return g_wins[i].proc;
    return 0;
}

static void fill_round_rect(int x, int y, int w, int h, int r, uint32_t c)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r < 0) r = 0;
    gfx_fill_rect(x + r, y, w - 2 * r, h, c);
    gfx_fill_rect(x, y + r, w, h - 2 * r, c);
    for (int cy = 0; cy < r; cy++) {
        for (int cx = 0; cx < r; cx++) {
            int dx = r - cx, dy = r - cy;
            if (dx * dx + dy * dy <= r * r) {
                gfx_fill_rect(x + cx, y + cy, 1, 1, c);
                gfx_fill_rect(x + w - 1 - cx, y + cy, 1, 1, c);
                gfx_fill_rect(x + cx, y + h - 1 - cy, 1, 1, c);
                gfx_fill_rect(x + w - 1 - cx, y + h - 1 - cy, 1, 1, c);
            }
        }
    }
}

static void dt_draw_wallpaper_rect(int rx, int ry, int rw, int rh)
{
    int W = scr_w(), H = scr_h();
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > W) rw = W - rx;
    if (ry + rh > H) rh = H - ry;
    if (rw <= 0 || rh <= 0) return;

    int half = H / 2;
    for (int y = ry; y < ry + rh; y++) {
        uint32_t c;
        if (y < half) {
            int t = (y * 255) / (half ? half : 1);
            int r = ((COL_WALL_TOP >> 16) & 0xFF) + ((((COL_WALL_MID >> 16) & 0xFF) - ((COL_WALL_TOP >> 16) & 0xFF)) * t / 255);
            int g = ((COL_WALL_TOP >> 8) & 0xFF) + ((((COL_WALL_MID >> 8) & 0xFF) - ((COL_WALL_TOP >> 8) & 0xFF)) * t / 255);
            int b = (COL_WALL_TOP & 0xFF) + (((COL_WALL_MID & 0xFF) - (COL_WALL_TOP & 0xFF)) * t / 255);
            c = (r << 16) | (g << 8) | b;
        } else {
            int t = ((y - half) * 255) / ((H - half) ? (H - half) : 1);
            int r = ((COL_WALL_MID >> 16) & 0xFF) + ((((COL_WALL_BOT >> 16) & 0xFF) - ((COL_WALL_MID >> 16) & 0xFF)) * t / 255);
            int g = ((COL_WALL_MID >> 8) & 0xFF) + ((((COL_WALL_BOT >> 8) & 0xFF) - ((COL_WALL_MID >> 8) & 0xFF)) * t / 255);
            int b = (COL_WALL_MID & 0xFF) + (((COL_WALL_BOT & 0xFF) - (COL_WALL_MID & 0xFF)) * t / 255);
            c = (r << 16) | (g << 8) | b;
        }
        gfx_fill_rect(rx, y, rw, 1, c);
    }

    int gy = H / 4;
    for (int i = 0; i < 3; i++) {
        int rr = 180 + i * 120;
        uint32_t col = (i == 0) ? 0x1B2A4A : (i == 1 ? 0x2A2350 : 0x14283C);
        int cy = gy + i * 90;
        int y0 = cy - rr; if (y0 < ry) y0 = ry;
        int y1 = cy + rr; if (y1 > ry + rh - 1) y1 = ry + rh - 1;
        int cx = W - 220 + i * 40;
        for (int yy = y0; yy <= y1; yy += 2) {
            int dy = yy - cy;
            int span = rr * rr - dy * dy;
            if (span <= 0) continue;
            int sx = 0;
            while (sx * sx < span) sx++;
            int x0 = cx - sx; if (x0 < rx) x0 = rx;
            int x1 = cx + sx; if (x1 > rx + rw) x1 = rx + rw;
            if (x1 > x0)
                gfx_fill_rect(x0, yy, x1 - x0, 1, col);
        }
    }
}


static void dt_draw_icon(int idx, int hover)
{
    const desktop_icon_t* ic = &icons[idx];
    int x = icon_x(idx);
    int y = icon_y(idx);

    if (hover) {
        fill_round_rect(x - 8, y - 8, ICON_W + 16, ICON_H + ICON_LABEL_H + 16, 10, 0x2A3550);
    }

    /* tile */
    fill_round_rect(x, y, ICON_W, ICON_H, 14, ic->color);
    /* top gloss */
    fill_round_rect(x + 4, y + 4, ICON_W - 8, ICON_H / 2 - 4, 10, 0xFFFFFF & 0x00303030);
    gfx_fill_rect(x + 6, y + 5, ICON_W - 12, 2, 0x60FFFFFF);

    /* inner glyph block */
    gfx_fill_rect(x + 14, y + 16, ICON_W - 28, ICON_H - 32, 0xE0FFFFFF);
    gfx_fill_rect(x + 20, y + 24, ICON_W - 40, 3, ic->color);
    gfx_fill_rect(x + 20, y + 31, ICON_W - 40, 3, ic->color);
    gfx_fill_rect(x + 20, y + 38, ICON_W - 44, 3, ic->color);

    int tw = gfx_text_width(ic->name);
    int tx = x + (ICON_W - tw) / 2;
    if (tx < x) tx = x;
    gfx_draw_text(tx, y + ICON_H + 5, ic->name, COL_TEXT, GFX_TRANSPARENT);
}

static uint8_t cmos_read(uint8_t reg)
{
    outb(0x70, reg);
    return inb(0x71);
}

static void rtc_time(int* hh, int* mm, int* ss)
{
    uint32_t guard = 0;
    while ((cmos_read(0x0A) & 0x80) && guard++ < 1000000) {}
    int sec = cmos_read(0x00);
    int min = cmos_read(0x02);
    int hour = cmos_read(0x04);
    uint8_t regB = cmos_read(0x0B);
    int pm = 0;
    if (!(regB & 0x02) && (hour & 0x80)) {
        pm = 1;
        hour &= 0x7F;
    }
    if (!(regB & 0x04)) {
        sec = (sec & 0x0F) + ((sec >> 4) * 10);
        min = (min & 0x0F) + ((min >> 4) * 10);
        hour = (hour & 0x0F) + ((hour >> 4) * 10);
    }
    if (!(regB & 0x02)) {
        if (pm && hour < 12) hour += 12;
        if (!pm && hour == 12) hour = 0;
    }
    *ss = sec; *mm = min; *hh = hour;
}

static void two_digit(char* p, int v)
{
    if (v < 0) v = 0;
    p[0] = (char)('0' + (v / 10) % 10);
    p[1] = (char)('0' + v % 10);
}

static void dt_draw_taskbar(const char* user)
{
    int W = scr_w(), H = scr_h();
    int ty = H - TASKBAR_H;

    gfx_fill_rect(0, ty, W, TASKBAR_H, COL_TASKBAR);
    gfx_fill_rect(0, ty, W, 2, COL_ACCENT);

    /* start button */
    fill_round_rect(10, ty + 7, 150, TASKBAR_H - 14, 9, COL_TASKBAR2);
    gfx_fill_rect(22, ty + 14, 14, 14, COL_ACCENT);
    gfx_fill_rect(24, ty + 16, 10, 10, COL_TASKBAR2);
    gfx_draw_text(46, ty + 14, "Lumina", COL_TEXT, GFX_TRANSPARENT);

    /* app pills: terminal launcher + minimized windows */
    g_pill_count = 0;
    int px = 180;

    {
        const char* lbl = "Terminal";
        int w = gfx_text_width(lbl) + 28;
        fill_round_rect(px, ty + 9, w, TASKBAR_H - 18, 8, 0x1B2130);
        gfx_draw_text(px + 14, ty + 14, lbl, COL_TEXT_DIM, GFX_TRANSPARENT);
        if (g_pill_count < MAX_PILLS) {
            g_pills[g_pill_count].x = px;
            g_pills[g_pill_count].y = ty + 9;
            g_pills[g_pill_count].w = w;
            g_pills[g_pill_count].h = TASKBAR_H - 18;
            g_pills[g_pill_count].action = 1;
            g_pills[g_pill_count].proc = 0;
            g_pill_count++;
        }
        px += w + 8;
    }

    for (int i = 0; i < g_min_count; i++) {
        const char* lbl = g_min[i]->name;
        int w = gfx_text_width(lbl) + 28;
        fill_round_rect(px, ty + 9, w, TASKBAR_H - 18, 8, 0x2A3550);
        gfx_draw_text(px + 14, ty + 14, lbl, COL_TEXT, GFX_TRANSPARENT);
        if (g_pill_count < MAX_PILLS) {
            g_pills[g_pill_count].x = px;
            g_pills[g_pill_count].y = ty + 9;
            g_pills[g_pill_count].w = w;
            g_pills[g_pill_count].h = TASKBAR_H - 18;
            g_pills[g_pill_count].action = 2;
            g_pills[g_pill_count].proc = g_min[i];
            g_pill_count++;
        }
        px += w + 8;
    }

    /* user */
    char ubuf[40];
    int n = 0;
    const char* pre = "User: ";
    while (*pre) ubuf[n++] = *pre++;
    if (user && user[0]) { while (*user && n < 34) ubuf[n++] = *user++; }
    else { const char* g = "guest"; while (*g && n < 34) ubuf[n++] = *g++; }
    ubuf[n] = '\0';

    int uw = gfx_text_width(ubuf);

    /* clock */
    int hh, mm, ss;
    rtc_time(&hh, &mm, &ss);
    char clk[8];
    two_digit(clk + 0, hh); clk[2] = ':';
    two_digit(clk + 3, mm); clk[5] = '\0';
    int cw = gfx_text_width(clk);

    int cbox_w = cw + 24;
    int cbox_x = W - cbox_w - 12;
    fill_round_rect(cbox_x, ty + 9, cbox_w, TASKBAR_H - 18, 8, COL_TASKBAR2);
    gfx_draw_text(cbox_x + 12, ty + 14, clk, COL_TEXT, GFX_TRANSPARENT);

    int ux = cbox_x - uw - 20;
    if (ux > 180 + 400) gfx_draw_text(ux, ty + 14, ubuf, COL_TEXT_DIM, GFX_TRANSPARENT);
}

static void dt_draw_window_frame(const dt_win_t* w, const char* title, int focused, int with_body)
{
    int wx = w->x, wy = w->y, ww = w->w, wh = w->h;

    if (with_body) {
        fill_round_rect(wx - 1, wy - 1, ww + 2, wh + 2, 12, 0x0B0F18);
        fill_round_rect(wx, wy, ww, wh, 11, 0xF4F6FA);
    }

    uint32_t c0 = focused ? 0x3A5CFF : 0x2C3549;
    uint32_t c1 = focused ? 0x6E4BFF : 0x3B4763;
    for (int yy = 0; yy < 40; yy++) {
        int t = (yy * 255) / 39;
        int r = ((c0 >> 16) & 0xFF) + ((((c1 >> 16) & 0xFF) - ((c0 >> 16) & 0xFF)) * t / 255);
        int g = ((c0 >> 8) & 0xFF) + ((((c1 >> 8) & 0xFF) - ((c0 >> 8) & 0xFF)) * t / 255);
        int b = (c0 & 0xFF) + (((c1 & 0xFF) - (c0 & 0xFF)) * t / 255);
        gfx_fill_rect(wx + 1, wy + 1 + yy, ww - 2, 1, (r << 16) | (g << 8) | b);
    }
    gfx_draw_text(wx + 14, wy + 12, title, focused ? 0xFFFFFF : 0xD8DEEC, GFX_TRANSPARENT);

    fill_round_rect(wx + ww - 72, wy + 12, 16, 16, 4, 0xE8ECF4);
    fill_round_rect(wx + ww - 46, wy + 12, 16, 16, 4, 0xFF6B6B);
    gfx_draw_text(wx + ww - 42, wy + 12, "x", 0xFFFFFF, GFX_TRANSPARENT);

    gfx_draw_rect(wx, wy, ww, wh, focused ? 0x7B5BFF : 0xC8CEDB);
}

static void dt_select_client(void)
{
    dt_win_t* w = dt_win_of(g_foreground);
    if (!w) {
        gfx_clear_window();
        return;
    }
    int cx, cy, cw, ch;
    dt_win_client(w, &cx, &cy, &cw, &ch);
    gfx_set_window(cx, cy, cw, ch);
}

static void dt_flush_rect(int x, int y, int w, int h)
{
    gfx_clear_window();
    gfx_flush_rect(x, y, w, h);
}

static void dt_paint_rect(int rx, int ry, int rw, int rh)
{
    int W = scr_w(), H = scr_h();
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > W) rw = W - rx;
    if (ry + rh > H) rh = H - ry;
    if (rw <= 0 || rh <= 0) return;

    gfx_clear_window();
    gfx_begin();
    dt_draw_wallpaper_rect(rx, ry, rw, rh);

    for (int i = 0; i < ICON_COUNT; i++) {
        int ix = icon_x(i) - 10, iy = icon_y(i) - 10;
        int iw = ICON_W + 20, ih = ICON_H + ICON_LABEL_H + 20;
        if (ix + iw <= rx || rx + rw <= ix || iy + ih <= ry || ry + rh <= iy) continue;
        dt_draw_icon(i, i == g_hover_icon);
    }

    int ty = H - TASKBAR_H;
    if (!(ry + rh <= ty || ty + TASKBAR_H <= ry))
        dt_draw_taskbar(g_user);

    for (int i = 0; i < DT_MAX_WIN; i++) {
        dt_win_t* w = &g_wins[i];
        if (!w->proc) continue;
        if (w->x + w->w <= rx || rx + rw <= w->x) continue;
        if (w->y + w->h <= ry || ry + rh <= w->y) continue;
        dt_draw_window_frame(w, w->proc->name, w->proc == g_foreground, 0);
    }
}

static void dt_repaint_rect(int x, int y, int w, int h)
{
    dt_paint_rect(x, y, w, h);
    dt_flush_rect(x, y, w, h);
}

static void dt_paint_full(void)
{
    dt_paint_rect(0, 0, scr_w(), scr_h());
    dt_flush_rect(0, 0, scr_w(), scr_h());
}

static void dt_show_window(dt_win_t* w, const char* title)
{
    int cx, cy, cw, ch;
    dt_win_client(w, &cx, &cy, &cw, &ch);

    gfx_clear_window();
    gfx_begin();
    dt_draw_wallpaper_rect(w->x - 2, w->y - 2, w->w + 4, w->h + 4);
    dt_draw_window_frame(w, title, 1, 1);
    gfx_fill_rect(cx, cy, cw, ch, 0x101418);
    dt_flush_rect(w->x - 2, w->y - 2, w->w + 4, w->h + 4);
}

static void dt_refresh_frame(proc_t* p)
{
    dt_win_t* w = dt_win_of(p);
    if (!w) return;
    gfx_clear_window();
    gfx_begin();
    dt_draw_window_frame(w, p->name, p == g_foreground, 0);
    dt_flush_rect(w->x - 1, w->y - 1, w->w + 2, w->h + 2);
}

static void dt_focus(proc_t* p)
{
    proc_t* old = g_foreground;
    if (old == p) return;
    g_foreground = p;
    if (old) dt_refresh_frame(old);
    if (p) dt_refresh_frame(p);
}

static int dt_point_in(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

void desktop_win_info(win_info_t* info)
{
    dt_win_t* w = dt_win_of(proc_current());
    dt_win_t tmp;
    if (!w) {
        tmp.proc = 0;
        dt_slot_rect(0, &tmp.x, &tmp.y, &tmp.w, &tmp.h);
        w = &tmp;
    }
    info->win_x = w->x;
    info->win_y = w->y;
    info->win_w = w->w;
    info->win_h = w->h;
    info->client_x = w->x + 1;
    info->client_y = w->y + 41;
    info->client_w = w->w - 2;
    info->client_h = w->h - 42;
    info->close_x = w->x + w->w - 46;
    info->close_y = w->y + 12;
    info->close_w = 16;
    info->close_h = 16;
    info->min_x = w->x + w->w - 72;
    info->min_y = w->y + 12;
    info->min_w = 16;
    info->min_h = 16;
}

static void dt_suspend_proc(proc_t* proc)
{
    int rx = 0, ry = 0, rw = 0, rh = 0;
    dt_win_release(proc, &rx, &ry, &rw, &rh);
    dt_add_min(proc);
    proc->minimized = 1;
    proc_set_current(0);
    if (g_foreground == proc)
        g_foreground = dt_pick_focus();
    if (rw > 0)
        dt_repaint_rect(rx - 2, ry - 2, rw + 4, rh + 4);
    if (g_foreground)
        dt_refresh_frame(g_foreground);
    dt_select_client();
}

static void dt_close_proc(proc_t* proc)
{
    int rx = 0, ry = 0, rw = 0, rh = 0;
    dt_win_release(proc, &rx, &ry, &rw, &rh);
    if (g_foreground == proc)
        g_foreground = dt_pick_focus();
    mouse_click_consume();
    proc_destroy(proc);
    proc_set_current(0);
    if (rw > 0)
        dt_repaint_rect(rx - 2, ry - 2, rw + 4, rh + 4);
    if (g_foreground)
        dt_refresh_frame(g_foreground);
    dt_select_client();
}

static void dt_finish_proc(proc_t* proc)
{
    if (!proc) return;
    if (proc->state == PROC_SUSPENDED) {
        dt_suspend_proc(proc);
    } else if (proc->state == PROC_ZOMBIE) {
        dt_close_proc(proc);
    } else {
        if (proc->state != PROC_READY && proc->state != PROC_SLEEPING && proc->state != PROC_BLOCKED)
            proc->state = PROC_READY;
        gfx_clear_window();
        proc_set_current(0);
    }
}

static void dt_run_app(const char* title, const char* path, const char* user)
{
    (void)user;
    if (!dt_win_has_free()) return;

    proc_t* proc = proc_create(path, title);
    if (!proc) return;

    proc->gui = 1;
    dt_win_t* win = dt_win_alloc(proc);
    if (!win) {
        proc_destroy(proc);
        return;
    }

    dt_focus(proc);
    dt_show_window(win, title);
    dt_select_client();
    proc_set_current(proc);

    uint64_t saved_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(saved_cr3));
    paging_switch_user_pt(proc->page_dir_phys);

    dt_trace_note(proc, proc->entry_eip, proc->user_esp);
    enter_user_mode(proc->entry_eip, proc->user_esp,
                    GDT_USER_CS | 3, GDT_USER_DS | 3, 0x202);

    paging_switch_user_pt(saved_cr3);
    dt_finish_proc(proc);
}

static void dt_run_proc(proc_t* proc)
{
    if (!proc) return;
    proc->state = PROC_RUNNING;
    proc_set_current(proc);

    if (proc == g_foreground)
        dt_select_client();
    else
        gfx_clear_window();

    uint64_t saved_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(saved_cr3));
    paging_switch_user_pt(proc->page_dir_phys);

    if (proc->has_ctx) {
        dt_trace_note(proc, proc->ctx.rip, proc->ctx.rsp);
        resume_user_mode(&proc->ctx);
    } else {
        dt_trace_note(proc, proc->entry_eip, proc->user_esp);
        enter_user_mode(proc->entry_eip, proc->user_esp,
                        GDT_USER_CS | 3, GDT_USER_DS | 3, 0x202);
    }

    paging_switch_user_pt(saved_cr3);
    dt_finish_proc(proc);
}

static void dt_resume_app(proc_t* proc)
{
    if (!proc) return;
    if (!dt_win_has_free()) return;

    dt_win_t* win = dt_win_alloc(proc);
    if (!win) return;

    proc->minimized = 0;
    proc->state = PROC_RUNNING;
    dt_remove_min(proc);

    dt_focus(proc);
    dt_show_window(win, proc->name);
    dt_select_client();
    proc_set_current(proc);

    uint64_t saved_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(saved_cr3));
    paging_switch_user_pt(proc->page_dir_phys);

    dt_trace_note(proc, proc->ctx.rip, proc->ctx.rsp);
    resume_user_mode(&proc->ctx);

    paging_switch_user_pt(saved_cr3);
    dt_finish_proc(proc);
}

void desktop_splash(void)
{
    int W = scr_w(), H = scr_h();
    int lg = 128;
    int lx = (W - lg) / 2;
    int ly = H / 2 - 140;
    int bw = 400, bh = 8;
    int bx = (W - bw) / 2, by = H / 2 + 110;

    gfx_begin();
    for (int y = 0; y < H; y++) {
        int t = (y * 255) / (H ? H : 1);
        int r = 0x0B + (((0x16 - 0x0B) * t) / 255);
        int g = 0x10 + (((0x1E - 0x10) * t) / 255);
        int b = 0x1A + (((0x38 - 0x1A) * t) / 255);
        gfx_fill_rect(0, y, W, 1, (r << 16) | (g << 8) | b);
    }

    const char* name = "LuminaOS";
    int nw = gfx_text_width(name);
    gfx_draw_text((W - nw) / 2, ly + lg + 30, name, COL_TEXT, GFX_TRANSPARENT);
    const char* sub = "starting up...";
    int sw = gfx_text_width(sub);
    gfx_draw_text((W - sw) / 2, ly + lg + 54, sub, COL_TEXT_DIM, GFX_TRANSPARENT);
    fill_round_rect(bx, by, bw, bh, 4, 0x1E2533);

    int nd = 8;
    static const int dirx[8] = { 34, 24, 0, -24, -34, -24, 0, 24 };
    static const int diry[8] = { 0, 24, 34, 24, 0, -24, -34, -24 };
    for (int p = 0; p <= 100; p += 4) {
        int step = p / 4;

        fill_round_rect(lx, ly, lg, lg, 28, COL_ACCENT);
        fill_round_rect(lx + 14, ly + 14, lg - 28, lg - 28, 20, 0x14213D);
        for (int i = 0; i < nd; i++) {
            int idx = (i + step) % nd;
            int px = lx + lg / 2 + dirx[idx];
            int py = ly + lg / 2 + diry[idx];
            int lit = (i == 0);
            gfx_fill_rect(px - 3, py - 3, 6, 6, lit ? 0xFFFFFF : COL_ACCENT2);
        }

        int fw = (bw - 4) * p / 100;
        if (fw > 0)
            fill_round_rect(bx + 2, by + 2, fw, bh - 4, 3, COL_ACCENT);

        gfx_flush();
        timer_wait(22);
    }
    timer_wait(180);
    gfx_clear(0x000000);
    gfx_flush();
}

int desktop_run(int fs_ready, const char* user)
{
    (void)fs_ready;
    g_user = user;

    int prev_x = -1;
    int prev_y = -1;
    int need_full = 1;
    int last_hover = -2;
    uint32_t last_sec = 0xFFFFFFFF;
    int last_min = -1;

    while (1) {
        proc_wake_check();
        proc_t* next = sched_pick_ready();
        if (next) {
            dt_run_proc(next);
            continue;
        }

        if (need_full) {
            need_full = 0;
            last_sec = timer_ticks() / 1000;
            dt_paint_full();
        }

        if (!g_foreground && keyboard_peek() == '\033') {
            keyboard_getchar_nonblock();
            return 0;
        }

        int mx = mouse_x();
        int my = mouse_y();

        int hover = -1;
        if (!g_foreground) {
            for (int i = 0; i < ICON_COUNT; i++)
                if (dt_point_in(mx, my, icon_x(i), icon_y(i), ICON_W, ICON_H + ICON_LABEL_H)) { hover = i; break; }
        }

        int moved = (mx != prev_x || my != prev_y);
        prev_x = mx;
        prev_y = my;

        if (!g_foreground && hover != last_hover) {
            int old = last_hover;
            last_hover = hover;
            g_hover_icon = hover;
            if (old >= 0)
                dt_repaint_rect(icon_x(old) - 10, icon_y(old) - 10, ICON_W + 20, ICON_H + ICON_LABEL_H + 20);
            if (hover >= 0)
                dt_repaint_rect(icon_x(hover) - 10, icon_y(hover) - 10, ICON_W + 20, ICON_H + ICON_LABEL_H + 20);
        }

        uint32_t sec_now = timer_ticks() / 1000;
        if (sec_now != last_sec) {
            last_sec = sec_now;
            int hh, mm, ss;
            rtc_time(&hh, &mm, &ss);
            int min_now = hh * 60 + mm;
            if (min_now != last_min) {
                last_min = min_now;
                dt_repaint_rect(0, scr_h() - TASKBAR_H, scr_w(), TASKBAR_H);
            }
        }

        if (mouse_click_peek()) {
            dt_win_t* hit = dt_win_at(mx, my);
            if (g_foreground && hit) {
                if (hit->proc != g_foreground) {
                    mouse_click_consume();
                    dt_focus(hit->proc);
                    dt_select_client();
                } else {
                    int bx = hit->x + hit->w - 72;
                    int by = hit->y + 12;
                    if (!dt_point_in(mx, my, bx, by, 16, 16) &&
                        !dt_point_in(mx, my, bx + 26, by, 16, 16))
                        mouse_click_consume();
                }
            } else if (mouse_click_consume()) {
                int handled = 0;
                for (int i = 0; i < ICON_COUNT; i++) {
                    if (dt_point_in(mx, my, icon_x(i), icon_y(i), ICON_W, ICON_H + ICON_LABEL_H)) {
                        handled = 1;
                        if (icons[i].action == APP_REBOOT) {
                            gfx_disable();
                            system_reboot();
                        } else if (icons[i].action == APP_HALT) {
                            system_halt();
                        } else if (icons[i].action == APP_LAUNCH) {
                            dt_run_app(icons[i].name, icons[i].elf, user);
                            char pending[64];
                            if (syscall_pending_file(pending, sizeof(pending)) > 0)
                                dt_run_app("Notepad", "NOTEPAD.ELF", user);
                        }
                        break;
                    }
                }

                if (!handled) {
                    for (int i = 0; i < g_pill_count; i++) {
                        if (dt_point_in(mx, my, g_pills[i].x, g_pills[i].y, g_pills[i].w, g_pills[i].h)) {
                            handled = 1;
                            if (g_pills[i].action == 1)
                                dt_run_app("Terminal", "TERMINAL.ELF", user);
                            else if (g_pills[i].action == 2)
                                dt_resume_app(g_pills[i].proc);
                            break;
                        }
                    }
                }
            }
        }

        if (moved)
            gfx_cursor_show(mx, my);
    }
}
