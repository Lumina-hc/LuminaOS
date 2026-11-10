#include <users/password_services.h>
#include <users/render.h>
#include <kernel/gfx.h>

#define PS_PANEL_W 360
#define PS_PANEL_H 430
#define PS_PAD_X   40
#define PS_BOX_W   280
#define PS_BOX_H   32
#define PS_BTN_W   130
#define PS_BTN_H   30

#define PS_COL_PANEL  0x00101823
#define PS_COL_BORDER 0x002A3547
#define PS_COL_TITLE  0x00FFFFFF
#define PS_COL_LABEL  0x00A8B3C4
#define PS_COL_ACCENT 0x004C8DFF
#define PS_COL_TEXT   0x00FFFFFF

static gfx_input_t ps_name;
static gfx_input_t ps_pass;
static gfx_input_t ps_reg_name;
static gfx_input_t ps_reg_pass;

static int ps_px;
static int ps_py;
static int ps_ready;
static int ps_action;

static void ps_layout(void)
{
    int w = gfx_width();
    int h = gfx_height();
    ps_px = (w - PS_PANEL_W) / 2;
    ps_py = (h - PS_PANEL_H) / 2;
    if (ps_px < 0)
        ps_px = 0;
    if (ps_py < 0)
        ps_py = 0;
}

static int ps_box_x(void)
{
    return ps_px + PS_PAD_X;
}

static int ps_btn_x(void)
{
    return ps_px + PS_PANEL_W - PS_PAD_X - PS_BTN_W;
}

static int ps_hit_rect(int bx, int by, int bw, int bh, int mx, int my)
{
    return mx >= bx && mx < bx + bw && my >= by && my < by + bh;
}

static void ps_focus(gfx_input_t* target)
{
    input_set_focus(&ps_name, &ps_name == target);
    input_set_focus(&ps_pass, &ps_pass == target);
    input_set_focus(&ps_reg_name, &ps_reg_name == target);
    input_set_focus(&ps_reg_pass, &ps_reg_pass == target);
}

static void ps_cycle_focus(void)
{
    if (ps_name.focused)
        ps_focus(&ps_pass);
    else if (ps_pass.focused)
        ps_focus(&ps_reg_name);
    else if (ps_reg_name.focused)
        ps_focus(&ps_reg_pass);
    else
        ps_focus(&ps_name);
}

static void ps_label(int y, const char* text)
{
    gfx_draw_text(ps_box_x(), ps_py + y, text, PS_COL_LABEL, GFX_TRANSPARENT);
}

static void ps_button(int y, const char* text)
{
    int bx = ps_btn_x();
    int by = ps_py + y;
    int tw = gfx_text_width(text);
    gfx_fill_rect(bx, by, PS_BTN_W, PS_BTN_H, PS_COL_ACCENT);
    gfx_draw_text(bx + (PS_BTN_W - tw) / 2, by + (PS_BTN_H - 8) / 2, text,
                  PS_COL_TEXT, GFX_TRANSPARENT);
}

void name_input_init(void)
{
    if (!ps_ready) {
        ps_layout();
        ps_ready = 1;
    }
    input_init(&ps_name, ps_box_x(), ps_py + 86, PS_BOX_W, PS_BOX_H, 16);
}

void password_input_init(void)
{
    if (!ps_ready) {
        ps_layout();
        ps_ready = 1;
    }
    input_init(&ps_pass, ps_box_x(), ps_py + 146, PS_BOX_W, PS_BOX_H, 16);
    ps_pass.masked = 1;
}

void reg_name_input_init(void)
{
    if (!ps_ready) {
        ps_layout();
        ps_ready = 1;
    }
    input_init(&ps_reg_name, ps_box_x(), ps_py + 284, PS_BOX_W, PS_BOX_H, 16);
}

void reg_password_input_init(void)
{
    if (!ps_ready) {
        ps_layout();
        ps_ready = 1;
    }
    input_init(&ps_reg_pass, ps_box_x(), ps_py + 344, PS_BOX_W, PS_BOX_H, 16);
    ps_reg_pass.masked = 1;
}

void password_service_init(void)
{
    ps_layout();
    ps_ready = 1;
    ps_action = 0;
    name_input_init();
    password_input_init();
    reg_name_input_init();
    reg_password_input_init();
    ps_focus(&ps_name);
}

void password_service_draw(void)
{
    if (!ps_ready)
        password_service_init();

    gfx_fill_rect(ps_px, ps_py, PS_PANEL_W, PS_PANEL_H, PS_COL_PANEL);
    gfx_draw_rect(ps_px, ps_py, PS_PANEL_W, PS_PANEL_H, PS_COL_BORDER);
    gfx_fill_rect(ps_px, ps_py, PS_PANEL_W, 3, PS_COL_ACCENT);

    const char* title = "LuminaOS";
    gfx_draw_text(ps_px + (PS_PANEL_W - gfx_text_width(title)) / 2, ps_py + 20,
                  title, PS_COL_TITLE, GFX_TRANSPARENT);

    ps_label(48, "Sign in");
    ps_label(70, "User");
    ps_label(130, "Password");
    ps_button(190, "Login");

    gfx_fill_rect(ps_box_x(), ps_py + 232, PS_PANEL_W - PS_PAD_X * 2, 1,
                  PS_COL_BORDER);

    ps_label(240, "Create account");
    ps_label(268, "User");
    ps_label(328, "Password");
    ps_button(388, "Register");

    input_draw(&ps_name);
    input_draw(&ps_pass);
    input_draw(&ps_reg_name);
    input_draw(&ps_reg_pass);
}

void password_service_key(int key)
{
    if (!ps_ready)
        password_service_init();

    if (key == '\t') {
        ps_cycle_focus();
        return;
    }
    if (key == '\n') {
        if (ps_name.focused || ps_pass.focused)
            ps_action = 1;
        else
            ps_action = 2;
        return;
    }

    if (ps_name.focused)
        input_on_key(&ps_name, key);
    else if (ps_pass.focused)
        input_on_key(&ps_pass, key);
    else if (ps_reg_name.focused)
        input_on_key(&ps_reg_name, key);
    else if (ps_reg_pass.focused)
        input_on_key(&ps_reg_pass, key);
}

void password_service_mouse(int mx, int my, int click)
{
    if (!ps_ready)
        password_service_init();
    if (!click)
        return;

    if (ps_hit_rect(ps_btn_x(), ps_py + 190, PS_BTN_W, PS_BTN_H, mx, my)) {
        ps_action = 1;
        return;
    }
    if (ps_hit_rect(ps_btn_x(), ps_py + 388, PS_BTN_W, PS_BTN_H, mx, my)) {
        ps_action = 2;
        return;
    }

    if (input_hit(&ps_name, mx, my))
        ps_focus(&ps_name);
    else if (input_hit(&ps_pass, mx, my))
        ps_focus(&ps_pass);
    else if (input_hit(&ps_reg_name, mx, my))
        ps_focus(&ps_reg_name);
    else if (input_hit(&ps_reg_pass, mx, my))
        ps_focus(&ps_reg_pass);
}

void password_service_tick(uint32_t now_ms)
{
    if (!ps_ready)
        return;
    input_tick(&ps_name, now_ms);
    input_tick(&ps_pass, now_ms);
    input_tick(&ps_reg_name, now_ms);
    input_tick(&ps_reg_pass, now_ms);
}

int password_service_action(void)
{
    return ps_action;
}

void password_service_reset_action(void)
{
    ps_action = 0;
}

void password_service_clear(void)
{
    if (!ps_ready) {
        password_service_init();
        return;
    }
    input_clear(&ps_name);
    input_clear(&ps_pass);
    input_clear(&ps_reg_name);
    input_clear(&ps_reg_pass);
    ps_action = 0;
    ps_focus(&ps_name);
}

const char* name_input_value(void)
{
    return ps_name.text;
}

const char* password_input_value(void)
{
    return ps_pass.text;
}

const char* reg_name_input_value(void)
{
    return ps_reg_name.text;
}

const char* reg_password_input_value(void)
{
    return ps_reg_pass.text;
}
