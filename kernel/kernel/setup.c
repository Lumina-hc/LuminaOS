#include <kernel/setup.h>
#include <kernel/gfx.h>
#include <kernel/mouse.h>
#include <kernel/keyboard.h>
#include <kernel/timer.h>
#include <kernel/i18n.h>
#include <kernel/config.h>
#include <kernel/time.h>
#include <kernel/auth.h>
#include <users/render.h>

#define WZ_PAGES 5

#define COL_BG      0x000B1020
#define COL_PANEL   0x00101823
#define COL_BORDER  0x002A3547
#define COL_TITLE   0x00FFFFFF
#define COL_TEXT    0x00E8ECF4
#define COL_DIM     0x009AA4B8
#define COL_ACCENT  0x004C8DFF
#define COL_CARD    0x00161D2B
#define COL_CARD_ON 0x001E2B45
#define COL_ERROR   0x00FF6B6B

typedef struct {
    int minutes;
    const char* en;
    const char* zh;
} tz_entry_t;

static const tz_entry_t tz_table[] = {
    { -480, "Los Angeles", "洛杉矶" },
    { -300, "New York", "纽约" },
    {    0, "London", "伦敦" },
    {   60, "Berlin", "柏林" },
    {  180, "Moscow", "莫斯科" },
    {  330, "New Delhi", "新德里" },
    {  480, "Beijing", "北京" },
    {  540, "Tokyo", "东京" },
    {  600, "Sydney", "悉尼" },
    {  420, "Bangkok", "曼谷" }
};

#define TZ_COUNT ((int)(sizeof(tz_table) / sizeof(tz_table[0])))

static int page;
static int sel_lang;
static int sel_tz;
static int finished;
static int created_user;
static const char* err_msg;
static gfx_input_t user_in;
static gfx_input_t pass_in;

static int pw;
static int ph;
static int px;
static int py;

static int text_h(const char* s)
{
    while (*s) {
        if ((unsigned char)*s >= 0x80)
            return 16;
        s++;
    }
    return 8;
}

static int hit(int x, int y, int w, int h, int mx, int my)
{
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

static void copy_str(char* dst, const char* src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void fill_round(int x, int y, int w, int h, int r, uint32_t c)
{
    if (r < 1) {
        gfx_fill_rect(x, y, w, h, c);
        return;
    }
    gfx_fill_rect(x + r, y, w - 2 * r, h, c);
    gfx_fill_rect(x, y + r, w, h - 2 * r, c);
    for (int i = 0; i < r; i++) {
        int dx = r - i;
        gfx_fill_rect(x + i, y + dx, 1, h - 2 * dx, c);
        gfx_fill_rect(x + w - 1 - i, y + dx, 1, h - 2 * dx, c);
    }
}

static void draw_button(int x, int y, int w, int h, const char* label, int primary)
{
    uint32_t bg = primary ? COL_ACCENT : COL_CARD;
    uint32_t fg = primary ? 0x00FFFFFF : COL_TEXT;
    fill_round(x, y, w, h, 8, bg);
    if (!primary)
        gfx_draw_rect(x, y, w, h, COL_BORDER);
    int tw = gfx_text_width(label);
    int th = text_h(label);
    gfx_draw_text(x + (w - tw) / 2, y + (h - th) / 2, label, fg, GFX_TRANSPARENT);
}

static void draw_header(void)
{
    const char* titles[WZ_PAGES] = {
        tr(S_WELCOME), tr(S_LANGUAGE), tr(S_TIMEZONE),
        tr(S_CREATE_USER), tr(S_DONE)
    };
    const char* subs[WZ_PAGES] = {
        tr(S_WELCOME_SUB), tr(S_LANGUAGE_SUB), tr(S_TIMEZONE_SUB),
        tr(S_CREATE_USER_SUB), tr(S_DONE_SUB)
    };
    gfx_draw_text(px + 40, py + 34, titles[page], COL_TITLE, GFX_TRANSPARENT);
    gfx_draw_text(px + 40, py + 62, subs[page], COL_DIM, GFX_TRANSPARENT);
    gfx_fill_rect(px + 40, py + 92, pw - 80, 1, COL_BORDER);
}

static void draw_page_welcome(void)
{
    const char* name = "LuminaOS";
    int tw = gfx_text_width(name);
    fill_round(px + (pw - 96) / 2, py + 130, 96, 96, 24, COL_ACCENT);
    gfx_fill_rect(px + (pw - 96) / 2 + 20, py + 158, 56, 4, 0x00FFFFFF);
    gfx_fill_rect(px + (pw - 96) / 2 + 20, py + 168, 40, 4, 0x00FFFFFF);
    gfx_draw_text(px + (pw - tw) / 2, py + 250, name, COL_TITLE, GFX_TRANSPARENT);

    char line[32];
    copy_str(line, "v0.7.6", sizeof(line));
    int lw = gfx_text_width(line);
    gfx_draw_text(px + (pw - lw) / 2, py + 274, line, COL_DIM, GFX_TRANSPARENT);
}

static void draw_lang_card(int x, int y, int w, int h, const char* label, int idx)
{
    int on = sel_lang == idx;
    fill_round(x, y, w, h, 12, on ? COL_CARD_ON : COL_CARD);
    gfx_draw_rect(x, y, w, h, on ? COL_ACCENT : COL_BORDER);
    int lw = gfx_text_width(label);
    int lh = text_h(label);
    gfx_draw_text(x + (w - lw) / 2, y + (h - lh) / 2, label,
                  on ? COL_TITLE : COL_TEXT, GFX_TRANSPARENT);
}

static void draw_page_language(void)
{
    int cw = 220;
    int ch = 110;
    int gap = 24;
    int total = cw * 2 + gap;
    int x = px + (pw - total) / 2;
    int y = py + 150;
    draw_lang_card(x, y, cw, ch, "English", LANG_EN);
    draw_lang_card(x + cw + gap, y, cw, ch, "中文", LANG_ZH);
}

static void draw_clock_line(int x, int y, int minutes)
{
    os_time_t utc;
    os_time_t loc;
    time_utc(&utc);
    time_add_minutes(&utc, minutes, &loc);

    char buf[16];
    int n = 0;
    buf[n++] = (char)('0' + (loc.hour / 10) % 10);
    buf[n++] = (char)('0' + loc.hour % 10);
    buf[n++] = ':';
    buf[n++] = (char)('0' + (loc.minute / 10) % 10);
    buf[n++] = (char)('0' + loc.minute % 10);
    buf[n] = '\0';
    gfx_draw_text(x, y, buf, COL_TEXT, GFX_TRANSPARENT);
}

static void draw_page_timezone(void)
{
    int cw = 264;
    int ch = 50;
    int gx = 12;
    int gy = 6;
    int total = cw * 2 + gx;
    int x0 = px + (pw - total) / 2;
    int y0 = py + 110;

    for (int i = 0; i < TZ_COUNT; i++) {
        int col = i % 2;
        int row = i / 2;
        int x = x0 + col * (cw + gx);
        int y = y0 + row * (ch + gy);
        int on = sel_tz == i;
        fill_round(x, y, cw, ch, 10, on ? COL_CARD_ON : COL_CARD);
        gfx_draw_rect(x, y, cw, ch, on ? COL_ACCENT : COL_BORDER);

        const char* city = (i18n_lang() == LANG_ZH) ? tz_table[i].zh
                                                    : tz_table[i].en;
        gfx_draw_text(x + 14, y + 10, city, on ? COL_TITLE : COL_TEXT,
                      GFX_TRANSPARENT);

        char tz[12];
        time_format_tz(tz_table[i].minutes, tz, sizeof(tz));
        gfx_draw_text(x + 14, y + 28, tz, COL_DIM, GFX_TRANSPARENT);
        draw_clock_line(x + cw - 58, y + 17, tz_table[i].minutes);
    }
}

static void draw_page_user(void)
{
    int bw = 360;
    int bx = px + (pw - bw) / 2;
    int y1 = py + 128;
    int y2 = py + 206;

    gfx_draw_text(bx, y1, tr(S_USERNAME), COL_DIM, GFX_TRANSPARENT);
    gfx_draw_text(bx, y2, tr(S_PASSWORD), COL_DIM, GFX_TRANSPARENT);

    user_in.x = bx;
    user_in.y = y1 + 20;
    pass_in.x = bx;
    pass_in.y = y2 + 20;

    input_draw(&user_in);
    input_draw(&pass_in);

    if (err_msg)
        gfx_draw_text(bx, py + 316, err_msg, COL_ERROR, GFX_TRANSPARENT);
}

static void draw_summary_row(int y, const char* label, const char* value)
{
    int x = px + 100;
    gfx_draw_text(x, y, label, COL_DIM, GFX_TRANSPARENT);
    gfx_draw_text(x + 120, y, value, COL_TEXT, GFX_TRANSPARENT);
}

static void draw_page_done(const char* user_out)
{
    char tz[12];
    char uline[24];

    time_format_tz(sel_tz >= 0 ? tz_table[sel_tz].minutes : 0, tz, sizeof(tz));
    if (created_user || user_out[0])
        copy_str(uline, user_out, sizeof(uline));
    else
        copy_str(uline, tr(S_NO_USER), sizeof(uline));

    draw_summary_row(py + 150, tr(S_SUMMARY_LANG),
                     sel_lang == LANG_ZH ? "中文" : "English");
    draw_summary_row(py + 190, tr(S_SUMMARY_TZ), tz);
    draw_summary_row(py + 230, tr(S_SUMMARY_USER), uline);

    if (err_msg)
        gfx_draw_text(px + 100, py + 290, err_msg, COL_ERROR, GFX_TRANSPARENT);
}

static int nav_back_x(void) { return px + 40; }
static int nav_btn_y(void) { return py + ph - 66; }
static int nav_next_x(void) { return px + pw - 40 - 160; }

static void paint(const char* user_out)
{
    gfx_begin();
    gfx_clear(COL_BG);

    fill_round(px, py, pw, ph, 16, COL_PANEL);
    gfx_draw_rect(px, py, pw, ph, COL_BORDER);
    gfx_fill_rect(px, py, pw, 3, COL_ACCENT);

    draw_header();

    if (page == 0)
        draw_page_welcome();
    else if (page == 1)
        draw_page_language();
    else if (page == 2)
        draw_page_timezone();
    else if (page == 3)
        draw_page_user();
    else
        draw_page_done(user_out);

    int by = nav_btn_y();
    if (page > 0)
        draw_button(nav_back_x(), by, 160, 40, tr(S_BACK), 0);

    if (page == 4)
        draw_button(nav_next_x(), by, 160, 40, tr(S_START), 1);
    else if (page == 3) {
        draw_button(nav_next_x() - 150, by, 140, 40, tr(S_SKIP), 0);
        draw_button(nav_next_x(), by, 160, 40, tr(S_NEXT), 1);
    } else
        draw_button(nav_next_x(), by, 160, 40, tr(S_NEXT), 1);

    gfx_flush();
}

static void page_enter(void)
{
    err_msg = 0;
    if (page == 3) {
        input_set_focus(&user_in, 1);
        input_set_focus(&pass_in, 0);
    }
}

static int page_next(char* created, int len)
{
    if (page == 3) {
        const char* u = user_in.text;
        const char* p = pass_in.text;
        if (!u[0] || !p[0]) {
            err_msg = tr(S_ERR_EMPTY);
            return 0;
        }
        int r = auth_register(u, p);
        if (r == 0) {
            copy_str(created, u, len);
            created_user = 1;
            page = 4;
            page_enter();
            return 1;
        }
        if (r == -1)
            err_msg = tr(S_ERR_INVALID);
        else if (r == -2)
            err_msg = tr(S_ERR_EXISTS);
        else if (r == -3)
            err_msg = tr(S_ERR_FULL);
        else
            err_msg = tr(S_ERR_FAIL);
        return 0;
    }

    if (page < 4) {
        page++;
        page_enter();
    }
    return 1;
}

void setup_run(char* user_out, int user_len)
{
    user_out[0] = '\0';
    page = 0;
    sel_lang = i18n_lang();
    sel_tz = 6;
    finished = 0;
    created_user = 0;
    err_msg = 0;

    pw = 640;
    ph = 470;
    if (pw > gfx_width() - 40)
        pw = gfx_width() - 40;
    if (ph > gfx_height() - 40)
        ph = gfx_height() - 40;
    px = (gfx_width() - pw) / 2;
    py = (gfx_height() - ph) / 2;
    if (px < 0) px = 0;
    if (py < 0) py = 0;

    int bw = 360;
    int bx = px + (pw - bw) / 2;
    input_init(&user_in, bx, py + 148, bw, 34, 16);
    input_init(&pass_in, bx, py + 226, bw, 34, 16);
    pass_in.masked = 1;

    int last_mx = -1;
    int last_my = -1;
    uint32_t last_blink = 0xFFFFFFFF;

    while (!finished) {
        int mx = mouse_x();
        int my = mouse_y();
        int dirty = 0;

        uint32_t now = timer_ticks();
        input_tick(&user_in, now);
        input_tick(&pass_in, now);
        uint32_t blink = (uint32_t)(user_in.cursor_blink_on) |
                         ((uint32_t)pass_in.cursor_blink_on << 1) |
                         ((now / 500 & 1) << 2);
        if (blink != last_blink) {
            last_blink = blink;
            dirty = 1;
        }

        if (mx != last_mx || my != last_my) {
            last_mx = mx;
            last_my = my;
            dirty = 1;
        }

        int click = mouse_click_consume();
        if (click) {
            dirty = 1;
            if (page == 1) {
                int cw = 220, ch = 110, gap = 24;
                int total = cw * 2 + gap;
                int x = px + (pw - total) / 2;
                int y = py + 150;
                if (hit(x, y, cw, ch, mx, my))
                    sel_lang = LANG_EN;
                if (hit(x + cw + gap, y, cw, ch, mx, my))
                    sel_lang = LANG_ZH;
                i18n_set_lang(sel_lang);
            } else if (page == 2) {
                int cw = 264, ch = 50, gx = 12, gy = 6;
                int total = cw * 2 + gx;
                int x0 = px + (pw - total) / 2;
                int y0 = py + 110;
                for (int i = 0; i < TZ_COUNT; i++) {
                    int x = x0 + (i % 2) * (cw + gx);
                    int y = y0 + (i / 2) * (ch + gy);
                    if (hit(x, y, cw, ch, mx, my))
                        sel_tz = i;
                }
            } else if (page == 3) {
                input_on_mouse(&user_in, mx, my, 1);
                input_on_mouse(&pass_in, mx, my, 1);
            }

            int by = nav_btn_y();
            if (page > 0 && hit(nav_back_x(), by, 160, 40, mx, my)) {
                page--;
                page_enter();
            } else if (page == 4 && hit(nav_next_x(), by, 160, 40, mx, my)) {
                finished = 1;
            } else if (page == 3 && hit(nav_next_x() - 150, by, 140, 40, mx, my)) {
                page = 4;
                page_enter();
            } else if (page < 4 && hit(nav_next_x(), by, 160, 40, mx, my)) {
                page_next(user_out, user_out, user_len);
            }
        }

        int k;
        while ((k = keyboard_getchar_nonblock()) >= 0) {
            dirty = 1;
            if (k == 27) {
                if (page > 0) {
                    page--;
                    page_enter();
                }
                continue;
            }
            if (k == '\n') {
                if (page == 4)
                    finished = 1;
                else
                    page_next(user_out, user_out, user_len);
                continue;
            }
            if (page == 3) {
                if (k == '\t') {
                    int f = user_in.focused;
                    input_set_focus(&user_in, !f);
                    input_set_focus(&pass_in, f);
                } else if (user_in.focused) {
                    input_on_key(&user_in, k);
                } else if (pass_in.focused) {
                    input_on_key(&pass_in, k);
                }
                continue;
            }
            if (page == 1) {
                if (k >= '1' && k <= '2')
                    sel_lang = k - '1';
                i18n_set_lang(sel_lang);
            } else if (page == 2) {
                if (k == 'w' || k == 'W') { sel_tz = (sel_tz + TZ_COUNT - 2) % TZ_COUNT; }
                if (k == 's' || k == 'S') { sel_tz = (sel_tz + 2) % TZ_COUNT; }
                if (k == 'a' || k == 'A') { sel_tz = (sel_tz + TZ_COUNT - 1) % TZ_COUNT; }
                if (k == 'd' || k == 'D') { sel_tz = (sel_tz + 1) % TZ_COUNT; }
            }
        }

        if (dirty) {
            paint(user_out);
            gfx_cursor_show(mx, my);
        }

        timer_wait(10);
    }

    config_set_lang(sel_lang);
    config_set_tz(tz_table[sel_tz].minutes);
    config_mark_done();
    i18n_set_lang(sel_lang);
    time_set_tz_offset(tz_table[sel_tz].minutes);
    if (config_save() != 0)
        err_msg = tr(S_ERR_FAIL);
}
