#include <users/render.h>
#include <kernel/gfx.h>
#include <kernel/timer.h>

#define INPUT_CHAR_W 8
#define INPUT_PAD_X  5
#define INPUT_BLINK_MS 500

static void input_fix_scroll(gfx_input_t* in)
{
    int visible = (in->w - INPUT_PAD_X * 2) / INPUT_CHAR_W;
    if (visible < 1)
        visible = 1;
    if (in->scroll < 0)
        in->scroll = 0;
    if (in->cursor < in->scroll)
        in->scroll = in->cursor;
    if (in->cursor - in->scroll >= visible)
        in->scroll = in->cursor - visible + 1;
    if (in->scroll > in->len)
        in->scroll = in->len;
}

int input_hit(const gfx_input_t* in, int mx, int my)
{
    if (!in)
        return 0;
    if (mx < in->x || mx >= in->x + in->w)
        return 0;
    if (my < in->y || my >= in->y + in->h)
        return 0;
    return 1;
}

void input_init(gfx_input_t* in, int x, int y, int w, int h, int max_len)
{
    if (!in)
        return;

    in->x = x;
    in->y = y;
    in->w = w;
    in->h = h;
    in->len = 0;
    in->cursor = 0;
    in->focused = 0;
    in->scroll = 0;
    in->masked = 0;
    in->max_len = max_len;
    if (in->max_len < 2)
        in->max_len = 2;
    if (in->max_len > 256)
        in->max_len = 256;

    for (int i = 0; i < 256; i++)
        in->text[i] = '\0';

    in->fg = 0x00E8ECF4;
    in->bg = 0x00141A24;
    in->border_color = 0x00323C4E;
    in->focus_color = 0x004C8DFF;
    in->cursor_color = 0x00E8ECF4;
    in->cursor_blink_on = 1;
    in->blink_last_ms = timer_ticks();
}

void input_set_focus(gfx_input_t* in, int focused)
{
    if (!in)
        return;
    focused = focused ? 1 : 0;
    if (in->focused == focused)
        return;
    in->focused = focused;
    in->cursor_blink_on = 1;
    in->blink_last_ms = timer_ticks();
}

void input_clear(gfx_input_t* in)
{
    if (!in)
        return;
    in->len = 0;
    in->cursor = 0;
    in->scroll = 0;
    for (int i = 0; i < 256; i++)
        in->text[i] = '\0';
}

void input_draw(gfx_input_t* in)
{
    if (!in)
        return;

    uint32_t border = in->focused ? in->focus_color : in->border_color;
    gfx_fill_rect(in->x + 1, in->y + 1, in->w - 2, in->h - 2, in->bg);
    gfx_draw_rect(in->x, in->y, in->w, in->h, border);

    int visible = (in->w - INPUT_PAD_X * 2) / INPUT_CHAR_W;
    if (visible < 1)
        visible = 1;

    int ty = in->y + (in->h - 8) / 2;
    for (int i = in->scroll; i < in->len && i < in->scroll + visible; i++) {
        char c = in->masked ? '*' : in->text[i];
        int tx = in->x + INPUT_PAD_X + (i - in->scroll) * INPUT_CHAR_W;
        gfx_draw_char(tx, ty, c, in->fg, in->bg);
    }

    if (in->focused && in->cursor_blink_on) {
        int cx = in->x + INPUT_PAD_X + (in->cursor - in->scroll) * INPUT_CHAR_W;
        gfx_fill_rect(cx, in->y + (in->h - 12) / 2, 1, 12, in->cursor_color);
    }
}

void input_on_key(gfx_input_t* in, int key)
{
    if (!in || key <= 0)
        return;

    if (key == '\b') {
        if (in->cursor > 0) {
            for (int i = in->cursor - 1; i < in->len; i++)
                in->text[i] = in->text[i + 1];
            in->cursor--;
            in->len--;
            input_fix_scroll(in);
        }
        return;
    }

    if (key == '\n' || key == '\t' || key == 27)
        return;
    if (key < 32 || key > 126)
        return;
    if (in->len + 1 >= in->max_len)
        return;

    for (int i = in->len; i > in->cursor; i--)
        in->text[i] = in->text[i - 1];
    in->text[in->cursor] = (char)key;
    in->len++;
    in->cursor++;
    in->text[in->len] = '\0';
    input_fix_scroll(in);
}

void input_on_mouse(gfx_input_t* in, int mx, int my, int click)
{
    if (!in || !click)
        return;

    if (!input_hit(in, mx, my)) {
        input_set_focus(in, 0);
        return;
    }

    input_set_focus(in, 1);

    int pos = in->scroll + (mx - in->x - INPUT_PAD_X) / INPUT_CHAR_W;
    if (mx < in->x + INPUT_PAD_X)
        pos = 0;
    if (pos > in->len)
        pos = in->len;
    if (pos < 0)
        pos = 0;
    in->cursor = pos;
    input_fix_scroll(in);
}

void input_tick(gfx_input_t* in, uint32_t now_ms)
{
    if (!in)
        return;
    if (!in->focused) {
        in->cursor_blink_on = 0;
        return;
    }
    if (now_ms - in->blink_last_ms >= INPUT_BLINK_MS) {
        in->blink_last_ms = now_ms;
        in->cursor_blink_on = in->cursor_blink_on ? 0 : 1;
    }
}
