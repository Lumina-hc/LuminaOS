#ifndef _KERNEL_RENDER_H
#define _KERNEL_RENDER_H

#include <stdint.h>

typedef struct {
    int x;
    int y;
    int w;
    int h;
    int len;
    int cursor;
    int focused;
    int scroll;
    int max_len;
    int masked;
    char text[256];
    uint32_t fg;
    uint32_t bg;
    uint32_t border_color;
    uint32_t focus_color;
    uint32_t cursor_color;
    int cursor_blink_on;
    uint32_t blink_last_ms;
} gfx_input_t;

int input_hit(const gfx_input_t* in, int mx, int my);
void input_init(gfx_input_t* in, int x, int y, int w, int h, int max_len);
void input_set_focus(gfx_input_t* in, int focused);
void input_clear(gfx_input_t* in);
void input_draw(gfx_input_t* in);
void input_on_key(gfx_input_t* in, int key);
void input_on_mouse(gfx_input_t* in, int mx, int my, int click);
void input_tick(gfx_input_t* in, uint32_t now_ms);

#endif
