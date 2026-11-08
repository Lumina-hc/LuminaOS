#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

void mouse_init(void);
void mouse_set_bounds(int w, int h);
void mouse_set_pos(int x, int y);
int mouse_available(void);
int mouse_x(void);
int mouse_y(void);
int mouse_left(void);
int mouse_right(void);
int mouse_click_consume(void);
int mouse_click_peek(void);
void ps2_drain(void);

#endif
