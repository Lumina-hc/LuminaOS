#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

void keyboard_init(void);
void keyboard_feed(uint8_t scancode);
char keyboard_getchar(void);
int keyboard_getchar_nonblock(void);
int keyboard_peek(void);
int keyboard_scancode_nonblock(void);
void keyboard_scancode_flush(void);

#endif
