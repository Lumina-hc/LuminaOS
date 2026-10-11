#ifndef GFX_H
#define GFX_H

#include <stdint.h>

#define GFX_TRANSPARENT 0xFFFFFFFE

void gfx_init(uint16_t width, uint16_t height);
void gfx_disable(void);
int gfx_width(void);
int gfx_height(void);
void gfx_clear(uint32_t color);
void gfx_putpixel(int x, int y, uint32_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint32_t color);
void gfx_draw_rect(int x, int y, int w, int h, uint32_t color);
void gfx_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void gfx_draw_text(int x, int y, const char* s, uint32_t fg, uint32_t bg);
int gfx_text_width(const char* s);
void gfx_cjk_load(const void* data, uint32_t size);
int gfx_cjk_count(void);
void gfx_begin(void);
void gfx_flush(void);
void gfx_set_window(int x, int y, int w, int h);
void gfx_clear_window(void);
void gfx_draw_cursor_abs(int x, int y);
void gfx_cursor_show(int x, int y);
void gfx_cursor_reset(void);
int gfx_window_w(void);
int gfx_window_h(void);

#endif
