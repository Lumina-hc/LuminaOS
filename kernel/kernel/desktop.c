#include <kernel/desktop.h>
#include <kernel/gfx.h>
#include <kernel/mouse.h>
#include <kernel/keyboard.h>
#include <kernel/system.h>
#include <kernel/proc.h>
#include <kernel/syscall.h>
#include <kernel/paging.h>
#include <kernel/timer.h>
#include <kernel/gdt.h>
#include <stdint.h>

#define FONT_W 8
#define FONT_H 16

#define DESK_W 800
#define DESK_H 600
#define TASKBAR_H 40

#define WIN_BASE_W 600
#define WIN_BASE_H 440
#define WIN_TITLE_H 30

#define USER_BASE_ADDR 0x08048000
#define USER_STACK_TOP 0x7FF00000
#define USER_STACK_PAGES 16

extern void enter_user_mode(uint64_t rip, uint64_t rsp, uint64_t cs, uint64_t ss, uint64_t rflags);
extern void kernel_resume(void);

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
    { "Files",    0x4F7FBF, "FILES.ELF",   APP_LAUNCH },
    { "Notepad",  0x3FAF6F, "NOTEPAD.ELF", APP_LAUNCH },
    { "About",    0xBF6F3F, "ABOUT.ELF",   APP_LAUNCH },
    { "System",   0x9F9FAF, "SYSTEM.ELF",  APP_LAUNCH },
    { "Reboot",   0xAF4F4F, 0,             APP_REBOOT },
    { "Shutdown", 0x8F8FAF, 0,             APP_HALT  },
};
#define ICON_COUNT ((int)(sizeof(icons) / sizeof(icons[0])))

#define ICON_W 64
#define ICON_H 64
#define ICON_LABEL_H 24
#define ICON_COLS 3
#define ICON_CELL_X 100
#define ICON_CELL_Y 104

static int icon_x(int i)
{
    return 40 + (i % ICON_COLS) * ICON_CELL_X;
}

static int icon_y(int i)
{
    return 30 + (i / ICON_COLS) * ICON_CELL_Y;
}

static int dt_str_len(const char* s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void dt_draw_cursor(int x, int y)
{
    gfx_cursor_show(x, y);
}

static void dt_draw_wallpaper(void)
{
    uint32_t top = 0x1E3A5F;
    uint32_t bot = 0x0F2238;
    int h = DESK_H - TASKBAR_H;
    for (int y = 0; y < h; y++) {
        int t = (y * 255) / (h - 1);
        int r = ((top >> 16) & 0xFF) + ((((bot >> 16) & 0xFF) - ((top >> 16) & 0xFF)) * t / 255);
        int g = ((top >> 8) & 0xFF) + ((((bot >> 8) & 0xFF) - ((top >> 8) & 0xFF)) * t / 255);
        int b = (top & 0xFF) + (((bot & 0xFF) - (top & 0xFF)) * t / 255);
        gfx_fill_rect(0, y, DESK_W, 1, (r << 16) | (g << 8) | b);
    }
}

static void dt_draw_icon(int idx, int hover)
{
    const desktop_icon_t* ic = &icons[idx];
    int x = icon_x(idx);
    int y = icon_y(idx);

    uint32_t base = ic->color;
    uint32_t light = 0xFFFFFF;
    uint32_t dark = 0x000000;

    gfx_fill_rect(x + 3, y + 3, ICON_W, ICON_H, 0x00000055);
    gfx_fill_rect(x, y, ICON_W, ICON_H, base);
    gfx_fill_rect(x, y, ICON_W, 3, light);
    gfx_fill_rect(x, y, 3, ICON_H, light);

    gfx_fill_rect(x + 6, y + 6, ICON_W - 12, 10, dark);
    gfx_fill_rect(x + 6, y + 6, ICON_W - 12, 10, 0x00000088);
    gfx_draw_text(x + 8, y + 8, "Lumina", 0xFFFFFF, base);

    if (hover)
        gfx_draw_rect(x, y, ICON_W, ICON_H, 0xFFFFFF);
    else
        gfx_draw_rect(x, y, ICON_W, ICON_H, dark);

    int text_w = gfx_text_width(ic->name);
    int text_x = x + (ICON_W - text_w) / 2;
    if (text_x < x) text_x = x;
    gfx_draw_text(text_x, y + ICON_H + 8, ic->name, 0xFFFFFF, 0x0F2238);
}

static void dt_draw_taskbar(const char* user)
{
    gfx_fill_rect(0, DESK_H - TASKBAR_H, DESK_W, TASKBAR_H, 0x202020);
    gfx_fill_rect(0, DESK_H - TASKBAR_H, DESK_W, 2, 0x4F7FBF);

    gfx_fill_rect(8, DESK_H - TASKBAR_H + 6, 120, TASKBAR_H - 12, 0x303030);
    gfx_draw_text(28, DESK_H - TASKBAR_H + 12, "LuminaOS", 0xE0E0E0, 0x303030);

    char ubuf[48];
    int n = 0;
    const char* pre = "User: ";
    while (*pre) ubuf[n++] = *pre++;
    if (user && user[0]) {
        while (*user && n < 40) ubuf[n++] = *user++;
    } else {
        ubuf[n++] = 'g'; ubuf[n++] = 'u'; ubuf[n++] = 'e'; ubuf[n++] = 's'; ubuf[n++] = 't';
    }
    ubuf[n] = '\0';

    int tw = dt_str_len(ubuf) * FONT_W;
    gfx_draw_text(DESK_W - tw - 10, DESK_H - TASKBAR_H + 12, ubuf, 0xE0E0E0, 0x202020);
}

static void dt_draw_window_frame(const char* title)
{
    int wx = 100, wy = 80, ww = WIN_BASE_W, wh = WIN_BASE_H;

    gfx_fill_rect(wx, wy, ww, wh, 0xF7F7F7);
    gfx_draw_rect(wx, wy, ww, wh, 0x404040);

    uint32_t c0 = 0x4040A0;
    uint32_t c1 = 0x28287A;
    for (int yy = 0; yy < WIN_TITLE_H; yy++) {
        int t = (yy * 255) / (WIN_TITLE_H - 1);
        int r = ((c0 >> 16) & 0xFF) + ((((c1 >> 16) & 0xFF) - ((c0 >> 16) & 0xFF)) * t / 255);
        int g = ((c0 >> 8) & 0xFF) + ((((c1 >> 8) & 0xFF) - ((c0 >> 8) & 0xFF)) * t / 255);
        int b = (c0 & 0xFF) + (((c1 & 0xFF) - (c0 & 0xFF)) * t / 255);
        gfx_fill_rect(wx + 1, wy + yy, ww - 2, 1, (r << 16) | (g << 8) | b);
    }
    gfx_draw_text(wx + 8, wy + (WIN_TITLE_H - FONT_H) / 2, title, 0xFFFFFF, 0x34348C);

    gfx_fill_rect(wx + ww - 26, wy + 5, 20, 18, 0xC04040);
    gfx_fill_rect(wx + ww - 26, wy + 5, 20, 2, 0xE06060);
    gfx_draw_text(wx + ww - 22, wy + 6, "X", 0xFFFFFF, 0xC04040);
}

static void dt_prepare_win(const char* title, const char* user)
{
    gfx_begin();
    dt_draw_wallpaper();
    for (int i = 0; i < ICON_COUNT; i++)
        dt_draw_icon(i, 0);
    dt_draw_taskbar(user);
    dt_draw_window_frame(title);

    int cx = 101;
    int cy = 80 + WIN_TITLE_H;
    int cw = WIN_BASE_W - 2;
    int ch = WIN_BASE_H - WIN_TITLE_H - 1;
    gfx_set_window(cx, cy, cw, ch);
    gfx_fill_rect(0, 0, cw, ch, 0x202020);
    gfx_flush();
    gfx_cursor_reset();
    dt_draw_cursor(mouse_x(), mouse_y());
}

static int dt_point_in(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

void desktop_win_info(win_info_t* info)
{
    info->win_x = 100;
    info->win_y = 80;
    info->win_w = WIN_BASE_W;
    info->win_h = WIN_BASE_H;
    info->client_x = 101;
    info->client_y = 80 + WIN_TITLE_H;
    info->client_w = WIN_BASE_W - 2;
    info->client_h = WIN_BASE_H - WIN_TITLE_H - 1;
    info->close_x = 100 + WIN_BASE_W - 26;
    info->close_y = 85;
    info->close_w = 20;
    info->close_h = 18;
}

static void dbg_out(const char* msg) {
    for (const char* p = msg; *p; p++)
        __asm__ volatile ("outb %0, $0xE9" :: "a"((uint8_t)*p));
}

static void dt_run_app(const char* title, const char* path, const char* user)
{
    (void)user;
    dt_prepare_win(title, user);

    proc_t* proc = proc_create(path, title);
    if (!proc) {
        dbg_out("E[proc_fail]");
        gfx_clear_window();
        gfx_flush();
        return;
    }

    proc_set_current(proc);

    uint64_t saved_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(saved_cr3));
    paging_switch_user_pt(proc->page_dir_phys);

    dbg_out("D[enter_umode]");
    enter_user_mode(proc->entry_eip, proc->user_esp,
                    GDT_USER_CS | 3, GDT_USER_DS | 3, 0x202);

    paging_switch_user_pt(saved_cr3);

    gfx_clear_window();
    gfx_cursor_reset();

    proc_destroy(proc);
}

int desktop_run(int fs_ready, const char* user)
{
    (void)fs_ready;

    int prev_x = -1;
    int prev_y = -1;
    int dirty = 1;
    int last_hover = -2;

    while (1) {
        int k = keyboard_getchar_nonblock();
        if (k == '\033')
            return 0;

        int mx = mouse_x();
        int my = mouse_y();

        int hover = -1;
        for (int i = 0; i < ICON_COUNT; i++)
            if (dt_point_in(mx, my, icon_x(i), icon_y(i), ICON_W, ICON_H + ICON_LABEL_H)) { hover = i; break; }

        int moved = (mx != prev_x || my != prev_y);
        prev_x = mx;
        prev_y = my;

        if (hover != last_hover) { dirty = 1; last_hover = hover; }

        int left = mouse_click_consume();
        if (left) {
            for (int i = 0; i < ICON_COUNT; i++) {
                if (dt_point_in(mx, my, icon_x(i), icon_y(i), ICON_W, ICON_H + ICON_LABEL_H)) {
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
                        gfx_cursor_reset();
                    }
                    dirty = 1;
                    break;
                }
            }
        }

        if (dirty) {
            gfx_begin();
            dt_draw_wallpaper();

            for (int i = 0; i < ICON_COUNT; i++)
                dt_draw_icon(i, i == hover);

            dt_draw_taskbar(user);
            gfx_flush();
            gfx_cursor_reset();
            dt_draw_cursor(mx, my);
            dirty = 0;
        } else if (moved) {
            dt_draw_cursor(mx, my);
        }
    }
}
